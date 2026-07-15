#include "vr/vr_openxr.h"
#include "vr/vr_d3d9_capture.h"
#include "vr/vr_d3d9ex_interop_probe.h"
#include "gfx_d3d/r_init.h"

#include "qcommon/qcommon.h"

#include <Windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>
#include <wrl/client.h>

#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include <mutex>
namespace
{
using Microsoft::WRL::ComPtr;

constexpr XrViewConfigurationType kViewConfiguration =
    XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;

XrInstance g_vrInstance = XR_NULL_HANDLE;
XrSystemId g_vrSystemId = XR_NULL_SYSTEM_ID;
XrSession g_vrSession = XR_NULL_HANDLE;
XrSpace g_vrAppSpace = XR_NULL_HANDLE;

constexpr std::uint32_t kVrControllerCount = 2u;

enum VrControllerHand : std::uint32_t
{
    VR_CONTROLLER_LEFT = 0u,
    VR_CONTROLLER_RIGHT = 1u,
};

XrActionSet g_vrControllerActionSet = XR_NULL_HANDLE;

XrAction g_vrGripPoseAction = XR_NULL_HANDLE;
XrAction g_vrAimPoseAction = XR_NULL_HANDLE;
XrAction g_vrTriggerValueAction = XR_NULL_HANDLE;
XrAction g_vrSqueezeValueAction = XR_NULL_HANDLE;

std::array<XrPath, kVrControllerCount>
    g_vrControllerHandPaths = {
        XR_NULL_PATH,
        XR_NULL_PATH,
    };

std::array<XrSpace, kVrControllerCount>
    g_vrControllerGripSpaces = {
        XR_NULL_HANDLE,
        XR_NULL_HANDLE,
    };

std::array<XrSpace, kVrControllerCount>
    g_vrControllerAimSpaces = {
        XR_NULL_HANDLE,
        XR_NULL_HANDLE,
    };

bool g_vrControllerActionsCreated = false;
bool g_vrControllerActionsAttached = false;
bool g_vrControllerSpacesCreated = false;

std::array<bool, kVrControllerCount>
    g_vrLoggedFirstGripPose = {};

std::array<bool, kVrControllerCount>
    g_vrLoggedFirstAimPose = {};

std::array<bool, kVrControllerCount>
    g_vrControllerTriggerPressed = {};

std::array<bool, kVrControllerCount>
    g_vrControllerSqueezePressed = {};

std::uint64_t g_vrControllerDiagnosticFrame = 0u;

struct VrControllerRenderPose
{
    bool gripValid = false;
    bool aimValid = false;
    XrPosef gripPose = {
        {0.0f, 0.0f, 0.0f, 1.0f},
        {0.0f, 0.0f, 0.0f},
    };
    XrPosef aimPose = {
        {0.0f, 0.0f, 0.0f, 1.0f},
        {0.0f, 0.0f, 0.0f},
    };
};

std::array<
    VrControllerRenderPose,
    kVrControllerCount>
    g_vrControllerRenderPoses = {};

std::mutex g_vrWeaponControllerPoseMutex;

bool g_vrRightControllerWeaponPoseValid = false;
bool g_vrRightControllerWeaponBaseValid = false;

float g_vrRightControllerWeaponPosition[3] = {};
float g_vrRightControllerWeaponAxis[3][3] = {};

float g_vrRightControllerWeaponBasePosition[3] = {};
float g_vrRightControllerWeaponBaseAxis[3][3] = {};

bool g_vrLoggedRightControllerWeaponCalibration = false;
bool g_vrLoggedRightControllerWeaponApply = false;

float g_vrRightControllerFinalWeaponForward[3] = {
    1.0f,
    0.0f,
    0.0f,
};

bool g_vrRightControllerFinalWeaponAimValid = false;

float g_vrRightControllerFinalWeaponMuzzleWorld[3] = {};
bool g_vrRightControllerFinalWeaponMuzzleValid = false;
bool g_vrRightControllerFinalWeaponMuzzleBlocked = false;

bool g_vrRightControllerAttackPressed = false;

bool g_vrLoggedRightControllerUsercmdAim = false;
bool g_vrLoggedRightControllerAttackInjection = false;

struct VrControllerProxyVertex
{
    float position[4];
    float color[4];
};

constexpr std::uint32_t
    kVrMaximumControllerProxyVertices = 48u;

ComPtr<ID3D11VertexShader>
    g_vrControllerProxyVertexShader;

ComPtr<ID3D11PixelShader>
    g_vrControllerProxyPixelShader;

ComPtr<ID3D11InputLayout>
    g_vrControllerProxyInputLayout;

ComPtr<ID3D11Buffer>
    g_vrControllerProxyVertexBuffer;

bool g_vrControllerProxyResourcesReady = false;
bool g_vrLoggedFirstControllerProxyDraw = false;

XrSessionState g_vrSessionState = XR_SESSION_STATE_UNKNOWN;
XrEnvironmentBlendMode g_vrBlendMode =
    XR_ENVIRONMENT_BLEND_MODE_OPAQUE;

bool g_vrInitialized = false;
bool g_vrSessionRunning = false;
bool g_vrExitRequested = false;

ComPtr<ID3D11Device> g_vrD3dDevice;
ComPtr<ID3D11DeviceContext> g_vrD3dContext;

ComPtr<ID3D11VertexShader> g_vrTestVertexShader;
ComPtr<ID3D11PixelShader> g_vrTestPixelShader;
ComPtr<ID3D11InputLayout> g_vrTestInputLayout;
ComPtr<ID3D11Buffer> g_vrTestVertexBuffer;
ComPtr<ID3D11Buffer> g_vrTestIndexBuffer;
ComPtr<ID3D11Buffer> g_vrGridVertexBuffer;
ComPtr<ID3D11Buffer> g_vrTestConstantBuffer;
ComPtr<ID3D11RasterizerState> g_vrTestRasterizerState;
ComPtr<ID3D11DepthStencilState> g_vrTestDepthStencilState;

UINT g_vrTestIndexCount = 0;
UINT g_vrGridVertexCount = 0;
bool g_vrLoggedFirstTestFrame = false;

ComPtr<ID3D11VertexShader> g_vrBlitVertexShader;
ComPtr<ID3D11PixelShader> g_vrBlitPixelShader;
ComPtr<ID3D11InputLayout> g_vrBlitInputLayout;
std::array<ComPtr<ID3D11Buffer>, 2>
    g_vrBlitVertexBuffers;
ComPtr<ID3D11SamplerState> g_vrBlitSampler;
constexpr std::uint32_t kVrStereoEyeCount = 2u;

ComPtr<ID3D11Texture2D>
    g_vrCapturedStereoTexture;

ComPtr<ID3D11ShaderResourceView>
    g_vrCapturedStereoView;

std::uint32_t g_vrCapturedStereoWidth = 0u;
std::uint32_t g_vrCapturedStereoHeight = 0u;
std::uint64_t g_vrUploadedStereoSerial = 0u;
bool g_vrLoggedFirstStereoUpload = false;

std::mutex g_vrHeadOrientationMutex;

constexpr float kVrGameUnitsPerMeter =
    39.37007874015748f;

XrVector3f g_vrHeadPositionOrigin = {};
float g_vrHeadPositionLocal[3] = {};
bool g_vrHeadPositionOriginValid = false;
bool g_vrHeadPositionValid = false;
bool g_vrLoggedFirstPositionApply = false;

constexpr float kVrDefaultHalfIpdGameUnits =
    0.032f * kVrGameUnitsPerMeter;

float g_vrHalfIpdGameUnits =
    kVrDefaultHalfIpdGameUnits;

bool g_vrLoggedFirstStereoEyeOffset = false;



float g_vrHeadOrientationAxis[3][3] = {
    {1.0f, 0.0f, 0.0f},
    {0.0f, 1.0f, 0.0f},
    {0.0f, 0.0f, 1.0f},
};

XrQuaternionf g_vrHeadBaseOrientation = {
    0.0f,
    0.0f,
    0.0f,
    1.0f,
};

bool g_vrHeadBaseOrientationValid = false;
bool g_vrHeadOrientationValid = false;
bool g_vrLoggedFirstHeadPose = false;
bool g_vrLoggedFirstCameraApply = false;


std::vector<XrViewConfigurationView> g_vrViewConfigs;
std::vector<XrView> g_vrViews;

struct VrEyeProjectionTangents
{
    float left = 0.0f;
    float right = 0.0f;
    float down = 0.0f;
    float up = 0.0f;
};

std::mutex g_vrProjectionMutex;

std::array<
    VrEyeProjectionTangents,
    kVrStereoEyeCount>
    g_vrEyeProjectionTangents = {};

bool g_vrEyeProjectionValid = false;

thread_local int g_vrCurrentRenderEye = -1;

std::array<bool, kVrStereoEyeCount>
    g_vrLoggedProjectionApply = {};

bool g_vrLoggedProjectionPublish = false;

struct VrEyeSwapchain
{
    XrSwapchain handle = XR_NULL_HANDLE;
    int32_t width = 0;
    int32_t height = 0;

    std::vector<XrSwapchainImageD3D11KHR> images;
    std::vector<ComPtr<ID3D11RenderTargetView>> renderTargetViews;
    std::vector<ComPtr<ID3D11Texture2D>> depthTextures;
    std::vector<ComPtr<ID3D11DepthStencilView>> depthStencilViews;
};

std::vector<VrEyeSwapchain> g_vrEyeSwapchains;

const char* VR_SessionStateName(const XrSessionState state)
{
    switch (state)
    {
        case XR_SESSION_STATE_UNKNOWN:
            return "UNKNOWN";
        case XR_SESSION_STATE_IDLE:
            return "IDLE";
        case XR_SESSION_STATE_READY:
            return "READY";
        case XR_SESSION_STATE_SYNCHRONIZED:
            return "SYNCHRONIZED";
        case XR_SESSION_STATE_VISIBLE:
            return "VISIBLE";
        case XR_SESSION_STATE_FOCUSED:
            return "FOCUSED";
        case XR_SESSION_STATE_STOPPING:
            return "STOPPING";
        case XR_SESSION_STATE_LOSS_PENDING:
            return "LOSS_PENDING";
        case XR_SESSION_STATE_EXITING:
            return "EXITING";
        default:
            return "UNRECOGNIZED";
    }
}

void VR_LogXrFailure(const char* operation, const XrResult result)
{
    char resultText[XR_MAX_RESULT_STRING_SIZE] = {};

    if (g_vrInstance != XR_NULL_HANDLE &&
        XR_SUCCEEDED(
            xrResultToString(
                g_vrInstance,
                result,
                resultText)))
    {
        Com_PrintWarning(
            0,
            "[VR] %s failed: %s (%d).\n",
            operation,
            resultText,
            static_cast<int>(result));
    }
    else
    {
        Com_PrintWarning(
            0,
            "[VR] %s failed with result %d.\n",
            operation,
            static_cast<int>(result));
    }
}

void VR_LogHrFailure(const char* operation, const HRESULT hr)
{
    Com_PrintWarning(
        0,
        "[VR] %s failed with HRESULT 0x%08lX.\n",
        operation,
        static_cast<unsigned long>(hr));
}

bool VR_HasInstanceExtension(const char* requestedExtension)
{
    uint32_t extensionCount = 0;

    XrResult result =
        xrEnumerateInstanceExtensionProperties(
            nullptr,
            0,
            &extensionCount,
            nullptr);

    if (XR_FAILED(result))
    {
        VR_LogXrFailure(
            "xrEnumerateInstanceExtensionProperties(count)",
            result);

        return false;
    }

    std::vector<XrExtensionProperties> extensions(extensionCount);

    for (XrExtensionProperties& extension : extensions)
    {
        extension = XrExtensionProperties{
            XR_TYPE_EXTENSION_PROPERTIES
        };
    }

    result =
        xrEnumerateInstanceExtensionProperties(
            nullptr,
            extensionCount,
            &extensionCount,
            extensions.data());

    if (XR_FAILED(result))
    {
        VR_LogXrFailure(
            "xrEnumerateInstanceExtensionProperties(list)",
            result);

        return false;
    }

    for (const XrExtensionProperties& extension : extensions)
    {
        if (std::strcmp(
                extension.extensionName,
                requestedExtension) == 0)
        {
            return true;
        }
    }

    return false;
}

bool VR_LuidMatches(const LUID& left, const LUID& right)
{
    return
        left.LowPart == right.LowPart &&
        left.HighPart == right.HighPart;
}

bool VR_CreateD3D11Device(
    const XrGraphicsRequirementsD3D11KHR& requirements)
{
    ComPtr<IDXGIFactory> factory;

    HRESULT hr =
        CreateDXGIFactory(
            IID_PPV_ARGS(factory.GetAddressOf()));

    if (FAILED(hr))
    {
        VR_LogHrFailure("CreateDXGIFactory", hr);
        return false;
    }

    ComPtr<IDXGIAdapter> selectedAdapter;

    for (UINT adapterIndex = 0; ; ++adapterIndex)
    {
        ComPtr<IDXGIAdapter> candidateAdapter;

        hr = factory->EnumAdapters(
            adapterIndex,
            candidateAdapter.GetAddressOf());

        if (hr == DXGI_ERROR_NOT_FOUND)
        {
            break;
        }

        if (FAILED(hr))
        {
            VR_LogHrFailure("IDXGIFactory::EnumAdapters", hr);
            return false;
        }

        DXGI_ADAPTER_DESC description = {};

        hr = candidateAdapter->GetDesc(&description);

        if (FAILED(hr))
        {
            VR_LogHrFailure("IDXGIAdapter::GetDesc", hr);
            return false;
        }

        if (VR_LuidMatches(
                description.AdapterLuid,
                requirements.adapterLuid))
        {
            selectedAdapter = candidateAdapter;
            break;
        }
    }

    if (!selectedAdapter)
    {
        Com_PrintWarning(
            0,
            "[VR] Could not find the DXGI adapter requested by OpenXR.\n");

        return false;
    }

    // The June 2010 DirectX SDK headers stop at feature level 11_0.
    // OpenXR already tells us the minimum level required by the runtime, so
    // request that exact level without referring to newer enum constants.
    D3D_FEATURE_LEVEL requestedFeatureLevel =
        requirements.minFeatureLevel;

    D3D_FEATURE_LEVEL createdFeatureLevel =
        D3D_FEATURE_LEVEL_10_0;

    hr = D3D11CreateDevice(
        selectedAdapter.Get(),
        D3D_DRIVER_TYPE_UNKNOWN,
        nullptr,
        D3D11_CREATE_DEVICE_BGRA_SUPPORT,
        &requestedFeatureLevel,
        1,
        D3D11_SDK_VERSION,
        g_vrD3dDevice.GetAddressOf(),
        &createdFeatureLevel,
        g_vrD3dContext.GetAddressOf());

    if (FAILED(hr))
    {
        VR_LogHrFailure("D3D11CreateDevice", hr);
        return false;
    }

    if (!VR_ProbeD3D9ExD3D11Interop(
            dx.device,
            g_vrD3dDevice.Get(),
            requirements.adapterLuid))
    {
        Com_PrintWarning(
            0,
            "[VR] D3D9Ex/D3D11 shared-texture "
            "interop probe did not pass. "
            "The current CPU bridge remains active.\n");
    }

    Com_Printf(
        0,
        "[VR] Created OpenXR D3D11 device at feature level 0x%X.\n",
        static_cast<unsigned int>(createdFeatureLevel));

    return true;
}


struct VrTestMatrix
{
    float m[4][4];
};

struct VrTestVertex
{
    float position[3];
    float color[3];
};

struct VrTestConstants
{
    VrTestMatrix modelViewProjection;
};

VrTestMatrix VR_TestIdentity()
{
    VrTestMatrix result = {};
    result.m[0][0] = 1.0f;
    result.m[1][1] = 1.0f;
    result.m[2][2] = 1.0f;
    result.m[3][3] = 1.0f;
    return result;
}

VrTestMatrix VR_TestMultiply(
    const VrTestMatrix& left,
    const VrTestMatrix& right)
{
    VrTestMatrix result = {};

    for (int row = 0; row < 4; ++row)
    {
        for (int column = 0; column < 4; ++column)
        {
            for (int element = 0; element < 4; ++element)
            {
                result.m[row][column] +=
                    left.m[row][element] *
                    right.m[element][column];
            }
        }
    }

    return result;
}

VrTestMatrix VR_TestTranslation(
    const float x,
    const float y,
    const float z)
{
    VrTestMatrix result = VR_TestIdentity();
    result.m[3][0] = x;
    result.m[3][1] = y;
    result.m[3][2] = z;
    return result;
}

VrTestMatrix VR_TestRotation(
    const XrQuaternionf& q)
{
    const float xx = q.x * q.x;
    const float yy = q.y * q.y;
    const float zz = q.z * q.z;
    const float xy = q.x * q.y;
    const float xz = q.x * q.z;
    const float yz = q.y * q.z;
    const float xw = q.x * q.w;
    const float yw = q.y * q.w;
    const float zw = q.z * q.w;

    VrTestMatrix result = VR_TestIdentity();

    result.m[0][0] = 1.0f - 2.0f * (yy + zz);
    result.m[0][1] = 2.0f * (xy + zw);
    result.m[0][2] = 2.0f * (xz - yw);

    result.m[1][0] = 2.0f * (xy - zw);
    result.m[1][1] = 1.0f - 2.0f * (xx + zz);
    result.m[1][2] = 2.0f * (yz + xw);

    result.m[2][0] = 2.0f * (xz + yw);
    result.m[2][1] = 2.0f * (yz - xw);
    result.m[2][2] = 1.0f - 2.0f * (xx + yy);

    return result;
}

VrTestMatrix VR_TestView(const XrPosef& pose)
{
    const XrQuaternionf inverseOrientation = {
        -pose.orientation.x,
        -pose.orientation.y,
        -pose.orientation.z,
        pose.orientation.w,
    };

    return VR_TestMultiply(
        VR_TestTranslation(
            -pose.position.x,
            -pose.position.y,
            -pose.position.z),
        VR_TestRotation(inverseOrientation));
}

VrTestMatrix VR_TestProjection(
    const XrFovf& fov,
    const float nearDistance,
    const float farDistance)
{
    const float left = std::tan(fov.angleLeft);
    const float right = std::tan(fov.angleRight);
    const float down = std::tan(fov.angleDown);
    const float up = std::tan(fov.angleUp);

    const float width = right - left;
    const float height = up - down;

    VrTestMatrix result = {};

    result.m[0][0] = 2.0f / width;
    result.m[1][1] = 2.0f / height;
    result.m[2][0] = (right + left) / width;
    result.m[2][1] = (up + down) / height;
    result.m[2][2] =
        -farDistance / (farDistance - nearDistance);
    result.m[2][3] = -1.0f;
    result.m[3][2] =
        -(farDistance * nearDistance) /
        (farDistance - nearDistance);

    return result;
}

using VrD3DCompileFunction = HRESULT (WINAPI*)(
    LPCVOID,
    SIZE_T,
    LPCSTR,
    const D3D_SHADER_MACRO*,
    ID3DInclude*,
    LPCSTR,
    LPCSTR,
    UINT,
    UINT,
    ID3DBlob**,
    ID3DBlob**);

bool VR_TestCompileShader(
    const char* source,
    const char* entryPoint,
    const char* target,
    ComPtr<ID3DBlob>& output)
{
    const char* compilerDlls[] = {
        "d3dcompiler_47.dll",
        "d3dcompiler_46.dll",
        "d3dcompiler_43.dll",
    };

    HMODULE module = nullptr;

    for (const char* dllName : compilerDlls)
    {
        module = LoadLibraryA(dllName);

        if (module != nullptr)
        {
            break;
        }
    }

    if (module == nullptr)
    {
        Com_PrintWarning(
            0,
            "[VR] No D3DCompiler DLL could be loaded.\n");
        return false;
    }

    const auto compile =
        reinterpret_cast<VrD3DCompileFunction>(
            GetProcAddress(module, "D3DCompile"));

    if (compile == nullptr)
    {
        // Keep the D3DCompiler module loaded for the process lifetime.
// ID3DBlob instances returned by D3DCompile are implemented by this
// DLL and may still be alive after this function returns.
        Com_PrintWarning(
            0,
            "[VR] D3DCompile export was not found.\n");
        return false;
    }

    ComPtr<ID3DBlob> errors;

    const HRESULT hr =
        compile(
            source,
            std::strlen(source),
            "KisakCOD VR test shader",
            nullptr,
            nullptr,
            entryPoint,
            target,
            D3DCOMPILE_ENABLE_STRICTNESS,
            0,
            output.GetAddressOf(),
            errors.GetAddressOf());

    if (FAILED(hr))
    {
        if (errors)
        {
            Com_PrintWarning(
                0,
                "[VR] Shader compiler output: %s\n",
                static_cast<const char*>(
                    errors->GetBufferPointer()));
        }

        VR_LogHrFailure("D3DCompile", hr);
        // Keep the D3DCompiler module loaded for the process lifetime.
// ID3DBlob instances returned by D3DCompile are implemented by this
// DLL and may still be alive after this function returns.
        return false;
    }

    // Keep the D3DCompiler module loaded for the process lifetime.
// ID3DBlob instances returned by D3DCompile are implemented by this
// DLL and may still be alive after this function returns.
    return true;
}


struct VrBlitVertex
{
    float position[2];
    float uv[2];
};

bool VR_CreateCapturedFrameBlitResources()
{
    static const char* shaderSource = R"(
Texture2D capturedFrame : register(t0);
SamplerState capturedSampler : register(s0);

struct VertexInput
{
    float2 position : POSITION;
    float2 uv : TEXCOORD0;
};

struct PixelInput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

PixelInput VSMain(VertexInput input)
{
    PixelInput output;
    output.position = float4(input.position, 0.0f, 1.0f);
    output.uv = input.uv;
    return output;
}

float4 PSMain(PixelInput input) : SV_TARGET
{
    return capturedFrame.Sample(
        capturedSampler,
        input.uv);
}
)";

    ComPtr<ID3DBlob> vertexCode;
    ComPtr<ID3DBlob> pixelCode;

    if (!VR_TestCompileShader(
            shaderSource,
            "VSMain",
            "vs_4_0",
            vertexCode))
    {
        return false;
    }

    if (!VR_TestCompileShader(
            shaderSource,
            "PSMain",
            "ps_4_0",
            pixelCode))
    {
        return false;
    }

    HRESULT hr =
        g_vrD3dDevice->CreateVertexShader(
            vertexCode->GetBufferPointer(),
            vertexCode->GetBufferSize(),
            nullptr,
            g_vrBlitVertexShader.GetAddressOf());

    if (FAILED(hr))
    {
        VR_LogHrFailure(
            "CreateVertexShader(capture blit)",
            hr);

        return false;
    }

    hr =
        g_vrD3dDevice->CreatePixelShader(
            pixelCode->GetBufferPointer(),
            pixelCode->GetBufferSize(),
            nullptr,
            g_vrBlitPixelShader.GetAddressOf());

    if (FAILED(hr))
    {
        VR_LogHrFailure(
            "CreatePixelShader(capture blit)",
            hr);

        return false;
    }

    const D3D11_INPUT_ELEMENT_DESC layout[] = {
        {
            "POSITION",
            0,
            DXGI_FORMAT_R32G32_FLOAT,
            0,
            0,
            D3D11_INPUT_PER_VERTEX_DATA,
            0,
        },
        {
            "TEXCOORD",
            0,
            DXGI_FORMAT_R32G32_FLOAT,
            0,
            8,
            D3D11_INPUT_PER_VERTEX_DATA,
            0,
        },
    };

    hr =
        g_vrD3dDevice->CreateInputLayout(
            layout,
            2,
            vertexCode->GetBufferPointer(),
            vertexCode->GetBufferSize(),
            g_vrBlitInputLayout.GetAddressOf());

    if (FAILED(hr))
    {
        VR_LogHrFailure(
            "CreateInputLayout(capture blit)",
            hr);

        return false;
    }

    static const VrBlitVertex eyeVertices[2][4] = {
        {
            {{-1.0f,  1.0f}, {0.0f, 0.0f}},
            {{ 1.0f,  1.0f}, {0.5f, 0.0f}},
            {{-1.0f, -1.0f}, {0.0f, 1.0f}},
            {{ 1.0f, -1.0f}, {0.5f, 1.0f}},
        },
        {
            {{-1.0f,  1.0f}, {0.5f, 0.0f}},
            {{ 1.0f,  1.0f}, {1.0f, 0.0f}},
            {{-1.0f, -1.0f}, {0.5f, 1.0f}},
            {{ 1.0f, -1.0f}, {1.0f, 1.0f}},
        },
    };

    D3D11_BUFFER_DESC vertexDescription = {};
    vertexDescription.ByteWidth =
        static_cast<UINT>(
            sizeof(eyeVertices[0]));
    vertexDescription.Usage =
        D3D11_USAGE_IMMUTABLE;
    vertexDescription.BindFlags =
        D3D11_BIND_VERTEX_BUFFER;

    for (std::uint32_t eyeIndex = 0;
         eyeIndex < 2u;
         ++eyeIndex)
    {
        D3D11_SUBRESOURCE_DATA vertexData = {};
        vertexData.pSysMem =
            eyeVertices[eyeIndex];

        hr =
            g_vrD3dDevice->CreateBuffer(
                &vertexDescription,
                &vertexData,
                g_vrBlitVertexBuffers[eyeIndex]
                    .GetAddressOf());

        if (FAILED(hr))
        {
            VR_LogHrFailure(
                "CreateBuffer(stereo capture blit)",
                hr);

            return false;
        }
    }

    D3D11_SAMPLER_DESC samplerDescription = {};
    samplerDescription.Filter =
        D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    samplerDescription.AddressU =
        D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDescription.AddressV =
        D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDescription.AddressW =
        D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDescription.MaxLOD =
        D3D11_FLOAT32_MAX;

    hr =
        g_vrD3dDevice->CreateSamplerState(
            &samplerDescription,
            g_vrBlitSampler.GetAddressOf());

    if (FAILED(hr))
    {
        VR_LogHrFailure(
            "CreateSamplerState(capture blit)",
            hr);

        return false;
    }

    Com_Printf(
        0,
        "[VR] D3D9 captured-frame blit resources created.\n");

    return true;
}

bool VR_UpdateCapturedStereoTexture()
{
    std::vector<std::uint8_t> pixels;
    std::uint32_t width = 0u;
    std::uint32_t height = 0u;
    std::uint64_t serial = 0u;

    if (!VR_D3D9CopyLatestStereoFrame(
            g_vrUploadedStereoSerial,
            pixels,
            width,
            height,
            serial))
    {
        return
            g_vrCapturedStereoView !=
            nullptr;
    }

    if (!g_vrCapturedStereoTexture ||
        width != g_vrCapturedStereoWidth ||
        height != g_vrCapturedStereoHeight)
    {
        g_vrCapturedStereoView.Reset();
        g_vrCapturedStereoTexture.Reset();

        D3D11_TEXTURE2D_DESC textureDescription = {};
        textureDescription.Width = width;
        textureDescription.Height = height;
        textureDescription.MipLevels = 1;
        textureDescription.ArraySize = 1;
        textureDescription.Format =
            DXGI_FORMAT_B8G8R8A8_UNORM;
        textureDescription.SampleDesc.Count = 1;
        textureDescription.Usage =
            D3D11_USAGE_DEFAULT;
        textureDescription.BindFlags =
            D3D11_BIND_SHADER_RESOURCE;

        HRESULT hr =
            g_vrD3dDevice->CreateTexture2D(
                &textureDescription,
                nullptr,
                g_vrCapturedStereoTexture
                    .GetAddressOf());

        if (FAILED(hr))
        {
            VR_LogHrFailure(
                "CreateTexture2D(stereo capture)",
                hr);

            return false;
        }

        hr =
            g_vrD3dDevice->CreateShaderResourceView(
                g_vrCapturedStereoTexture.Get(),
                nullptr,
                g_vrCapturedStereoView
                    .GetAddressOf());

        if (FAILED(hr))
        {
            VR_LogHrFailure(
                "CreateShaderResourceView("
                "stereo capture)",
                hr);

            g_vrCapturedStereoTexture.Reset();
            return false;
        }

        g_vrCapturedStereoWidth = width;
        g_vrCapturedStereoHeight = height;
    }

    g_vrD3dContext->UpdateSubresource(
        g_vrCapturedStereoTexture.Get(),
        0,
        nullptr,
        pixels.data(),
        width * 4u,
        0);

    g_vrUploadedStereoSerial = serial;

    if (!g_vrLoggedFirstStereoUpload)
    {
        Com_Printf(
            0,
            "[VR] Uploaded first complete side-by-side "
            "D3D9 frame to one D3D11 texture: "
            "%u x %u.\n",
            width,
            height);

        g_vrLoggedFirstStereoUpload = true;
    }

    return true;
}

bool VR_CreateHeadTrackedScene()
{
    static const char* shaderSource = R"(
cbuffer TestConstants : register(b0)
{
    row_major float4x4 modelViewProjection;
};

struct VertexInput
{
    float3 position : POSITION;
    float3 color : COLOR;
};

struct PixelInput
{
    float4 position : SV_POSITION;
    float3 color : COLOR;
};

PixelInput VSMain(VertexInput input)
{
    PixelInput output;

    output.position =
        mul(float4(input.position, 1.0f),
            modelViewProjection);

    output.color = input.color;

    return output;
}

float4 PSMain(PixelInput input) : SV_TARGET
{
    return float4(input.color, 1.0f);
}
)";

    ComPtr<ID3DBlob> vertexCode;
    ComPtr<ID3DBlob> pixelCode;

    if (!VR_TestCompileShader(
            shaderSource,
            "VSMain",
            "vs_4_0",
            vertexCode))
    {
        return false;
    }

    if (!VR_TestCompileShader(
            shaderSource,
            "PSMain",
            "ps_4_0",
            pixelCode))
    {
        return false;
    }

    HRESULT hr =
        g_vrD3dDevice->CreateVertexShader(
            vertexCode->GetBufferPointer(),
            vertexCode->GetBufferSize(),
            nullptr,
            g_vrTestVertexShader.GetAddressOf());

    if (FAILED(hr))
    {
        VR_LogHrFailure("CreateVertexShader", hr);
        return false;
    }

    hr =
        g_vrD3dDevice->CreatePixelShader(
            pixelCode->GetBufferPointer(),
            pixelCode->GetBufferSize(),
            nullptr,
            g_vrTestPixelShader.GetAddressOf());

    if (FAILED(hr))
    {
        VR_LogHrFailure("CreatePixelShader", hr);
        return false;
    }

    const D3D11_INPUT_ELEMENT_DESC layout[] = {
        {
            "POSITION",
            0,
            DXGI_FORMAT_R32G32B32_FLOAT,
            0,
            0,
            D3D11_INPUT_PER_VERTEX_DATA,
            0,
        },
        {
            "COLOR",
            0,
            DXGI_FORMAT_R32G32B32_FLOAT,
            0,
            12,
            D3D11_INPUT_PER_VERTEX_DATA,
            0,
        },
    };

    hr =
        g_vrD3dDevice->CreateInputLayout(
            layout,
            2,
            vertexCode->GetBufferPointer(),
            vertexCode->GetBufferSize(),
            g_vrTestInputLayout.GetAddressOf());

    if (FAILED(hr))
    {
        VR_LogHrFailure("CreateInputLayout", hr);
        return false;
    }

    static const VrTestVertex cubeVertices[] = {
        {{-0.40f, -0.40f, -0.40f}, {1.0f, 0.1f, 0.1f}},
        {{-0.40f,  0.40f, -0.40f}, {0.1f, 1.0f, 0.1f}},
        {{ 0.40f,  0.40f, -0.40f}, {0.1f, 0.2f, 1.0f}},
        {{ 0.40f, -0.40f, -0.40f}, {1.0f, 1.0f, 0.1f}},
        {{-0.40f, -0.40f,  0.40f}, {1.0f, 0.1f, 1.0f}},
        {{-0.40f,  0.40f,  0.40f}, {0.1f, 1.0f, 1.0f}},
        {{ 0.40f,  0.40f,  0.40f}, {1.0f, 1.0f, 1.0f}},
        {{ 0.40f, -0.40f,  0.40f}, {0.8f, 0.4f, 0.1f}},
    };

    static const unsigned short cubeIndices[] = {
        0, 1, 2,  0, 2, 3,
        4, 6, 5,  4, 7, 6,
        4, 5, 1,  4, 1, 0,
        3, 2, 6,  3, 6, 7,
        1, 5, 6,  1, 6, 2,
        4, 0, 3,  4, 3, 7,
    };

    g_vrTestIndexCount =
        static_cast<UINT>(
            sizeof(cubeIndices) /
            sizeof(cubeIndices[0]));

    D3D11_BUFFER_DESC cubeVertexDescription = {};
    cubeVertexDescription.ByteWidth =
        static_cast<UINT>(sizeof(cubeVertices));
    cubeVertexDescription.Usage =
        D3D11_USAGE_IMMUTABLE;
    cubeVertexDescription.BindFlags =
        D3D11_BIND_VERTEX_BUFFER;

    D3D11_SUBRESOURCE_DATA cubeVertexData = {};
    cubeVertexData.pSysMem = cubeVertices;

    hr =
        g_vrD3dDevice->CreateBuffer(
            &cubeVertexDescription,
            &cubeVertexData,
            g_vrTestVertexBuffer.GetAddressOf());

    if (FAILED(hr))
    {
        VR_LogHrFailure("CreateBuffer(cube vertices)", hr);
        return false;
    }

    D3D11_BUFFER_DESC cubeIndexDescription = {};
    cubeIndexDescription.ByteWidth =
        static_cast<UINT>(sizeof(cubeIndices));
    cubeIndexDescription.Usage =
        D3D11_USAGE_IMMUTABLE;
    cubeIndexDescription.BindFlags =
        D3D11_BIND_INDEX_BUFFER;

    D3D11_SUBRESOURCE_DATA cubeIndexData = {};
    cubeIndexData.pSysMem = cubeIndices;

    hr =
        g_vrD3dDevice->CreateBuffer(
            &cubeIndexDescription,
            &cubeIndexData,
            g_vrTestIndexBuffer.GetAddressOf());

    if (FAILED(hr))
    {
        VR_LogHrFailure("CreateBuffer(cube indices)", hr);
        return false;
    }

    std::vector<VrTestVertex> gridVertices;

    constexpr int gridHalfSize = 10;
    constexpr float gridSpacing = 0.50f;
    constexpr float gridY = -1.40f;

    for (int line = -gridHalfSize;
         line <= gridHalfSize;
         ++line)
    {
        const float coordinate =
            static_cast<float>(line) * gridSpacing;

        const bool majorLine = (line % 5) == 0;

        const float brightness =
            majorLine ? 0.50f : 0.20f;

        const float extent =
            static_cast<float>(gridHalfSize) *
            gridSpacing;

        gridVertices.push_back({
            {-extent, gridY, coordinate},
            {brightness, brightness, brightness},
        });

        gridVertices.push_back({
            { extent, gridY, coordinate},
            {brightness, brightness, brightness},
        });

        gridVertices.push_back({
            {coordinate, gridY, -extent},
            {brightness, brightness, brightness},
        });

        gridVertices.push_back({
            {coordinate, gridY,  extent},
            {brightness, brightness, brightness},
        });
    }

    g_vrGridVertexCount =
        static_cast<UINT>(gridVertices.size());

    D3D11_BUFFER_DESC gridDescription = {};
    gridDescription.ByteWidth =
        static_cast<UINT>(
            gridVertices.size() *
            sizeof(VrTestVertex));
    gridDescription.Usage =
        D3D11_USAGE_IMMUTABLE;
    gridDescription.BindFlags =
        D3D11_BIND_VERTEX_BUFFER;

    D3D11_SUBRESOURCE_DATA gridData = {};
    gridData.pSysMem = gridVertices.data();

    hr =
        g_vrD3dDevice->CreateBuffer(
            &gridDescription,
            &gridData,
            g_vrGridVertexBuffer.GetAddressOf());

    if (FAILED(hr))
    {
        VR_LogHrFailure("CreateBuffer(grid vertices)", hr);
        return false;
    }

    D3D11_BUFFER_DESC constantDescription = {};
    constantDescription.ByteWidth =
        sizeof(VrTestConstants);
    constantDescription.Usage =
        D3D11_USAGE_DEFAULT;
    constantDescription.BindFlags =
        D3D11_BIND_CONSTANT_BUFFER;

    hr =
        g_vrD3dDevice->CreateBuffer(
            &constantDescription,
            nullptr,
            g_vrTestConstantBuffer.GetAddressOf());

    if (FAILED(hr))
    {
        VR_LogHrFailure("CreateBuffer(constants)", hr);
        return false;
    }

    D3D11_RASTERIZER_DESC rasterizerDescription = {};
    rasterizerDescription.FillMode = D3D11_FILL_SOLID;
    rasterizerDescription.CullMode = D3D11_CULL_NONE;
    rasterizerDescription.DepthClipEnable = TRUE;

    hr =
        g_vrD3dDevice->CreateRasterizerState(
            &rasterizerDescription,
            g_vrTestRasterizerState.GetAddressOf());

    if (FAILED(hr))
    {
        VR_LogHrFailure("CreateRasterizerState", hr);
        return false;
    }

    D3D11_DEPTH_STENCIL_DESC depthDescription = {};
    depthDescription.DepthEnable = TRUE;
    depthDescription.DepthWriteMask =
        D3D11_DEPTH_WRITE_MASK_ALL;
    depthDescription.DepthFunc =
        D3D11_COMPARISON_LESS_EQUAL;

    hr =
        g_vrD3dDevice->CreateDepthStencilState(
            &depthDescription,
            g_vrTestDepthStencilState.GetAddressOf());

    if (FAILED(hr))
    {
        VR_LogHrFailure("CreateDepthStencilState", hr);
        return false;
    }

    Com_Printf(
        0,
        "[VR] Head-tracked cube and floor-grid resources created.\n");

    return true;
}


struct VrHeadVector
{
    float x;
    float y;
    float z;
};

XrQuaternionf VR_NormalizeQuaternion(
    const XrQuaternionf& quaternion)
{
    const float lengthSquared =
        quaternion.x * quaternion.x +
        quaternion.y * quaternion.y +
        quaternion.z * quaternion.z +
        quaternion.w * quaternion.w;

    if (lengthSquared <= 0.000001f)
    {
        return {
            0.0f,
            0.0f,
            0.0f,
            1.0f,
        };
    }

    const float inverseLength =
        1.0f / std::sqrt(lengthSquared);

    return {
        quaternion.x * inverseLength,
        quaternion.y * inverseLength,
        quaternion.z * inverseLength,
        quaternion.w * inverseLength,
    };
}

XrQuaternionf VR_ConjugateQuaternion(
    const XrQuaternionf& quaternion)
{
    return {
        -quaternion.x,
        -quaternion.y,
        -quaternion.z,
        quaternion.w,
    };
}

XrQuaternionf VR_MultiplyQuaternion(
    const XrQuaternionf& left,
    const XrQuaternionf& right)
{
    XrQuaternionf result = {};

    result.x =
        left.w * right.x +
        left.x * right.w +
        left.y * right.z -
        left.z * right.y;

    result.y =
        left.w * right.y -
        left.x * right.z +
        left.y * right.w +
        left.z * right.x;

    result.z =
        left.w * right.z +
        left.x * right.y -
        left.y * right.x +
        left.z * right.w;

    result.w =
        left.w * right.w -
        left.x * right.x -
        left.y * right.y -
        left.z * right.z;

    return VR_NormalizeQuaternion(result);
}

VrHeadVector VR_CrossHeadVector(
    const VrHeadVector& left,
    const VrHeadVector& right)
{
    return {
        left.y * right.z -
            left.z * right.y,

        left.z * right.x -
            left.x * right.z,

        left.x * right.y -
            left.y * right.x,
    };
}

VrHeadVector VR_RotateHeadVector(
    const XrQuaternionf& orientation,
    const VrHeadVector& vector)
{
    const VrHeadVector quaternionVector = {
        orientation.x,
        orientation.y,
        orientation.z,
    };

    VrHeadVector twiceCross =
        VR_CrossHeadVector(
            quaternionVector,
            vector);

    twiceCross.x *= 2.0f;
    twiceCross.y *= 2.0f;
    twiceCross.z *= 2.0f;

    const VrHeadVector secondCross =
        VR_CrossHeadVector(
            quaternionVector,
            twiceCross);

    return {
        vector.x +
            orientation.w * twiceCross.x +
            secondCross.x,

        vector.y +
            orientation.w * twiceCross.y +
            secondCross.y,

        vector.z +
            orientation.w * twiceCross.z +
            secondCross.z,
    };
}

VrHeadVector VR_OpenXrVectorToCod(
    const VrHeadVector& vector)
{
    // OpenXR: +X right, +Y up, -Z forward.
    // CoD camera-local basis: +X forward, +Y left, +Z up.
    return {
        -vector.z,
        -vector.x,
        vector.y,
    };
}

void VR_ResetHeadOrientation()
{
    std::lock_guard<std::mutex> lock(
        g_vrHeadOrientationMutex);

    g_vrHeadOrientationAxis[0][0] = 1.0f;
    g_vrHeadOrientationAxis[0][1] = 0.0f;
    g_vrHeadOrientationAxis[0][2] = 0.0f;

    g_vrHeadOrientationAxis[1][0] = 0.0f;
    g_vrHeadOrientationAxis[1][1] = 1.0f;
    g_vrHeadOrientationAxis[1][2] = 0.0f;

    g_vrHeadOrientationAxis[2][0] = 0.0f;
    g_vrHeadOrientationAxis[2][1] = 0.0f;
    g_vrHeadOrientationAxis[2][2] = 1.0f;

    g_vrHeadBaseOrientation = {
        0.0f,
        0.0f,
        0.0f,
        1.0f,
    };

    g_vrHeadBaseOrientationValid = false;
    g_vrHeadOrientationValid = false;
    g_vrLoggedFirstHeadPose = false;
    g_vrLoggedFirstCameraApply = false;
}

void VR_PublishHeadOrientation(
    const XrQuaternionf& currentOrientation)
{
    const XrQuaternionf normalizedCurrent =
        VR_NormalizeQuaternion(
            currentOrientation);

    std::lock_guard<std::mutex> lock(
        g_vrHeadOrientationMutex);

    if (!g_vrHeadBaseOrientationValid)
    {
        g_vrHeadBaseOrientation =
            normalizedCurrent;

        g_vrHeadBaseOrientationValid = true;
    }

    const XrQuaternionf relativeOrientation =
        VR_MultiplyQuaternion(
            VR_ConjugateQuaternion(
                g_vrHeadBaseOrientation),
            normalizedCurrent);

    const VrHeadVector forwardOpenXr =
        VR_RotateHeadVector(
            relativeOrientation,
            {0.0f, 0.0f, -1.0f});

    const VrHeadVector leftOpenXr =
        VR_RotateHeadVector(
            relativeOrientation,
            {-1.0f, 0.0f, 0.0f});

    const VrHeadVector upOpenXr =
        VR_RotateHeadVector(
            relativeOrientation,
            {0.0f, 1.0f, 0.0f});

    const VrHeadVector forwardCod =
        VR_OpenXrVectorToCod(
            forwardOpenXr);

    const VrHeadVector leftCod =
        VR_OpenXrVectorToCod(
            leftOpenXr);

    const VrHeadVector upCod =
        VR_OpenXrVectorToCod(
            upOpenXr);

    g_vrHeadOrientationAxis[0][0] =
        forwardCod.x;
    g_vrHeadOrientationAxis[0][1] =
        forwardCod.y;
    g_vrHeadOrientationAxis[0][2] =
        forwardCod.z;

    g_vrHeadOrientationAxis[1][0] =
        leftCod.x;
    g_vrHeadOrientationAxis[1][1] =
        leftCod.y;
    g_vrHeadOrientationAxis[1][2] =
        leftCod.z;

    g_vrHeadOrientationAxis[2][0] =
        upCod.x;
    g_vrHeadOrientationAxis[2][1] =
        upCod.y;
    g_vrHeadOrientationAxis[2][2] =
        upCod.z;

    // Publish center-head translation in the same protected snapshot as
        // the headset orientation. Averaging the two eye poses removes the
        // per-eye IPD offset and yields the physical head center.
        const XrVector3f centerHeadPosition = {
            (g_vrViews[0].pose.position.x +
             g_vrViews[1].pose.position.x) * 0.5f,
            (g_vrViews[0].pose.position.y +
             g_vrViews[1].pose.position.y) * 0.5f,
            (g_vrViews[0].pose.position.z +
             g_vrViews[1].pose.position.z) * 0.5f};

        if (!g_vrHeadPositionOriginValid)
        {
            g_vrHeadPositionOrigin =
                centerHeadPosition;

            g_vrHeadPositionOriginValid = true;
        }

        const float openXrDeltaX =
            centerHeadPosition.x -
            g_vrHeadPositionOrigin.x;

        const float openXrDeltaY =
            centerHeadPosition.y -
            g_vrHeadPositionOrigin.y;

        const float openXrDeltaZ =
            centerHeadPosition.z -
            g_vrHeadPositionOrigin.z;

        // OpenXR: +X right, +Y up, -Z forward.
        // CoD camera-local basis: +X forward, +Y left, +Z up.
        g_vrHeadPositionLocal[0] =
            -openXrDeltaZ *
            kVrGameUnitsPerMeter;

        g_vrHeadPositionLocal[1] =
            -openXrDeltaX *
            kVrGameUnitsPerMeter;

        g_vrHeadPositionLocal[2] =
            openXrDeltaY *
            kVrGameUnitsPerMeter;

        g_vrHeadPositionValid = true;

        if (g_vrViews.size() >= kVrStereoEyeCount)
        {
            const float eyeDeltaX =
                g_vrViews[0].pose.position.x -
                g_vrViews[1].pose.position.x;

            const float eyeDeltaY =
                g_vrViews[0].pose.position.y -
                g_vrViews[1].pose.position.y;

            const float eyeDeltaZ =
                g_vrViews[0].pose.position.z -
                g_vrViews[1].pose.position.z;

            const float detectedHalfIpdGameUnits =
                0.5f *
                std::sqrt(
                    eyeDeltaX * eyeDeltaX +
                    eyeDeltaY * eyeDeltaY +
                    eyeDeltaZ * eyeDeltaZ) *
                kVrGameUnitsPerMeter;

            if (detectedHalfIpdGameUnits > 0.1f &&
                detectedHalfIpdGameUnits < 3.0f)
            {
                g_vrHalfIpdGameUnits =
                    detectedHalfIpdGameUnits;
            }
        }

        g_vrHeadOrientationValid = true;

    if (!g_vrLoggedFirstHeadPose)
    {
        Com_Printf(
            0,
            "[VR] Recentered and published first "
            "OpenXR headset orientation.\n");

        g_vrLoggedFirstHeadPose = true;
    }
}

bool VR_SelectEnvironmentBlendMode()
{
    uint32_t blendModeCount = 0;

    XrResult result =
        xrEnumerateEnvironmentBlendModes(
            g_vrInstance,
            g_vrSystemId,
            kViewConfiguration,
            0,
            &blendModeCount,
            nullptr);

    if (XR_FAILED(result) || blendModeCount == 0)
    {
        VR_LogXrFailure(
            "xrEnumerateEnvironmentBlendModes(count)",
            result);

        return false;
    }

    std::vector<XrEnvironmentBlendMode> blendModes(
        blendModeCount);

    result =
        xrEnumerateEnvironmentBlendModes(
            g_vrInstance,
            g_vrSystemId,
            kViewConfiguration,
            blendModeCount,
            &blendModeCount,
            blendModes.data());

    if (XR_FAILED(result))
    {
        VR_LogXrFailure(
            "xrEnumerateEnvironmentBlendModes(list)",
            result);

        return false;
    }

    const auto opaque =
        std::find(
            blendModes.begin(),
            blendModes.end(),
            XR_ENVIRONMENT_BLEND_MODE_OPAQUE);

    g_vrBlendMode =
        opaque != blendModes.end()
            ? XR_ENVIRONMENT_BLEND_MODE_OPAQUE
            : blendModes.front();

    return true;
}



XrQuaternionf VR_ControllerConjugateQuaternion(
    const XrQuaternionf& quaternion)
{
    return {
        -quaternion.x,
        -quaternion.y,
        -quaternion.z,
        quaternion.w,
    };
}

XrVector3f VR_ControllerSubtract(
    const XrVector3f& left,
    const XrVector3f& right)
{
    return {
        left.x - right.x,
        left.y - right.y,
        left.z - right.z,
    };
}

XrVector3f VR_ControllerAddScaled(
    const XrVector3f& origin,
    const VrHeadVector& direction,
    const float scale)
{
    return {
        origin.x + direction.x * scale,
        origin.y + direction.y * scale,
        origin.z + direction.z * scale,
    };
}

bool VR_ProjectAppSpacePointToEye(
    const XrVector3f& point,
    const XrView& eyeView,
    float* ndcX,
    float* ndcY)
{
    if (ndcX == nullptr ||
        ndcY == nullptr)
    {
        return false;
    }

    const XrVector3f relativePosition =
        VR_ControllerSubtract(
            point,
            eyeView.pose.position);

    const VrHeadVector relativeVector = {
        relativePosition.x,
        relativePosition.y,
        relativePosition.z,
    };

    const VrHeadVector eyeLocal =
        VR_RotateHeadVector(
            VR_ControllerConjugateQuaternion(
                eyeView.pose.orientation),
            relativeVector);

    const float forwardDepth =
        -eyeLocal.z;

    if (forwardDepth <= 0.03f)
    {
        return false;
    }

    const float tanLeft =
        std::tan(eyeView.fov.angleLeft);

    const float tanRight =
        std::tan(eyeView.fov.angleRight);

    const float tanDown =
        std::tan(eyeView.fov.angleDown);

    const float tanUp =
        std::tan(eyeView.fov.angleUp);

    const float horizontalRange =
        tanRight - tanLeft;

    const float verticalRange =
        tanUp - tanDown;

    if (horizontalRange <= 0.0f ||
        verticalRange <= 0.0f)
    {
        return false;
    }

    const float projectedX =
        eyeLocal.x / forwardDepth;

    const float projectedY =
        eyeLocal.y / forwardDepth;

    *ndcX =
        2.0f *
            (projectedX - tanLeft) /
            horizontalRange -
        1.0f;

    *ndcY =
        2.0f *
            (projectedY - tanDown) /
            verticalRange -
        1.0f;

    return
        *ndcX > -1.5f &&
        *ndcX < 1.5f &&
        *ndcY > -1.5f &&
        *ndcY < 1.5f;
}

void VR_AppendControllerProxyVertex(
    std::array<
        VrControllerProxyVertex,
        kVrMaximumControllerProxyVertices>& vertices,
    std::uint32_t* vertexCount,
    const float x,
    const float y,
    const float red,
    const float green,
    const float blue,
    const float alpha)
{
    if (vertexCount == nullptr ||
        *vertexCount >= vertices.size())
    {
        return;
    }

    VrControllerProxyVertex& vertex =
        vertices[*vertexCount];

    vertex.position[0] = x;
    vertex.position[1] = y;
    vertex.position[2] = 0.0f;
    vertex.position[3] = 1.0f;

    vertex.color[0] = red;
    vertex.color[1] = green;
    vertex.color[2] = blue;
    vertex.color[3] = alpha;

    ++(*vertexCount);
}

void VR_AppendControllerProxyTriangle(
    std::array<
        VrControllerProxyVertex,
        kVrMaximumControllerProxyVertices>& vertices,
    std::uint32_t* vertexCount,
    const float x0,
    const float y0,
    const float x1,
    const float y1,
    const float x2,
    const float y2,
    const float red,
    const float green,
    const float blue)
{
    VR_AppendControllerProxyVertex(
        vertices,
        vertexCount,
        x0,
        y0,
        red,
        green,
        blue,
        1.0f);

    VR_AppendControllerProxyVertex(
        vertices,
        vertexCount,
        x1,
        y1,
        red,
        green,
        blue,
        1.0f);

    VR_AppendControllerProxyVertex(
        vertices,
        vertexCount,
        x2,
        y2,
        red,
        green,
        blue,
        1.0f);
}

void VR_AppendControllerProxyDiamond(
    std::array<
        VrControllerProxyVertex,
        kVrMaximumControllerProxyVertices>& vertices,
    std::uint32_t* vertexCount,
    const float centerX,
    const float centerY,
    const float halfWidth,
    const float halfHeight,
    const float red,
    const float green,
    const float blue)
{
    VR_AppendControllerProxyTriangle(
        vertices,
        vertexCount,
        centerX,
        centerY + halfHeight,
        centerX - halfWidth,
        centerY,
        centerX,
        centerY - halfHeight,
        red,
        green,
        blue);

    VR_AppendControllerProxyTriangle(
        vertices,
        vertexCount,
        centerX,
        centerY + halfHeight,
        centerX,
        centerY - halfHeight,
        centerX + halfWidth,
        centerY,
        red,
        green,
        blue);
}

void VR_AppendControllerProxyRay(
    std::array<
        VrControllerProxyVertex,
        kVrMaximumControllerProxyVertices>& vertices,
    std::uint32_t* vertexCount,
    const float startX,
    const float startY,
    const float endX,
    const float endY,
    const float halfThickness,
    const float red,
    const float green,
    const float blue)
{
    const float deltaX = endX - startX;
    const float deltaY = endY - startY;

    const float length =
        std::sqrt(
            deltaX * deltaX +
            deltaY * deltaY);

    if (length <= 0.0001f)
    {
        return;
    }

    const float normalX =
        -deltaY / length *
        halfThickness;

    const float normalY =
        deltaX / length *
        halfThickness;

    const float startLeftX = startX + normalX;
    const float startLeftY = startY + normalY;
    const float startRightX = startX - normalX;
    const float startRightY = startY - normalY;
    const float endLeftX = endX + normalX;
    const float endLeftY = endY + normalY;
    const float endRightX = endX - normalX;
    const float endRightY = endY - normalY;

    VR_AppendControllerProxyTriangle(
        vertices,
        vertexCount,
        startLeftX,
        startLeftY,
        endLeftX,
        endLeftY,
        endRightX,
        endRightY,
        red,
        green,
        blue);

    VR_AppendControllerProxyTriangle(
        vertices,
        vertexCount,
        startLeftX,
        startLeftY,
        endRightX,
        endRightY,
        startRightX,
        startRightY,
        red,
        green,
        blue);
}

bool VR_CreateControllerProxyResources()
{
    if (g_vrD3dDevice == nullptr)
    {
        return false;
    }

    if (g_vrControllerProxyResourcesReady)
    {
        return true;
    }

    static const char* vertexShaderSource = R"(
struct ControllerProxyVertex
{
    float4 position : POSITION;
    float4 color : COLOR0;
};

struct ControllerProxyPixel
{
    float4 position : SV_POSITION;
    float4 color : COLOR0;
};

ControllerProxyPixel VSMain(
    ControllerProxyVertex input)
{
    ControllerProxyPixel output;
    output.position = input.position;
    output.color = input.color;
    return output;
}
)";

    static const char* pixelShaderSource = R"(
struct ControllerProxyPixel
{
    float4 position : SV_POSITION;
    float4 color : COLOR0;
};

float4 PSMain(
    ControllerProxyPixel input) : SV_TARGET
{
    return input.color;
}
)";

    ComPtr<ID3DBlob> vertexShaderBlob;
    ComPtr<ID3DBlob> pixelShaderBlob;
    ComPtr<ID3DBlob> errorBlob;

    HRESULT hr =
        D3DCompile(
            vertexShaderSource,
            std::strlen(vertexShaderSource),
            "controller_proxy_vs",
            nullptr,
            nullptr,
            "VSMain",
            "vs_4_0",
            D3DCOMPILE_ENABLE_STRICTNESS,
            0,
            vertexShaderBlob.GetAddressOf(),
            errorBlob.GetAddressOf());

    if (FAILED(hr))
    {
        VR_LogHrFailure(
            "D3DCompile(controller proxy VS)",
            hr);

        return false;
    }

    errorBlob.Reset();

    hr =
        D3DCompile(
            pixelShaderSource,
            std::strlen(pixelShaderSource),
            "controller_proxy_ps",
            nullptr,
            nullptr,
            "PSMain",
            "ps_4_0",
            D3DCOMPILE_ENABLE_STRICTNESS,
            0,
            pixelShaderBlob.GetAddressOf(),
            errorBlob.GetAddressOf());

    if (FAILED(hr))
    {
        VR_LogHrFailure(
            "D3DCompile(controller proxy PS)",
            hr);

        return false;
    }

    hr =
        g_vrD3dDevice->CreateVertexShader(
            vertexShaderBlob->GetBufferPointer(),
            vertexShaderBlob->GetBufferSize(),
            nullptr,
            g_vrControllerProxyVertexShader
                .GetAddressOf());

    if (FAILED(hr))
    {
        VR_LogHrFailure(
            "CreateVertexShader(controller proxy)",
            hr);

        return false;
    }

    hr =
        g_vrD3dDevice->CreatePixelShader(
            pixelShaderBlob->GetBufferPointer(),
            pixelShaderBlob->GetBufferSize(),
            nullptr,
            g_vrControllerProxyPixelShader
                .GetAddressOf());

    if (FAILED(hr))
    {
        VR_LogHrFailure(
            "CreatePixelShader(controller proxy)",
            hr);

        return false;
    }

    const D3D11_INPUT_ELEMENT_DESC inputElements[] = {
        {
            "POSITION",
            0,
            DXGI_FORMAT_R32G32B32A32_FLOAT,
            0,
            0,
            D3D11_INPUT_PER_VERTEX_DATA,
            0,
        },
        {
            "COLOR",
            0,
            DXGI_FORMAT_R32G32B32A32_FLOAT,
            0,
            16,
            D3D11_INPUT_PER_VERTEX_DATA,
            0,
        },
    };

    hr =
        g_vrD3dDevice->CreateInputLayout(
            inputElements,
            static_cast<UINT>(
                sizeof(inputElements) /
                sizeof(inputElements[0])),
            vertexShaderBlob->GetBufferPointer(),
            vertexShaderBlob->GetBufferSize(),
            g_vrControllerProxyInputLayout
                .GetAddressOf());

    if (FAILED(hr))
    {
        VR_LogHrFailure(
            "CreateInputLayout(controller proxy)",
            hr);

        return false;
    }

    D3D11_BUFFER_DESC bufferDescription = {};
    bufferDescription.ByteWidth =
        sizeof(VrControllerProxyVertex) *
        kVrMaximumControllerProxyVertices;
    bufferDescription.Usage =
        D3D11_USAGE_DYNAMIC;
    bufferDescription.BindFlags =
        D3D11_BIND_VERTEX_BUFFER;
    bufferDescription.CPUAccessFlags =
        D3D11_CPU_ACCESS_WRITE;

    hr =
        g_vrD3dDevice->CreateBuffer(
            &bufferDescription,
            nullptr,
            g_vrControllerProxyVertexBuffer
                .GetAddressOf());

    if (FAILED(hr))
    {
        VR_LogHrFailure(
            "CreateBuffer(controller proxy)",
            hr);

        return false;
    }

    g_vrControllerProxyResourcesReady = true;

    Com_Printf(
        0,
        "[VR] Created visible OpenXR controller "
        "proxy renderer.\n");

    return true;
}

void VR_RenderControllerProxies(
    const std::uint32_t eyeIndex,
    const int32_t viewportWidth,
    const int32_t viewportHeight)
{
    if (!g_vrControllerProxyResourcesReady ||
        eyeIndex >= g_vrViews.size() ||
        viewportWidth <= 0 ||
        viewportHeight <= 0)
    {
        return;
    }

    std::array<
        VrControllerProxyVertex,
        kVrMaximumControllerProxyVertices>
        vertices = {};

    std::uint32_t vertexCount = 0u;

    const float markerHalfWidth =
        9.0f /
        static_cast<float>(viewportWidth);

    const float markerHalfHeight =
        9.0f /
        static_cast<float>(viewportHeight);

    const float rayHalfThickness =
        2.5f /
        static_cast<float>(viewportHeight);

    for (std::uint32_t handIndex = 0u;
         handIndex < kVrControllerCount;
         ++handIndex)
    {
        const VrControllerRenderPose& controller =
            g_vrControllerRenderPoses[handIndex];

        const float red =
            handIndex == VR_CONTROLLER_LEFT
                ? 0.1f
                : 1.0f;

        const float green =
            handIndex == VR_CONTROLLER_LEFT
                ? 0.85f
                : 0.45f;

        const float blue =
            handIndex == VR_CONTROLLER_LEFT
                ? 1.0f
                : 0.05f;

        if (controller.gripValid)
        {
            float gripX = 0.0f;
            float gripY = 0.0f;

            if (VR_ProjectAppSpacePointToEye(
                    controller.gripPose.position,
                    g_vrViews[eyeIndex],
                    &gripX,
                    &gripY))
            {
                VR_AppendControllerProxyDiamond(
                    vertices,
                    &vertexCount,
                    gripX,
                    gripY,
                    markerHalfWidth,
                    markerHalfHeight,
                    red,
                    green,
                    blue);
            }
        }

        if (controller.aimValid)
        {
            const VrHeadVector aimForward =
                VR_RotateHeadVector(
                    controller.aimPose.orientation,
                    {0.0f, 0.0f, -1.0f});

            const XrVector3f aimEnd =
                VR_ControllerAddScaled(
                    controller.aimPose.position,
                    aimForward,
                    0.75f);

            float aimStartX = 0.0f;
            float aimStartY = 0.0f;
            float aimEndX = 0.0f;
            float aimEndY = 0.0f;

            const bool startVisible =
                VR_ProjectAppSpacePointToEye(
                    controller.aimPose.position,
                    g_vrViews[eyeIndex],
                    &aimStartX,
                    &aimStartY);

            const bool endVisible =
                VR_ProjectAppSpacePointToEye(
                    aimEnd,
                    g_vrViews[eyeIndex],
                    &aimEndX,
                    &aimEndY);

            if (startVisible && endVisible)
            {
                VR_AppendControllerProxyRay(
                    vertices,
                    &vertexCount,
                    aimStartX,
                    aimStartY,
                    aimEndX,
                    aimEndY,
                    rayHalfThickness,
                    red,
                    green,
                    blue);
            }
        }
    }

    if (vertexCount == 0u)
    {
        return;
    }

    D3D11_MAPPED_SUBRESOURCE mapped = {};

    const HRESULT mapResult =
        g_vrD3dContext->Map(
            g_vrControllerProxyVertexBuffer.Get(),
            0,
            D3D11_MAP_WRITE_DISCARD,
            0,
            &mapped);

    if (FAILED(mapResult) ||
        mapped.pData == nullptr)
    {
        return;
    }

    std::memcpy(
        mapped.pData,
        vertices.data(),
        sizeof(VrControllerProxyVertex) *
            vertexCount);

    g_vrD3dContext->Unmap(
        g_vrControllerProxyVertexBuffer.Get(),
        0);

    const UINT stride =
        sizeof(VrControllerProxyVertex);

    const UINT offset = 0u;

    ID3D11Buffer* vertexBuffer =
        g_vrControllerProxyVertexBuffer.Get();

    g_vrD3dContext->IASetInputLayout(
        g_vrControllerProxyInputLayout.Get());

    g_vrD3dContext->IASetVertexBuffers(
        0,
        1,
        &vertexBuffer,
        &stride,
        &offset);

    g_vrD3dContext->IASetIndexBuffer(
        nullptr,
        DXGI_FORMAT_UNKNOWN,
        0);

    g_vrD3dContext->IASetPrimitiveTopology(
        D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    g_vrD3dContext->VSSetShader(
        g_vrControllerProxyVertexShader.Get(),
        nullptr,
        0);

    g_vrD3dContext->PSSetShader(
        g_vrControllerProxyPixelShader.Get(),
        nullptr,
        0);

    g_vrD3dContext->RSSetState(nullptr);

    g_vrD3dContext->OMSetDepthStencilState(
        nullptr,
        0);

    g_vrD3dContext->OMSetBlendState(
        nullptr,
        nullptr,
        0xFFFFFFFFu);

    g_vrD3dContext->Draw(
        vertexCount,
        0);

    if (!g_vrLoggedFirstControllerProxyDraw)
    {
        Com_Printf(
            0,
            "[VR] Rendered first visible OpenXR "
            "controller proxies.\n");

        g_vrLoggedFirstControllerProxyDraw = true;
    }
}

const char* VR_ControllerHandName(
    const std::uint32_t handIndex)
{
    return
        handIndex == VR_CONTROLLER_LEFT
            ? "left"
            : "right";
}

bool VR_ControllerStringToPath(
    const char* pathString,
    XrPath* path)
{
    if (pathString == nullptr ||
        path == nullptr)
    {
        return false;
    }

    const XrResult result =
        xrStringToPath(
            g_vrInstance,
            pathString,
            path);

    if (XR_FAILED(result))
    {
        VR_LogXrFailure(
            "xrStringToPath",
            result);

        return false;
    }

    return true;
}

void VR_DestroyControllerInput()
{
    for (XrSpace& space :
         g_vrControllerAimSpaces)
    {
        if (space != XR_NULL_HANDLE)
        {
            xrDestroySpace(space);
            space = XR_NULL_HANDLE;
        }
    }

    for (XrSpace& space :
         g_vrControllerGripSpaces)
    {
        if (space != XR_NULL_HANDLE)
        {
            xrDestroySpace(space);
            space = XR_NULL_HANDLE;
        }
    }

    if (g_vrSqueezeValueAction != XR_NULL_HANDLE)
    {
        xrDestroyAction(g_vrSqueezeValueAction);
        g_vrSqueezeValueAction = XR_NULL_HANDLE;
    }

    if (g_vrTriggerValueAction != XR_NULL_HANDLE)
    {
        xrDestroyAction(g_vrTriggerValueAction);
        g_vrTriggerValueAction = XR_NULL_HANDLE;
    }

    if (g_vrAimPoseAction != XR_NULL_HANDLE)
    {
        xrDestroyAction(g_vrAimPoseAction);
        g_vrAimPoseAction = XR_NULL_HANDLE;
    }

    if (g_vrGripPoseAction != XR_NULL_HANDLE)
    {
        xrDestroyAction(g_vrGripPoseAction);
        g_vrGripPoseAction = XR_NULL_HANDLE;
    }

    if (g_vrControllerActionSet != XR_NULL_HANDLE)
    {
        xrDestroyActionSet(g_vrControllerActionSet);
        g_vrControllerActionSet = XR_NULL_HANDLE;
    }

    g_vrControllerHandPaths = {
        XR_NULL_PATH,
        XR_NULL_PATH,
    };

    g_vrControllerActionsCreated = false;
    g_vrControllerActionsAttached = false;
    g_vrControllerSpacesCreated = false;

    g_vrLoggedFirstGripPose.fill(false);
    g_vrLoggedFirstAimPose.fill(false);
    g_vrControllerTriggerPressed.fill(false);
    g_vrControllerSqueezePressed.fill(false);
    g_vrControllerDiagnosticFrame = 0u;
    g_vrControllerRenderPoses = {};

    {
        std::lock_guard<std::mutex> lock(
            g_vrWeaponControllerPoseMutex);

        g_vrRightControllerWeaponPoseValid = false;
        g_vrRightControllerWeaponBaseValid = false;
        g_vrRightControllerFinalWeaponAimValid = false;
        g_vrRightControllerFinalWeaponMuzzleValid = false;
        g_vrRightControllerFinalWeaponMuzzleBlocked = false;
        g_vrRightControllerAttackPressed = false;

        g_vrRightControllerFinalWeaponForward[0] = 1.0f;
        g_vrRightControllerFinalWeaponForward[1] = 0.0f;
        g_vrRightControllerFinalWeaponForward[2] = 0.0f;

        memset(
            g_vrRightControllerWeaponPosition,
            0,
            sizeof(g_vrRightControllerWeaponPosition));

        memset(
            g_vrRightControllerWeaponAxis,
            0,
            sizeof(g_vrRightControllerWeaponAxis));

        memset(
            g_vrRightControllerFinalWeaponMuzzleWorld,
            0,
            sizeof(g_vrRightControllerFinalWeaponMuzzleWorld));

        memset(
            g_vrRightControllerWeaponBasePosition,
            0,
            sizeof(g_vrRightControllerWeaponBasePosition));

        memset(
            g_vrRightControllerWeaponBaseAxis,
            0,
            sizeof(g_vrRightControllerWeaponBaseAxis));
    }

    g_vrLoggedRightControllerWeaponCalibration = false;
    g_vrLoggedRightControllerWeaponApply = false;
    g_vrLoggedRightControllerUsercmdAim = false;
    g_vrLoggedRightControllerAttackInjection = false;
}

bool VR_CreateControllerAction(
    const XrActionType actionType,
    const char* actionName,
    const char* localizedName,
    XrAction* action)
{
    if (actionName == nullptr ||
        localizedName == nullptr ||
        action == nullptr)
    {
        return false;
    }

    XrActionCreateInfo createInfo{
        XR_TYPE_ACTION_CREATE_INFO
    };

    createInfo.actionType = actionType;

    std::snprintf(
        createInfo.actionName,
        XR_MAX_ACTION_NAME_SIZE,
        "%s",
        actionName);

    std::snprintf(
        createInfo.localizedActionName,
        XR_MAX_LOCALIZED_ACTION_NAME_SIZE,
        "%s",
        localizedName);

    createInfo.countSubactionPaths =
        static_cast<std::uint32_t>(
            g_vrControllerHandPaths.size());

    createInfo.subactionPaths =
        g_vrControllerHandPaths.data();

    const XrResult result =
        xrCreateAction(
            g_vrControllerActionSet,
            &createInfo,
            action);

    if (XR_FAILED(result))
    {
        VR_LogXrFailure(
            "xrCreateAction",
            result);

        return false;
    }

    return true;
}

bool VR_SuggestControllerBindings()
{
    XrPath touchProfile = XR_NULL_PATH;

    if (!VR_ControllerStringToPath(
            "/interaction_profiles/oculus/"
            "touch_controller",
            &touchProfile))
    {
        return false;
    }

    std::array<XrPath, 8> touchBindingPaths = {};

    const std::array<const char*, 8>
        touchBindingStrings = {
            "/user/hand/left/input/grip/pose",
            "/user/hand/right/input/grip/pose",
            "/user/hand/left/input/aim/pose",
            "/user/hand/right/input/aim/pose",
            "/user/hand/left/input/trigger/value",
            "/user/hand/right/input/trigger/value",
            "/user/hand/left/input/squeeze/value",
            "/user/hand/right/input/squeeze/value",
        };

    for (std::uint32_t bindingIndex = 0u;
         bindingIndex < touchBindingPaths.size();
         ++bindingIndex)
    {
        if (!VR_ControllerStringToPath(
                touchBindingStrings[bindingIndex],
                &touchBindingPaths[bindingIndex]))
        {
            return false;
        }
    }

    const std::array<XrActionSuggestedBinding, 8>
        touchBindings = {{
            {
                g_vrGripPoseAction,
                touchBindingPaths[0],
            },
            {
                g_vrGripPoseAction,
                touchBindingPaths[1],
            },
            {
                g_vrAimPoseAction,
                touchBindingPaths[2],
            },
            {
                g_vrAimPoseAction,
                touchBindingPaths[3],
            },
            {
                g_vrTriggerValueAction,
                touchBindingPaths[4],
            },
            {
                g_vrTriggerValueAction,
                touchBindingPaths[5],
            },
            {
                g_vrSqueezeValueAction,
                touchBindingPaths[6],
            },
            {
                g_vrSqueezeValueAction,
                touchBindingPaths[7],
            },
        }};

    XrInteractionProfileSuggestedBinding
        touchSuggestion{
            XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING
        };

    touchSuggestion.interactionProfile =
        touchProfile;

    touchSuggestion.countSuggestedBindings =
        static_cast<std::uint32_t>(
            touchBindings.size());

    touchSuggestion.suggestedBindings =
        touchBindings.data();

    XrResult result =
        xrSuggestInteractionProfileBindings(
            g_vrInstance,
            &touchSuggestion);

    if (XR_FAILED(result))
    {
        VR_LogXrFailure(
            "xrSuggestInteractionProfileBindings("
            "Oculus Touch)",
            result);

        return false;
    }

    XrPath simpleProfile = XR_NULL_PATH;

    if (VR_ControllerStringToPath(
            "/interaction_profiles/khr/"
            "simple_controller",
            &simpleProfile))
    {
        std::array<XrPath, 4> simpleBindingPaths = {};

        const std::array<const char*, 4>
            simpleBindingStrings = {
                "/user/hand/left/input/grip/pose",
                "/user/hand/right/input/grip/pose",
                "/user/hand/left/input/aim/pose",
                "/user/hand/right/input/aim/pose",
            };

        bool simplePathsValid = true;

        for (std::uint32_t bindingIndex = 0u;
             bindingIndex < simpleBindingPaths.size();
             ++bindingIndex)
        {
            simplePathsValid =
                simplePathsValid &&
                VR_ControllerStringToPath(
                    simpleBindingStrings[bindingIndex],
                    &simpleBindingPaths[bindingIndex]);
        }

        if (simplePathsValid)
        {
            const std::array<
                XrActionSuggestedBinding,
                4>
                simpleBindings = {{
                    {
                        g_vrGripPoseAction,
                        simpleBindingPaths[0],
                    },
                    {
                        g_vrGripPoseAction,
                        simpleBindingPaths[1],
                    },
                    {
                        g_vrAimPoseAction,
                        simpleBindingPaths[2],
                    },
                    {
                        g_vrAimPoseAction,
                        simpleBindingPaths[3],
                    },
                }};

            XrInteractionProfileSuggestedBinding
                simpleSuggestion{
                    XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING
                };

            simpleSuggestion.interactionProfile =
                simpleProfile;

            simpleSuggestion.countSuggestedBindings =
                static_cast<std::uint32_t>(
                    simpleBindings.size());

            simpleSuggestion.suggestedBindings =
                simpleBindings.data();

            result =
                xrSuggestInteractionProfileBindings(
                    g_vrInstance,
                    &simpleSuggestion);

            if (XR_FAILED(result))
            {
                Com_PrintWarning(
                    0,
                    "[VR] Simple-controller fallback "
                    "bindings were not accepted.\n");
            }
        }
    }

    Com_Printf(
        0,
        "[VR] Suggested Oculus Touch controller "
        "pose and analog bindings.\n");

    return true;
}

bool VR_CreateControllerActions()
{
    if (g_vrInstance == XR_NULL_HANDLE)
    {
        return false;
    }

    if (g_vrControllerActionsCreated)
    {
        return true;
    }

    if (!VR_ControllerStringToPath(
            "/user/hand/left",
            &g_vrControllerHandPaths[
                VR_CONTROLLER_LEFT]) ||
        !VR_ControllerStringToPath(
            "/user/hand/right",
            &g_vrControllerHandPaths[
                VR_CONTROLLER_RIGHT]))
    {
        VR_DestroyControllerInput();
        return false;
    }

    XrActionSetCreateInfo actionSetInfo{
        XR_TYPE_ACTION_SET_CREATE_INFO
    };

    std::snprintf(
        actionSetInfo.actionSetName,
        XR_MAX_ACTION_SET_NAME_SIZE,
        "%s",
        "gameplay");

    std::snprintf(
        actionSetInfo.localizedActionSetName,
        XR_MAX_LOCALIZED_ACTION_SET_NAME_SIZE,
        "%s",
        "KisakCOD Gameplay");

    actionSetInfo.priority = 0u;

    XrResult result =
        xrCreateActionSet(
            g_vrInstance,
            &actionSetInfo,
            &g_vrControllerActionSet);

    if (XR_FAILED(result))
    {
        VR_LogXrFailure(
            "xrCreateActionSet",
            result);

        VR_DestroyControllerInput();
        return false;
    }

    if (!VR_CreateControllerAction(
            XR_ACTION_TYPE_POSE_INPUT,
            "grip_pose",
            "Grip Pose",
            &g_vrGripPoseAction) ||
        !VR_CreateControllerAction(
            XR_ACTION_TYPE_POSE_INPUT,
            "aim_pose",
            "Aim Pose",
            &g_vrAimPoseAction) ||
        !VR_CreateControllerAction(
            XR_ACTION_TYPE_FLOAT_INPUT,
            "trigger_value",
            "Trigger Value",
            &g_vrTriggerValueAction) ||
        !VR_CreateControllerAction(
            XR_ACTION_TYPE_FLOAT_INPUT,
            "squeeze_value",
            "Squeeze Value",
            &g_vrSqueezeValueAction))
    {
        VR_DestroyControllerInput();
        return false;
    }

    if (!VR_SuggestControllerBindings())
    {
        VR_DestroyControllerInput();
        return false;
    }

    g_vrControllerActionsCreated = true;

    Com_Printf(
        0,
        "[VR] Created OpenXR controller action set.\n");

    return true;
}

bool VR_CreateControllerActionSpaces()
{
    for (std::uint32_t handIndex = 0u;
         handIndex < kVrControllerCount;
         ++handIndex)
    {
        XrActionSpaceCreateInfo gripSpaceInfo{
            XR_TYPE_ACTION_SPACE_CREATE_INFO
        };

        gripSpaceInfo.action =
            g_vrGripPoseAction;

        gripSpaceInfo.subactionPath =
            g_vrControllerHandPaths[handIndex];

        gripSpaceInfo.poseInActionSpace.orientation.w =
            1.0f;

        XrResult result =
            xrCreateActionSpace(
                g_vrSession,
                &gripSpaceInfo,
                &g_vrControllerGripSpaces[
                    handIndex]);

        if (XR_FAILED(result))
        {
            VR_LogXrFailure(
                "xrCreateActionSpace(grip)",
                result);

            return false;
        }

        XrActionSpaceCreateInfo aimSpaceInfo{
            XR_TYPE_ACTION_SPACE_CREATE_INFO
        };

        aimSpaceInfo.action =
            g_vrAimPoseAction;

        aimSpaceInfo.subactionPath =
            g_vrControllerHandPaths[handIndex];

        aimSpaceInfo.poseInActionSpace.orientation.w =
            1.0f;

        result =
            xrCreateActionSpace(
                g_vrSession,
                &aimSpaceInfo,
                &g_vrControllerAimSpaces[
                    handIndex]);

        if (XR_FAILED(result))
        {
            VR_LogXrFailure(
                "xrCreateActionSpace(aim)",
                result);

            return false;
        }
    }

    g_vrControllerSpacesCreated = true;
    return true;
}

bool VR_AttachControllerActions()
{
    if (!g_vrControllerActionsCreated ||
        g_vrSession == XR_NULL_HANDLE)
    {
        return false;
    }

    const XrActionSet actionSets[] = {
        g_vrControllerActionSet,
    };

    XrSessionActionSetsAttachInfo attachInfo{
        XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO
    };

    attachInfo.countActionSets = 1u;
    attachInfo.actionSets = actionSets;

    XrResult result =
        xrAttachSessionActionSets(
            g_vrSession,
            &attachInfo);

    if (XR_FAILED(result))
    {
        VR_LogXrFailure(
            "xrAttachSessionActionSets",
            result);

        return false;
    }

    g_vrControllerActionsAttached = true;

    if (!VR_CreateControllerActionSpaces())
    {
        return false;
    }

    Com_Printf(
        0,
        "[VR] OpenXR controller action set and "
        "pose spaces are ready.\n");

    return true;
}

bool VR_GetControllerPoseState(
    const XrAction action,
    const XrPath handPath,
    bool* active)
{
    if (active == nullptr)
    {
        return false;
    }

    XrActionStateGetInfo getInfo{
        XR_TYPE_ACTION_STATE_GET_INFO
    };

    getInfo.action = action;
    getInfo.subactionPath = handPath;

    XrActionStatePose state{
        XR_TYPE_ACTION_STATE_POSE
    };

    const XrResult result =
        xrGetActionStatePose(
            g_vrSession,
            &getInfo,
            &state);

    if (XR_FAILED(result))
    {
        VR_LogXrFailure(
            "xrGetActionStatePose",
            result);

        return false;
    }

    *active = state.isActive == XR_TRUE;
    return true;
}

bool VR_GetControllerFloatState(
    const XrAction action,
    const XrPath handPath,
    float* value,
    bool* active)
{
    if (value == nullptr ||
        active == nullptr)
    {
        return false;
    }

    XrActionStateGetInfo getInfo{
        XR_TYPE_ACTION_STATE_GET_INFO
    };

    getInfo.action = action;
    getInfo.subactionPath = handPath;

    XrActionStateFloat state{
        XR_TYPE_ACTION_STATE_FLOAT
    };

    const XrResult result =
        xrGetActionStateFloat(
            g_vrSession,
            &getInfo,
            &state);

    if (XR_FAILED(result))
    {
        VR_LogXrFailure(
            "xrGetActionStateFloat",
            result);

        return false;
    }

    *active = state.isActive == XR_TRUE;
    *value = *active ? state.currentState : 0.0f;

    return true;
}

bool VR_LocateControllerSpace(
    const XrSpace controllerSpace,
    const XrTime displayTime,
    XrSpaceLocation* location)
{
    if (controllerSpace == XR_NULL_HANDLE ||
        location == nullptr)
    {
        return false;
    }

    *location =
        XrSpaceLocation{
            XR_TYPE_SPACE_LOCATION
        };

    const XrResult result =
        xrLocateSpace(
            controllerSpace,
            g_vrAppSpace,
            displayTime,
            location);

    if (XR_FAILED(result))
    {
        VR_LogXrFailure(
            "xrLocateSpace(controller)",
            result);

        return false;
    }

    const XrSpaceLocationFlags requiredFlags =
        XR_SPACE_LOCATION_POSITION_VALID_BIT |
        XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;

    return
        (location->locationFlags & requiredFlags) ==
        requiredFlags;
}

void VR_LogControllerPoseSnapshot(
    const std::uint32_t handIndex,
    const XrSpaceLocation& aimLocation,
    const float triggerValue,
    const float squeezeValue)
{
    const VrHeadVector aimForward =
        VR_RotateHeadVector(
            aimLocation.pose.orientation,
            {0.0f, 0.0f, -1.0f});

    Com_Printf(
        0,
        "[VR] Controller %s aim snapshot: "
        "pos %.3f %.3f %.3f m, "
        "forward %.3f %.3f %.3f, "
        "trigger %.3f, squeeze %.3f.\n",
        VR_ControllerHandName(handIndex),
        aimLocation.pose.position.x,
        aimLocation.pose.position.y,
        aimLocation.pose.position.z,
        aimForward.x,
        aimForward.y,
        aimForward.z,
        triggerValue,
        squeezeValue);
}


void VR_PublishRightControllerWeaponPose(
    const XrPosef& controllerAimPose)
{
    if (g_vrViews.size() < kVrStereoEyeCount)
    {
        return;
    }

    const XrQuaternionf headOrientation =
        VR_NormalizeQuaternion(
            g_vrViews[0].pose.orientation);

    const XrQuaternionf inverseHeadOrientation =
        VR_ConjugateQuaternion(
            headOrientation);

    const XrVector3f headCenter = {
        (g_vrViews[0].pose.position.x +
         g_vrViews[1].pose.position.x) * 0.5f,
        (g_vrViews[0].pose.position.y +
         g_vrViews[1].pose.position.y) * 0.5f,
        (g_vrViews[0].pose.position.z +
         g_vrViews[1].pose.position.z) * 0.5f,
    };

    const VrHeadVector controllerOffsetOpenXr = {
        controllerAimPose.position.x -
            headCenter.x,
        controllerAimPose.position.y -
            headCenter.y,
        controllerAimPose.position.z -
            headCenter.z,
    };

    const VrHeadVector controllerOffsetHeadLocal =
        VR_RotateHeadVector(
            inverseHeadOrientation,
            controllerOffsetOpenXr);

    const VrHeadVector controllerPositionCod =
        VR_OpenXrVectorToCod(
            controllerOffsetHeadLocal);

    const XrQuaternionf controllerOrientation =
        VR_NormalizeQuaternion(
            controllerAimPose.orientation);

    const XrQuaternionf controllerRelativeToHead =
        VR_NormalizeQuaternion(
            VR_MultiplyQuaternion(
                inverseHeadOrientation,
                controllerOrientation));

    const VrHeadVector forwardCod =
        VR_OpenXrVectorToCod(
            VR_RotateHeadVector(
                controllerRelativeToHead,
                {0.0f, 0.0f, -1.0f}));

    const VrHeadVector leftCod =
        VR_OpenXrVectorToCod(
            VR_RotateHeadVector(
                controllerRelativeToHead,
                {-1.0f, 0.0f, 0.0f}));

    const VrHeadVector upCod =
        VR_OpenXrVectorToCod(
            VR_RotateHeadVector(
                controllerRelativeToHead,
                {0.0f, 1.0f, 0.0f}));

    std::lock_guard<std::mutex> lock(
        g_vrWeaponControllerPoseMutex);

    g_vrRightControllerWeaponPosition[0] =
        controllerPositionCod.x *
        kVrGameUnitsPerMeter;

    g_vrRightControllerWeaponPosition[1] =
        controllerPositionCod.y *
        kVrGameUnitsPerMeter;

    g_vrRightControllerWeaponPosition[2] =
        controllerPositionCod.z *
        kVrGameUnitsPerMeter;

    g_vrRightControllerWeaponAxis[0][0] =
        forwardCod.x;
    g_vrRightControllerWeaponAxis[0][1] =
        forwardCod.y;
    g_vrRightControllerWeaponAxis[0][2] =
        forwardCod.z;

    g_vrRightControllerWeaponAxis[1][0] =
        leftCod.x;
    g_vrRightControllerWeaponAxis[1][1] =
        leftCod.y;
    g_vrRightControllerWeaponAxis[1][2] =
        leftCod.z;

    g_vrRightControllerWeaponAxis[2][0] =
        upCod.x;
    g_vrRightControllerWeaponAxis[2][1] =
        upCod.y;
    g_vrRightControllerWeaponAxis[2][2] =
        upCod.z;

    g_vrRightControllerWeaponPoseValid = true;
}

void VR_UpdateControllerActions(
    const XrTime displayTime)
{
    if (!g_vrControllerActionsAttached ||
        !g_vrControllerSpacesCreated ||
        !g_vrSessionRunning)
    {
        return;
    }

    XrActiveActionSet activeActionSet = {};
    activeActionSet.actionSet =
        g_vrControllerActionSet;
    activeActionSet.subactionPath =
        XR_NULL_PATH;

    XrActionsSyncInfo syncInfo{
        XR_TYPE_ACTIONS_SYNC_INFO
    };

    syncInfo.countActiveActionSets = 1u;
    syncInfo.activeActionSets =
        &activeActionSet;

    const XrResult syncResult =
        xrSyncActions(
            g_vrSession,
            &syncInfo);

    if (XR_FAILED(syncResult))
    {
        VR_LogXrFailure(
            "xrSyncActions",
            syncResult);

        return;
    }

    ++g_vrControllerDiagnosticFrame;

    const bool logPeriodicSnapshot =
        (g_vrControllerDiagnosticFrame % 180u) ==
        0u;

    for (std::uint32_t handIndex = 0u;
         handIndex < kVrControllerCount;
         ++handIndex)
    {
        const XrPath handPath =
            g_vrControllerHandPaths[handIndex];

        bool gripActive = false;
        bool aimActive = false;

        VR_GetControllerPoseState(
            g_vrGripPoseAction,
            handPath,
            &gripActive);

        VR_GetControllerPoseState(
            g_vrAimPoseAction,
            handPath,
            &aimActive);

        XrSpaceLocation gripLocation{
            XR_TYPE_SPACE_LOCATION
        };

        XrSpaceLocation aimLocation{
            XR_TYPE_SPACE_LOCATION
        };

        const bool gripValid =
            gripActive &&
            VR_LocateControllerSpace(
                g_vrControllerGripSpaces[
                    handIndex],
                displayTime,
                &gripLocation);

        const bool aimValid =
            aimActive &&
            VR_LocateControllerSpace(
                g_vrControllerAimSpaces[
                    handIndex],
                displayTime,
                &aimLocation);

        VrControllerRenderPose& renderPose =
            g_vrControllerRenderPoses[handIndex];

        renderPose.gripValid = gripValid;
        renderPose.aimValid = aimValid;

        if (gripValid)
        {
            renderPose.gripPose =
                gripLocation.pose;
        }

        if (aimValid)
        {
            renderPose.aimPose =
                aimLocation.pose;

            if (handIndex == VR_CONTROLLER_RIGHT)
            {
                VR_PublishRightControllerWeaponPose(
                    aimLocation.pose);
            }
        }

        if (gripValid &&
            !g_vrLoggedFirstGripPose[handIndex])
        {
            Com_Printf(
                0,
                "[VR] Located first valid %s "
                "controller grip pose.\n",
                VR_ControllerHandName(handIndex));

            g_vrLoggedFirstGripPose[handIndex] =
                true;
        }

        if (aimValid &&
            !g_vrLoggedFirstAimPose[handIndex])
        {
            Com_Printf(
                0,
                "[VR] Located first valid %s "
                "controller aim pose.\n",
                VR_ControllerHandName(handIndex));

            g_vrLoggedFirstAimPose[handIndex] =
                true;
        }

        float triggerValue = 0.0f;
        float squeezeValue = 0.0f;
        bool triggerActive = false;
        bool squeezeActive = false;

        VR_GetControllerFloatState(
            g_vrTriggerValueAction,
            handPath,
            &triggerValue,
            &triggerActive);

        VR_GetControllerFloatState(
            g_vrSqueezeValueAction,
            handPath,
            &squeezeValue,
            &squeezeActive);

        const bool triggerPressed =
            triggerActive &&
            triggerValue >= 0.75f;

        const bool squeezePressed =
            squeezeActive &&
            squeezeValue >= 0.75f;

        if (triggerPressed &&
            !g_vrControllerTriggerPressed[
                handIndex])
        {
            Com_Printf(
                0,
                "[VR] Controller %s trigger "
                "crossed 0.75.\n",
                VR_ControllerHandName(handIndex));
        }

        if (squeezePressed &&
            !g_vrControllerSqueezePressed[
                handIndex])
        {
            Com_Printf(
                0,
                "[VR] Controller %s squeeze "
                "crossed 0.75.\n",
                VR_ControllerHandName(handIndex));
        }

        g_vrControllerTriggerPressed[handIndex] =
            triggerPressed;

        g_vrControllerSqueezePressed[handIndex] =
            squeezePressed;

        if (handIndex == VR_CONTROLLER_RIGHT)
        {
            const bool attackPressed =
                triggerActive &&
                triggerValue >= 0.55f;

            bool logAttackInjection = false;

            {
                std::lock_guard<std::mutex> lock(
                    g_vrWeaponControllerPoseMutex);

                logAttackInjection =
                    attackPressed &&
                    !g_vrRightControllerAttackPressed;

                g_vrRightControllerAttackPressed =
                    attackPressed;
            }

            if (logAttackInjection &&
                !g_vrLoggedRightControllerAttackInjection)
            {
                Com_Printf(
                    0,
                    "[VR] Right controller trigger "
                    "is ready to inject BUTTON_ATTACK.\n");

                g_vrLoggedRightControllerAttackInjection =
                    true;
            }
        }

        if (logPeriodicSnapshot &&
            aimValid)
        {
            VR_LogControllerPoseSnapshot(
                handIndex,
                aimLocation,
                triggerValue,
                squeezeValue);
        }
    }
}

bool VR_CreateSession()
{
    PFN_xrGetD3D11GraphicsRequirementsKHR
        getD3D11GraphicsRequirements = nullptr;

    XrResult result =
        xrGetInstanceProcAddr(
            g_vrInstance,
            "xrGetD3D11GraphicsRequirementsKHR",
            reinterpret_cast<PFN_xrVoidFunction*>(
                &getD3D11GraphicsRequirements));

    if (XR_FAILED(result) ||
        getD3D11GraphicsRequirements == nullptr)
    {
        VR_LogXrFailure(
            "xrGetInstanceProcAddr("
            "xrGetD3D11GraphicsRequirementsKHR)",
            result);

        return false;
    }

    XrGraphicsRequirementsD3D11KHR requirements{
        XR_TYPE_GRAPHICS_REQUIREMENTS_D3D11_KHR
    };

    result =
        getD3D11GraphicsRequirements(
            g_vrInstance,
            g_vrSystemId,
            &requirements);

    if (XR_FAILED(result))
    {
        VR_LogXrFailure(
            "xrGetD3D11GraphicsRequirementsKHR",
            result);

        return false;
    }

    if (!VR_CreateD3D11Device(requirements))
    {
        return false;
    }

    XrGraphicsBindingD3D11KHR graphicsBinding{
        XR_TYPE_GRAPHICS_BINDING_D3D11_KHR
    };

    graphicsBinding.device = g_vrD3dDevice.Get();

    XrSessionCreateInfo sessionCreateInfo{
        XR_TYPE_SESSION_CREATE_INFO
    };

    sessionCreateInfo.next = &graphicsBinding;
    sessionCreateInfo.systemId = g_vrSystemId;

    result =
        xrCreateSession(
            g_vrInstance,
            &sessionCreateInfo,
            &g_vrSession);

    if (XR_FAILED(result))
    {
        VR_LogXrFailure("xrCreateSession", result);
        return false;
    }

    XrReferenceSpaceCreateInfo spaceCreateInfo{
        XR_TYPE_REFERENCE_SPACE_CREATE_INFO
    };

    spaceCreateInfo.referenceSpaceType =
        XR_REFERENCE_SPACE_TYPE_LOCAL;

    spaceCreateInfo.poseInReferenceSpace.orientation.w = 1.0f;

    result =
        xrCreateReferenceSpace(
            g_vrSession,
            &spaceCreateInfo,
            &g_vrAppSpace);

    if (XR_FAILED(result))
    {
        VR_LogXrFailure("xrCreateReferenceSpace", result);
        return false;
    }

    if (!VR_SelectEnvironmentBlendMode())
    {
        return false;
    }

    if (g_vrControllerActionsCreated &&
        !VR_AttachControllerActions())
    {
        Com_PrintWarning(
            0,
            "[VR] Controller actions could not be "
            "attached. Rendering will continue "
            "without controller tracking.\n");
    }

    return true;
}

int64_t VR_SelectSwapchainFormat(
    const std::vector<int64_t>& runtimeFormats)
{
    constexpr std::array<DXGI_FORMAT, 4> preferredFormats = {
        DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
        DXGI_FORMAT_B8G8R8A8_UNORM_SRGB,
        DXGI_FORMAT_R8G8B8A8_UNORM,
        DXGI_FORMAT_B8G8R8A8_UNORM,
    };

    for (const DXGI_FORMAT preferredFormat : preferredFormats)
    {
        const int64_t candidate =
            static_cast<int64_t>(preferredFormat);

        if (std::find(
                runtimeFormats.begin(),
                runtimeFormats.end(),
                candidate) != runtimeFormats.end())
        {
            return candidate;
        }
    }

    return -1;
}

bool VR_CreateSwapchains()
{
    uint32_t viewCount = 0;

    XrResult result =
        xrEnumerateViewConfigurationViews(
            g_vrInstance,
            g_vrSystemId,
            kViewConfiguration,
            0,
            &viewCount,
            nullptr);

    if (XR_FAILED(result) || viewCount == 0)
    {
        VR_LogXrFailure(
            "xrEnumerateViewConfigurationViews(count)",
            result);

        return false;
    }

    g_vrViewConfigs.resize(viewCount);

    for (XrViewConfigurationView& viewConfig :
         g_vrViewConfigs)
    {
        viewConfig = XrViewConfigurationView{
            XR_TYPE_VIEW_CONFIGURATION_VIEW
        };
    }

    result =
        xrEnumerateViewConfigurationViews(
            g_vrInstance,
            g_vrSystemId,
            kViewConfiguration,
            viewCount,
            &viewCount,
            g_vrViewConfigs.data());

    if (XR_FAILED(result))
    {
        VR_LogXrFailure(
            "xrEnumerateViewConfigurationViews(list)",
            result);

        return false;
    }

    if (viewCount != 2)
    {
        Com_PrintWarning(
            0,
            "[VR] Expected two stereo views but runtime returned %u.\n",
            viewCount);

        return false;
    }

    uint32_t formatCount = 0;

    result =
        xrEnumerateSwapchainFormats(
            g_vrSession,
            0,
            &formatCount,
            nullptr);

    if (XR_FAILED(result) || formatCount == 0)
    {
        VR_LogXrFailure(
            "xrEnumerateSwapchainFormats(count)",
            result);

        return false;
    }

    std::vector<int64_t> runtimeFormats(formatCount);

    result =
        xrEnumerateSwapchainFormats(
            g_vrSession,
            formatCount,
            &formatCount,
            runtimeFormats.data());

    if (XR_FAILED(result))
    {
        VR_LogXrFailure(
            "xrEnumerateSwapchainFormats(list)",
            result);

        return false;
    }

    const int64_t selectedFormat =
        VR_SelectSwapchainFormat(runtimeFormats);

    if (selectedFormat < 0)
    {
        Com_PrintWarning(
            0,
            "[VR] Runtime did not expose a supported RGBA/BGRA "
            "swapchain format.\n");

        return false;
    }

    g_vrEyeSwapchains.resize(viewCount);
    g_vrViews.resize(viewCount);

    for (XrView& view : g_vrViews)
    {
        view = XrView{XR_TYPE_VIEW};
    }

    for (uint32_t eyeIndex = 0;
         eyeIndex < viewCount;
         ++eyeIndex)
    {
        const XrViewConfigurationView& viewConfig =
            g_vrViewConfigs[eyeIndex];

        VrEyeSwapchain& eyeSwapchain =
            g_vrEyeSwapchains[eyeIndex];

        // Low-memory diagnostic for the 32-bit CoD4 process.
        // The Quest runtime recommends roughly 2496 pixels per eye, but
        // allocating six full-size color images plus six matching depth
        // images consumes hundreds of megabytes before a map is loaded.
        constexpr uint32_t diagnosticEyeSize = 768u;

        eyeSwapchain.width =
            static_cast<int32_t>(
                viewConfig.recommendedImageRectWidth >
                        diagnosticEyeSize
                    ? diagnosticEyeSize
                    : viewConfig.recommendedImageRectWidth);

        eyeSwapchain.height =
            static_cast<int32_t>(
                viewConfig.recommendedImageRectHeight >
                        diagnosticEyeSize
                    ? diagnosticEyeSize
                    : viewConfig.recommendedImageRectHeight);

        XrSwapchainCreateInfo swapchainCreateInfo{
            XR_TYPE_SWAPCHAIN_CREATE_INFO
        };

        swapchainCreateInfo.usageFlags =
            XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT |
            XR_SWAPCHAIN_USAGE_SAMPLED_BIT;

        swapchainCreateInfo.format = selectedFormat;
        swapchainCreateInfo.sampleCount =
            viewConfig.recommendedSwapchainSampleCount;
        swapchainCreateInfo.width =
            static_cast<uint32_t>(
                eyeSwapchain.width);

        swapchainCreateInfo.height =
            static_cast<uint32_t>(
                eyeSwapchain.height);
        swapchainCreateInfo.faceCount = 1;
        swapchainCreateInfo.arraySize = 1;
        swapchainCreateInfo.mipCount = 1;

        result =
            xrCreateSwapchain(
                g_vrSession,
                &swapchainCreateInfo,
                &eyeSwapchain.handle);

        if (XR_FAILED(result))
        {
            VR_LogXrFailure("xrCreateSwapchain", result);
            return false;
        }

        uint32_t imageCount = 0;

        result =
            xrEnumerateSwapchainImages(
                eyeSwapchain.handle,
                0,
                &imageCount,
                nullptr);

        if (XR_FAILED(result) || imageCount == 0)
        {
            VR_LogXrFailure(
                "xrEnumerateSwapchainImages(count)",
                result);

            return false;
        }

        eyeSwapchain.images.resize(imageCount);

        for (XrSwapchainImageD3D11KHR& image :
             eyeSwapchain.images)
        {
            image = XrSwapchainImageD3D11KHR{
                XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR
            };
        }

        result =
            xrEnumerateSwapchainImages(
                eyeSwapchain.handle,
                imageCount,
                &imageCount,
                reinterpret_cast<XrSwapchainImageBaseHeader*>(
                    eyeSwapchain.images.data()));

        if (XR_FAILED(result))
        {
            VR_LogXrFailure(
                "xrEnumerateSwapchainImages(list)",
                result);

            return false;
        }

        eyeSwapchain.renderTargetViews.resize(imageCount);
        eyeSwapchain.depthTextures.resize(imageCount);
        eyeSwapchain.depthStencilViews.resize(imageCount);

        for (uint32_t imageIndex = 0;
             imageIndex < imageCount;
             ++imageIndex)
        {
            ID3D11Texture2D* texture =
                eyeSwapchain.images[imageIndex].texture;

            D3D11_TEXTURE2D_DESC textureDescription = {};
            texture->GetDesc(&textureDescription);

            D3D11_RENDER_TARGET_VIEW_DESC
                renderTargetViewDescription = {};

            // OpenXR runtimes may expose the swapchain texture through a
            // typeless resource. A null RTV description then fails because
            // Direct3D cannot infer the typed view format. Use the exact
            // typed format selected through xrEnumerateSwapchainFormats.
            renderTargetViewDescription.Format =
                static_cast<DXGI_FORMAT>(selectedFormat);

            const bool isMultisampled =
                textureDescription.SampleDesc.Count > 1;

            const bool isArrayTexture =
                textureDescription.ArraySize > 1;

            if (isArrayTexture && isMultisampled)
            {
                renderTargetViewDescription.ViewDimension =
                    D3D11_RTV_DIMENSION_TEXTURE2DMSARRAY;

                renderTargetViewDescription
                    .Texture2DMSArray.FirstArraySlice = 0;

                renderTargetViewDescription
                    .Texture2DMSArray.ArraySize = 1;
            }
            else if (isArrayTexture)
            {
                renderTargetViewDescription.ViewDimension =
                    D3D11_RTV_DIMENSION_TEXTURE2DARRAY;

                renderTargetViewDescription
                    .Texture2DArray.MipSlice = 0;

                renderTargetViewDescription
                    .Texture2DArray.FirstArraySlice = 0;

                renderTargetViewDescription
                    .Texture2DArray.ArraySize = 1;
            }
            else if (isMultisampled)
            {
                renderTargetViewDescription.ViewDimension =
                    D3D11_RTV_DIMENSION_TEXTURE2DMS;
            }
            else
            {
                renderTargetViewDescription.ViewDimension =
                    D3D11_RTV_DIMENSION_TEXTURE2D;

                renderTargetViewDescription
                    .Texture2D.MipSlice = 0;
            }

            const HRESULT hr =
                g_vrD3dDevice->CreateRenderTargetView(
                    texture,
                    &renderTargetViewDescription,
                    eyeSwapchain
                        .renderTargetViews[imageIndex]
                        .GetAddressOf());

            if (FAILED(hr))
            {
                Com_PrintWarning(
                    0,
                    "[VR] RTV failure: eye %u image %u, "
                    "resource format %u, selected format %lld, "
                    "array size %u, sample count %u.\n",
                    eyeIndex,
                    imageIndex,
                    static_cast<unsigned int>(
                        textureDescription.Format),
                    static_cast<long long>(selectedFormat),
                    textureDescription.ArraySize,
                    textureDescription.SampleDesc.Count);

                VR_LogHrFailure(
                    "ID3D11Device::CreateRenderTargetView",
                    hr);

                return false;
            }

            D3D11_TEXTURE2D_DESC depthTextureDescription = {};
            depthTextureDescription.Width =
                textureDescription.Width;
            depthTextureDescription.Height =
                textureDescription.Height;
            depthTextureDescription.MipLevels = 1;
            depthTextureDescription.ArraySize = 1;
            depthTextureDescription.Format =
                DXGI_FORMAT_D24_UNORM_S8_UINT;
            depthTextureDescription.SampleDesc =
                textureDescription.SampleDesc;
            depthTextureDescription.Usage =
                D3D11_USAGE_DEFAULT;
            depthTextureDescription.BindFlags =
                D3D11_BIND_DEPTH_STENCIL;

            HRESULT depthHr =
                g_vrD3dDevice->CreateTexture2D(
                    &depthTextureDescription,
                    nullptr,
                    eyeSwapchain
                        .depthTextures[imageIndex]
                        .GetAddressOf());

            if (FAILED(depthHr))
            {
                VR_LogHrFailure(
                    "CreateTexture2D(depth)",
                    depthHr);

                return false;
            }

            depthHr =
                g_vrD3dDevice->CreateDepthStencilView(
                    eyeSwapchain
                        .depthTextures[imageIndex]
                        .Get(),
                    nullptr,
                    eyeSwapchain
                        .depthStencilViews[imageIndex]
                        .GetAddressOf());

            if (FAILED(depthHr))
            {
                VR_LogHrFailure(
                    "CreateDepthStencilView",
                    depthHr);

                return false;
            }
        }

        Com_Printf(
            0,
            "[VR] OpenXR low-memory swapchain diagnostic: "
            "eye %u uses %d x %d with %u images.\n",
            eyeIndex,
            eyeSwapchain.width,
            eyeSwapchain.height,
            imageCount);
    }

    Com_Printf(
        0,
        "[VR] Selected OpenXR swapchain DXGI format %lld.\n",
        static_cast<long long>(selectedFormat));

    return true;
}

void VR_HandleSessionStateChanged(
    const XrEventDataSessionStateChanged& stateChanged)
{
    g_vrSessionState = stateChanged.state;

    Com_Printf(
        0,
        "[VR] OpenXR session state: %s.\n",
        VR_SessionStateName(g_vrSessionState));

    if (g_vrSessionState == XR_SESSION_STATE_READY &&
        !g_vrSessionRunning)
    {
        XrSessionBeginInfo beginInfo{
            XR_TYPE_SESSION_BEGIN_INFO
        };

        beginInfo.primaryViewConfigurationType =
            kViewConfiguration;

        const XrResult result =
            xrBeginSession(g_vrSession, &beginInfo);

        if (XR_SUCCEEDED(result))
        {
            g_vrSessionRunning = true;

            Com_Printf(
                0,
                "[VR] OpenXR session started.\n");
        }
        else
        {
            VR_LogXrFailure("xrBeginSession", result);
        }
    }
    else if (
        g_vrSessionState == XR_SESSION_STATE_STOPPING &&
        g_vrSessionRunning)
    {
        const XrResult result =
            xrEndSession(g_vrSession);

        if (XR_FAILED(result))
        {
            VR_LogXrFailure("xrEndSession", result);
        }

        g_vrSessionRunning = false;
    }
    else if (
        g_vrSessionState == XR_SESSION_STATE_EXITING ||
        g_vrSessionState == XR_SESSION_STATE_LOSS_PENDING)
    {
        g_vrSessionRunning = false;
        g_vrExitRequested = true;
    }
}

void VR_PollEvents()
{
    XrEventDataBuffer eventData{
        XR_TYPE_EVENT_DATA_BUFFER
    };

    while (true)
    {
        const XrResult result =
            xrPollEvent(g_vrInstance, &eventData);

        if (result == XR_EVENT_UNAVAILABLE)
        {
            return;
        }

        if (XR_FAILED(result))
        {
            VR_LogXrFailure("xrPollEvent", result);
            return;
        }

        switch (eventData.type)
        {
            case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED:
            {
                const auto* stateChanged =
                    reinterpret_cast<
                        const XrEventDataSessionStateChanged*>(
                            &eventData);

                VR_HandleSessionStateChanged(*stateChanged);
                break;
            }

            case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING:
                Com_PrintWarning(
                    0,
                    "[VR] OpenXR runtime reported instance loss "
                    "pending.\n");
                g_vrExitRequested = true;
                break;

            default:
                break;
        }

        eventData = XrEventDataBuffer{
            XR_TYPE_EVENT_DATA_BUFFER
        };
    }
}

bool VR_RenderSolidColorFrame(
    const XrFrameState& frameState,
    XrCompositionLayerProjection& projectionLayer,
    std::vector<XrCompositionLayerProjectionView>&
        projectionViews)
{
    XrViewLocateInfo viewLocateInfo{
        XR_TYPE_VIEW_LOCATE_INFO
    };

    viewLocateInfo.viewConfigurationType =
        kViewConfiguration;
    viewLocateInfo.displayTime =
        frameState.predictedDisplayTime;
    viewLocateInfo.space = g_vrAppSpace;

    XrViewState viewState{XR_TYPE_VIEW_STATE};

    uint32_t locatedViewCount = 0;

    XrResult result =
        xrLocateViews(
            g_vrSession,
            &viewLocateInfo,
            &viewState,
            static_cast<uint32_t>(g_vrViews.size()),
            &locatedViewCount,
            g_vrViews.data());

    if (XR_FAILED(result))
    {
        VR_LogXrFailure("xrLocateViews", result);
        return false;
    }

    const XrViewStateFlags requiredFlags =
        XR_VIEW_STATE_ORIENTATION_VALID_BIT |
        XR_VIEW_STATE_POSITION_VALID_BIT;

    if ((viewState.viewStateFlags & requiredFlags) !=
        requiredFlags)
    {
        return false;
    }

    if (locatedViewCount != g_vrEyeSwapchains.size())
    {
        Com_PrintWarning(
            0,
            "[VR] xrLocateViews returned %u views; expected %u.\n",
            locatedViewCount,
            static_cast<unsigned int>(
                g_vrEyeSwapchains.size()));

        return false;
    }

    VR_UpdateControllerActions(
        frameState.predictedDisplayTime);

    projectionViews.resize(locatedViewCount);

    if (locatedViewCount >= kVrStereoEyeCount)
    {
        std::array<
            VrEyeProjectionTangents,
            kVrStereoEyeCount>
            publishedTangents = {};

        bool projectionValid = true;

        for (std::uint32_t eyeIndex = 0u;
             eyeIndex < kVrStereoEyeCount;
             ++eyeIndex)
        {
            const XrFovf& fov =
                g_vrViews[eyeIndex].fov;

            VrEyeProjectionTangents& tangents =
                publishedTangents[eyeIndex];

            tangents.left =
                std::tan(fov.angleLeft);

            tangents.right =
                std::tan(fov.angleRight);

            tangents.down =
                std::tan(fov.angleDown);

            tangents.up =
                std::tan(fov.angleUp);

            projectionValid =
                projectionValid &&
                tangents.left < tangents.right &&
                tangents.down < tangents.up &&
                tangents.left < 0.0f &&
                tangents.right > 0.0f &&
                tangents.down < 0.0f &&
                tangents.up > 0.0f;
        }

        {
            std::lock_guard<std::mutex> lock(
                g_vrProjectionMutex);

            if (projectionValid)
            {
                g_vrEyeProjectionTangents =
                    publishedTangents;

                g_vrEyeProjectionValid = true;
            }
        }

        if (projectionValid &&
            !g_vrLoggedProjectionPublish)
        {
            Com_Printf(
                0,
                "[VR] Published OpenXR per-eye "
                "projection tangents: "
                "L %.4f %.4f %.4f %.4f, "
                "R %.4f %.4f %.4f %.4f.\n",
                publishedTangents[0].left,
                publishedTangents[0].right,
                publishedTangents[0].down,
                publishedTangents[0].up,
                publishedTangents[1].left,
                publishedTangents[1].right,
                publishedTangents[1].down,
                publishedTangents[1].up);

            g_vrLoggedProjectionPublish = true;
        }
    }

    if (locatedViewCount > 0)
    {
        VR_PublishHeadOrientation(
            g_vrViews[0].pose.orientation);
    }

    VR_UpdateCapturedStereoTexture();

    constexpr float clearColor[4] = {
        0.03f,
        0.08f,
        0.20f,
        1.0f,
    };

    for (uint32_t eyeIndex = 0;
         eyeIndex < locatedViewCount;
         ++eyeIndex)
    {
        VrEyeSwapchain& eyeSwapchain =
            g_vrEyeSwapchains[eyeIndex];

        uint32_t imageIndex = 0;

        XrSwapchainImageAcquireInfo acquireInfo{
            XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO
        };

        result =
            xrAcquireSwapchainImage(
                eyeSwapchain.handle,
                &acquireInfo,
                &imageIndex);

        if (XR_FAILED(result))
        {
            VR_LogXrFailure(
                "xrAcquireSwapchainImage",
                result);

            return false;
        }

        XrSwapchainImageWaitInfo waitInfo{
            XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO
        };

        waitInfo.timeout = XR_INFINITE_DURATION;

        result =
            xrWaitSwapchainImage(
                eyeSwapchain.handle,
                &waitInfo);

        if (XR_FAILED(result))
        {
            VR_LogXrFailure(
                "xrWaitSwapchainImage",
                result);

            XrSwapchainImageReleaseInfo releaseInfo{
                XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO
            };

            xrReleaseSwapchainImage(
                eyeSwapchain.handle,
                &releaseInfo);

            return false;
        }

        ID3D11RenderTargetView* renderTarget =
            eyeSwapchain
                .renderTargetViews[imageIndex]
                .Get();

        ID3D11DepthStencilView* depthStencilView =
            eyeSwapchain
                .depthStencilViews[imageIndex]
                .Get();

        g_vrD3dContext->OMSetRenderTargets(
            1,
            &renderTarget,
            depthStencilView);

        g_vrD3dContext->ClearRenderTargetView(
            renderTarget,
            clearColor);

        g_vrD3dContext->ClearDepthStencilView(
            depthStencilView,
            D3D11_CLEAR_DEPTH |
                D3D11_CLEAR_STENCIL,
            1.0f,
            0);

        D3D11_VIEWPORT viewport = {};
        viewport.Width =
            static_cast<float>(eyeSwapchain.width);
        viewport.Height =
            static_cast<float>(eyeSwapchain.height);
        viewport.MinDepth = 0.0f;
        viewport.MaxDepth = 1.0f;

        g_vrD3dContext->RSSetViewports(1, &viewport);
        g_vrD3dContext->RSSetState(
            g_vrTestRasterizerState.Get());

        g_vrD3dContext->OMSetDepthStencilState(
            g_vrTestDepthStencilState.Get(),
            0);

        ID3D11ShaderResourceView* capturedView =
            g_vrCapturedStereoView.Get();

        if (capturedView != nullptr)
        {
            g_vrD3dContext->OMSetRenderTargets(
                1,
                &renderTarget,
                nullptr);

            g_vrD3dContext->OMSetDepthStencilState(
                nullptr,
                0);

            g_vrD3dContext->IASetInputLayout(
                g_vrBlitInputLayout.Get());

            const UINT stride =
                sizeof(VrBlitVertex);

            const UINT offset = 0;

            ID3D11Buffer* vertexBuffer =
                eyeIndex <
                    g_vrBlitVertexBuffers.size()
                    ? g_vrBlitVertexBuffers[eyeIndex]
                        .Get()
                    : nullptr;

            g_vrD3dContext->IASetVertexBuffers(
                0,
                1,
                &vertexBuffer,
                &stride,
                &offset);

            g_vrD3dContext->IASetIndexBuffer(
                nullptr,
                DXGI_FORMAT_UNKNOWN,
                0);

            g_vrD3dContext->IASetPrimitiveTopology(
                D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

            g_vrD3dContext->VSSetShader(
                g_vrBlitVertexShader.Get(),
                nullptr,
                0);

            g_vrD3dContext->PSSetShader(
                g_vrBlitPixelShader.Get(),
                nullptr,
                0);

            g_vrD3dContext->PSSetShaderResources(
                0,
                1,
                &capturedView);

            ID3D11SamplerState* sampler =
                g_vrBlitSampler.Get();

            g_vrD3dContext->PSSetSamplers(
                0,
                1,
                &sampler);

            g_vrD3dContext->Draw(4, 0);

            ID3D11ShaderResourceView* nullView = nullptr;

            g_vrD3dContext->PSSetShaderResources(
                0,
                1,
                &nullView);
        }

        VR_RenderControllerProxies(
            eyeIndex,
            eyeSwapchain.width,
            eyeSwapchain.height);

        g_vrD3dContext->Flush();

        XrSwapchainImageReleaseInfo releaseInfo{
            XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO
        };

        result =
            xrReleaseSwapchainImage(
                eyeSwapchain.handle,
                &releaseInfo);

        if (XR_FAILED(result))
        {
            VR_LogXrFailure(
                "xrReleaseSwapchainImage",
                result);

            return false;
        }

        XrCompositionLayerProjectionView& projectionView =
            projectionViews[eyeIndex];

        projectionView =
            XrCompositionLayerProjectionView{
                XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW
            };

        projectionView.pose = g_vrViews[eyeIndex].pose;
        projectionView.fov = g_vrViews[eyeIndex].fov;

        projectionView.subImage.swapchain =
            eyeSwapchain.handle;

        projectionView.subImage.imageRect.offset = {0, 0};

        projectionView.subImage.imageRect.extent = {
            eyeSwapchain.width,
            eyeSwapchain.height,
        };

        projectionView.subImage.imageArrayIndex = 0;
    }

    projectionLayer =
        XrCompositionLayerProjection{
            XR_TYPE_COMPOSITION_LAYER_PROJECTION
        };

    projectionLayer.space = g_vrAppSpace;
    projectionLayer.viewCount =
        static_cast<uint32_t>(projectionViews.size());
    projectionLayer.views = projectionViews.data();

    if (!g_vrLoggedFirstTestFrame)
    {
        Com_Printf(
            0,
            "[VR] Submitted first OpenXR frame during D3D9 bridge.\n");

        g_vrLoggedFirstTestFrame = true;
    }

    return true;
}

void VR_ResetState()
{
    g_vrInstance = XR_NULL_HANDLE;
    g_vrSystemId = XR_NULL_SYSTEM_ID;
    g_vrSession = XR_NULL_HANDLE;
    g_vrAppSpace = XR_NULL_HANDLE;
    g_vrSessionState = XR_SESSION_STATE_UNKNOWN;
    g_vrBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    g_vrInitialized = false;
    g_vrSessionRunning = false;
    g_vrExitRequested = false;
}
}



bool VR_ApplyRightControllerToWeaponPlacement(
    const float cameraOrigin[3],
    const float cameraAxis[3][3],
    float weaponOrigin[3],
    float weaponAxis[3][3])
{
    if (cameraOrigin == nullptr ||
        cameraAxis == nullptr ||
        weaponOrigin == nullptr ||
        weaponAxis == nullptr)
    {
        return false;
    }

    float currentPosition[3] = {};
    float currentAxis[3][3] = {};
    float basePosition[3] = {};
    float baseAxis[3][3] = {};

    {
        std::lock_guard<std::mutex> lock(
            g_vrWeaponControllerPoseMutex);

        if (!g_vrRightControllerWeaponPoseValid)
        {
            return false;
        }

        if (!g_vrRightControllerWeaponBaseValid)
        {
            memcpy(
                g_vrRightControllerWeaponBasePosition,
                g_vrRightControllerWeaponPosition,
                sizeof(
                    g_vrRightControllerWeaponBasePosition));

            memcpy(
                g_vrRightControllerWeaponBaseAxis,
                g_vrRightControllerWeaponAxis,
                sizeof(
                    g_vrRightControllerWeaponBaseAxis));

            g_vrRightControllerWeaponBaseValid = true;
        }

        memcpy(
            currentPosition,
            g_vrRightControllerWeaponPosition,
            sizeof(currentPosition));

        memcpy(
            currentAxis,
            g_vrRightControllerWeaponAxis,
            sizeof(currentAxis));

        memcpy(
            basePosition,
            g_vrRightControllerWeaponBasePosition,
            sizeof(basePosition));

        memcpy(
            baseAxis,
            g_vrRightControllerWeaponBaseAxis,
            sizeof(baseAxis));
    }

    if (!g_vrLoggedRightControllerWeaponCalibration)
    {
        Com_Printf(
            0,
            "[VR] Calibrated right-controller "
            "weapon pose at first viewmodel render.\n");

        g_vrLoggedRightControllerWeaponCalibration = true;
    }

    float originalOriginLocal[3] = {};
    float originDeltaWorld[3] = {
        weaponOrigin[0] - cameraOrigin[0],
        weaponOrigin[1] - cameraOrigin[1],
        weaponOrigin[2] - cameraOrigin[2],
    };

    for (int localComponent = 0;
         localComponent < 3;
         ++localComponent)
    {
        originalOriginLocal[localComponent] =
            originDeltaWorld[0] *
                cameraAxis[localComponent][0] +
            originDeltaWorld[1] *
                cameraAxis[localComponent][1] +
            originDeltaWorld[2] *
                cameraAxis[localComponent][2];
    }

    auto transformBaseLocalToCurrent =
        [&baseAxis, &currentAxis](
            const float input[3],
            float output[3])
        {
            float controllerLocal[3] = {};

            for (int controllerComponent = 0;
                 controllerComponent < 3;
                 ++controllerComponent)
            {
                controllerLocal[controllerComponent] =
                    input[0] *
                        baseAxis[controllerComponent][0] +
                    input[1] *
                        baseAxis[controllerComponent][1] +
                    input[2] *
                        baseAxis[controllerComponent][2];
            }

            for (int outputComponent = 0;
                 outputComponent < 3;
                 ++outputComponent)
            {
                output[outputComponent] =
                    controllerLocal[0] *
                        currentAxis[0][outputComponent] +
                    controllerLocal[1] *
                        currentAxis[1][outputComponent] +
                    controllerLocal[2] *
                        currentAxis[2][outputComponent];
            }
        };

    float transformedOriginLocal[3] = {};

    transformBaseLocalToCurrent(
        originalOriginLocal,
        transformedOriginLocal);

    for (int component = 0;
         component < 3;
         ++component)
    {
        transformedOriginLocal[component] +=
            currentPosition[component] -
            basePosition[component];
    }

    for (int worldComponent = 0;
         worldComponent < 3;
         ++worldComponent)
    {
        weaponOrigin[worldComponent] =
            cameraOrigin[worldComponent] +
            transformedOriginLocal[0] *
                cameraAxis[0][worldComponent] +
            transformedOriginLocal[1] *
                cameraAxis[1][worldComponent] +
            transformedOriginLocal[2] *
                cameraAxis[2][worldComponent];
    }

    float originalWeaponAxis[3][3] = {};

    memcpy(
        originalWeaponAxis,
        weaponAxis,
        sizeof(originalWeaponAxis));

    for (int weaponAxisRow = 0;
         weaponAxisRow < 3;
         ++weaponAxisRow)
    {
        float weaponRowCameraLocal[3] = {};

        for (int cameraComponent = 0;
             cameraComponent < 3;
             ++cameraComponent)
        {
            weaponRowCameraLocal[cameraComponent] =
                originalWeaponAxis[weaponAxisRow][0] *
                    cameraAxis[cameraComponent][0] +
                originalWeaponAxis[weaponAxisRow][1] *
                    cameraAxis[cameraComponent][1] +
                originalWeaponAxis[weaponAxisRow][2] *
                    cameraAxis[cameraComponent][2];
        }

        float transformedWeaponRowLocal[3] = {};

        transformBaseLocalToCurrent(
            weaponRowCameraLocal,
            transformedWeaponRowLocal);

        for (int worldComponent = 0;
             worldComponent < 3;
             ++worldComponent)
        {
            weaponAxis[weaponAxisRow][worldComponent] =
                transformedWeaponRowLocal[0] *
                    cameraAxis[0][worldComponent] +
                transformedWeaponRowLocal[1] *
                    cameraAxis[1][worldComponent] +
                transformedWeaponRowLocal[2] *
                    cameraAxis[2][worldComponent];
        }
    }

    const float forwardLength =
        std::sqrt(
            weaponAxis[0][0] * weaponAxis[0][0] +
            weaponAxis[0][1] * weaponAxis[0][1] +
            weaponAxis[0][2] * weaponAxis[0][2]);

    if (forwardLength > 0.0001f)
    {
        std::lock_guard<std::mutex> lock(
            g_vrWeaponControllerPoseMutex);

        g_vrRightControllerFinalWeaponForward[0] =
            weaponAxis[0][0] / forwardLength;

        g_vrRightControllerFinalWeaponForward[1] =
            weaponAxis[0][1] / forwardLength;

        g_vrRightControllerFinalWeaponForward[2] =
            weaponAxis[0][2] / forwardLength;

        g_vrRightControllerFinalWeaponAimValid = true;
    }

    if (!g_vrLoggedRightControllerWeaponApply)
    {
        Com_Printf(
            0,
            "[VR] Applied right-controller motion "
            "delta to the CoD4 viewmodel.\n");

        g_vrLoggedRightControllerWeaponApply = true;
    }

    return true;
}


void VR_PublishRightControllerWeaponMuzzleWorld(
    const float muzzleOrigin[3])
{
    if (muzzleOrigin == nullptr)
    {
        return;
    }

    std::lock_guard<std::mutex> lock(
        g_vrWeaponControllerPoseMutex);

    g_vrRightControllerFinalWeaponMuzzleWorld[0] =
        muzzleOrigin[0];

    g_vrRightControllerFinalWeaponMuzzleWorld[1] =
        muzzleOrigin[1];

    g_vrRightControllerFinalWeaponMuzzleWorld[2] =
        muzzleOrigin[2];

    g_vrRightControllerFinalWeaponMuzzleValid = true;
}

void VR_SetRightControllerWeaponMuzzleBlocked(
    const bool blocked)
{
    std::lock_guard<std::mutex> lock(
        g_vrWeaponControllerPoseMutex);

    g_vrRightControllerFinalWeaponMuzzleBlocked =
        blocked;
}

bool VR_ShouldSuppressRightControllerBlockedMuzzleShot()
{
    std::lock_guard<std::mutex> lock(
        g_vrWeaponControllerPoseMutex);

    return
        g_vrRightControllerFinalWeaponAimValid &&
        g_vrRightControllerAttackPressed &&
        g_vrRightControllerFinalWeaponMuzzleBlocked;
}


bool VR_GetRightControllerWeaponMuzzleWorld(
    float muzzleOrigin[3])
{
    if (muzzleOrigin == nullptr)
    {
        return false;
    }

    std::lock_guard<std::mutex> lock(
        g_vrWeaponControllerPoseMutex);

    if (!g_vrRightControllerFinalWeaponMuzzleValid)
    {
        return false;
    }

    muzzleOrigin[0] =
        g_vrRightControllerFinalWeaponMuzzleWorld[0];

    muzzleOrigin[1] =
        g_vrRightControllerFinalWeaponMuzzleWorld[1];

    muzzleOrigin[2] =
        g_vrRightControllerFinalWeaponMuzzleWorld[2];

    return true;
}


bool VR_GetRightControllerWeaponCommand(
    float* gunPitch,
    float* gunYaw,
    bool* attackPressed)
{
    if (gunPitch == nullptr ||
        gunYaw == nullptr ||
        attackPressed == nullptr)
    {
        return false;
    }

    float forward[3] = {};
    bool aimValid = false;
    bool currentAttackPressed = false;

    {
        std::lock_guard<std::mutex> lock(
            g_vrWeaponControllerPoseMutex);

        aimValid =
            g_vrRightControllerFinalWeaponAimValid;

        currentAttackPressed =
            g_vrRightControllerAttackPressed;

        forward[0] =
            g_vrRightControllerFinalWeaponForward[0];

        forward[1] =
            g_vrRightControllerFinalWeaponForward[1];

        forward[2] =
            g_vrRightControllerFinalWeaponForward[2];
    }

    *attackPressed =
        aimValid && currentAttackPressed;

    if (!aimValid)
    {
        return false;
    }

    constexpr float radiansToDegrees =
        57.29577951308232f;

    const float horizontalLength =
        std::sqrt(
            forward[0] * forward[0] +
            forward[1] * forward[1]);

    float pitch =
        std::atan2(
            -forward[2],
            horizontalLength) *
        radiansToDegrees;

    float yaw =
        std::atan2(
            forward[1],
            forward[0]) *
        radiansToDegrees;

    pitch = std::fmod(pitch, 360.0f);
    yaw = std::fmod(yaw, 360.0f);

    if (pitch < 0.0f)
    {
        pitch += 360.0f;
    }

    if (yaw < 0.0f)
    {
        yaw += 360.0f;
    }

    *gunPitch = pitch;
    *gunYaw = yaw;

    if (!g_vrLoggedRightControllerUsercmdAim)
    {
        Com_Printf(
            0,
            "[VR] Supplied right-controller weapon "
            "aim to the multiplayer usercmd.\n");

        g_vrLoggedRightControllerUsercmdAim = true;
    }

    return true;
}

bool VR_ApplyHeadPosition(
    float viewOrigin[3],
    const float viewAxis[3][3])
{
    if (viewOrigin == nullptr ||
        viewAxis == nullptr)
    {
        return false;
    }

    float localPosition[3] = {};

    {
        std::lock_guard<std::mutex> lock(
            g_vrHeadOrientationMutex);

        if (!g_vrHeadPositionValid)
        {
            return false;
        }

        localPosition[0] =
            g_vrHeadPositionLocal[0];

        localPosition[1] =
            g_vrHeadPositionLocal[1];

        localPosition[2] =
            g_vrHeadPositionLocal[2];
    }

    // Use the normal player-camera basis. The MP callsite invokes this
    // before applying the headset's rotational offset.
    for (int worldComponent = 0;
         worldComponent < 3;
         ++worldComponent)
    {
        viewOrigin[worldComponent] +=
            localPosition[0] *
                viewAxis[0][worldComponent] +
            localPosition[1] *
                viewAxis[1][worldComponent] +
            localPosition[2] *
                viewAxis[2][worldComponent];
    }

    if (!g_vrLoggedFirstPositionApply)
    {
        Com_Printf(
            0,
            "[VR] Applied OpenXR headset position "
            "to the CoD4 camera.\n");

        g_vrLoggedFirstPositionApply = true;
    }

    return true;
}



void VR_BeginStereoEyeRender(
    const unsigned int eyeIndex)
{
    g_vrCurrentRenderEye =
        eyeIndex < kVrStereoEyeCount
            ? static_cast<int>(eyeIndex)
            : -1;
}

void VR_EndStereoEyeRender()
{
    g_vrCurrentRenderEye = -1;
}

bool VR_GetStereoEyeFovBounds(
    const unsigned int eyeIndex,
    float* tanHalfFovX,
    float* tanHalfFovY)
{
    if (eyeIndex >= kVrStereoEyeCount ||
        tanHalfFovX == nullptr ||
        tanHalfFovY == nullptr)
    {
        return false;
    }

    VrEyeProjectionTangents tangents = {};

    {
        std::lock_guard<std::mutex> lock(
            g_vrProjectionMutex);

        if (!g_vrEyeProjectionValid)
        {
            return false;
        }

        tangents =
            g_vrEyeProjectionTangents[eyeIndex];
    }

    const float leftMagnitude =
        -tangents.left;

    const float downMagnitude =
        -tangents.down;

    *tanHalfFovX =
        leftMagnitude > tangents.right
            ? leftMagnitude
            : tangents.right;

    *tanHalfFovY =
        downMagnitude > tangents.up
            ? downMagnitude
            : tangents.up;

    return
        *tanHalfFovX > 0.0f &&
        *tanHalfFovY > 0.0f;
}

bool VR_GetCurrentRenderEyeProjection(
    float* tanLeft,
    float* tanRight,
    float* tanDown,
    float* tanUp)
{
    if (tanLeft == nullptr ||
        tanRight == nullptr ||
        tanDown == nullptr ||
        tanUp == nullptr ||
        g_vrCurrentRenderEye < 0 ||
        g_vrCurrentRenderEye >=
            static_cast<int>(
                kVrStereoEyeCount))
    {
        return false;
    }

    const unsigned int eyeIndex =
        static_cast<unsigned int>(
            g_vrCurrentRenderEye);

    VrEyeProjectionTangents tangents = {};

    {
        std::lock_guard<std::mutex> lock(
            g_vrProjectionMutex);

        if (!g_vrEyeProjectionValid)
        {
            return false;
        }

        tangents =
            g_vrEyeProjectionTangents[eyeIndex];
    }

    *tanLeft = tangents.left;
    *tanRight = tangents.right;
    *tanDown = tangents.down;
    *tanUp = tangents.up;

    if (!g_vrLoggedProjectionApply[eyeIndex])
    {
        Com_Printf(
            0,
            "[VR] Supplying OpenXR asymmetric "
            "projection for eye %u.\n",
            eyeIndex);

        g_vrLoggedProjectionApply[eyeIndex] = true;
    }

    return true;
}

bool VR_ApplyStereoEyeOffsetForEye(
    float viewOrigin[3],
    const float viewAxis[3][3],
    const unsigned int eyeIndex)
{
    if (viewOrigin == nullptr ||
        viewAxis == nullptr ||
        eyeIndex >= kVrStereoEyeCount)
    {
        return false;
    }

    float halfIpdGameUnits =
        kVrDefaultHalfIpdGameUnits;

    {
        std::lock_guard<std::mutex> lock(
            g_vrHeadOrientationMutex);

        if (!g_vrHeadOrientationValid)
        {
            return false;
        }

        halfIpdGameUnits =
            g_vrHalfIpdGameUnits;
    }

    // CoD's camera basis uses row 1 as left. Left eye therefore moves in
    // +left, while right eye moves in -left.
    const float signedEyeOffset =
        eyeIndex == 0u
            ? halfIpdGameUnits
            : -halfIpdGameUnits;

    for (int worldComponent = 0;
         worldComponent < 3;
         ++worldComponent)
    {
        viewOrigin[worldComponent] +=
            signedEyeOffset *
            viewAxis[1][worldComponent];
    }

    if (!g_vrLoggedFirstStereoEyeOffset)
    {
        Com_Printf(
            0,
            "[VR] Applied same-frame stereo eye "
            "offsets; half IPD is %.3f CoD units.\n",
            halfIpdGameUnits);

        g_vrLoggedFirstStereoEyeOffset = true;
    }

    return true;
}


bool VR_ApplyHeadOrientation(
    float viewAxis[3][3])
{
    if (viewAxis == nullptr)
    {
        return false;
    }

    float headAxis[3][3] = {};

    {
        std::lock_guard<std::mutex> lock(
            g_vrHeadOrientationMutex);

        if (!g_vrHeadOrientationValid)
        {
            return false;
        }

        for (int row = 0; row < 3; ++row)
        {
            for (int column = 0;
                 column < 3;
                 ++column)
            {
                headAxis[row][column] =
                    g_vrHeadOrientationAxis[row][column];
            }
        }
    }

    float originalAxis[3][3] = {};

    for (int row = 0; row < 3; ++row)
    {
        for (int column = 0;
             column < 3;
             ++column)
        {
            originalAxis[row][column] =
                viewAxis[row][column];
        }
    }

    // The headset matrix is expressed in the camera-local
    // forward/left/up basis. Compose it with CoD4's completed
    // world-space render-camera axis.
    for (int row = 0; row < 3; ++row)
    {
        for (int column = 0;
             column < 3;
             ++column)
        {
            viewAxis[row][column] =
                headAxis[row][0] *
                    originalAxis[0][column] +
                headAxis[row][1] *
                    originalAxis[1][column] +
                headAxis[row][2] *
                    originalAxis[2][column];
        }
    }

    if (!g_vrLoggedFirstCameraApply)
    {
        Com_Printf(
            0,
            "[VR] Applied OpenXR headset orientation "
            "to the CoD4 camera.\n");

        g_vrLoggedFirstCameraApply = true;
    }

    return true;
}

bool VR_Init()
{
    VR_ResetHeadOrientation();

    {
        std::lock_guard<std::mutex> lock(
            g_vrProjectionMutex);

        g_vrEyeProjectionTangents = {};
        g_vrEyeProjectionValid = false;
    }

    g_vrCurrentRenderEye = -1;
    g_vrLoggedProjectionApply.fill(false);
    g_vrLoggedProjectionPublish = false;
    if (g_vrInitialized)
    {
        return true;
    }

    Com_Printf(
        0,
        "[VR] Initializing OpenXR head-rotation frame bridge...\n");

    if (!VR_HasInstanceExtension(
            XR_KHR_D3D11_ENABLE_EXTENSION_NAME))
    {
        Com_PrintWarning(
            0,
            "[VR] Runtime does not expose "
            "XR_KHR_D3D11_enable.\n");

        return false;
    }

    const char* enabledExtensions[] = {
        XR_KHR_D3D11_ENABLE_EXTENSION_NAME,
    };

    XrInstanceCreateInfo createInfo{
        XR_TYPE_INSTANCE_CREATE_INFO
    };

    std::snprintf(
        createInfo.applicationInfo.applicationName,
        XR_MAX_APPLICATION_NAME_SIZE,
        "%s",
        "KisakCOD VR");

    createInfo.applicationInfo.applicationVersion = 1;

    std::snprintf(
        createInfo.applicationInfo.engineName,
        XR_MAX_ENGINE_NAME_SIZE,
        "%s",
        "IW3 / KisakCOD");

    createInfo.applicationInfo.engineVersion = 1;
    createInfo.applicationInfo.apiVersion =
        XR_MAKE_VERSION(1, 0, 0);

    createInfo.enabledExtensionCount = 1;
    createInfo.enabledExtensionNames = enabledExtensions;

    XrResult result =
        xrCreateInstance(&createInfo, &g_vrInstance);

    if (XR_FAILED(result))
    {
        VR_LogXrFailure("xrCreateInstance", result);
        VR_ResetState();
        return false;
    }

    XrInstanceProperties runtimeProperties{
        XR_TYPE_INSTANCE_PROPERTIES
    };

    result =
        xrGetInstanceProperties(
            g_vrInstance,
            &runtimeProperties);

    if (XR_FAILED(result))
    {
        VR_LogXrFailure(
            "xrGetInstanceProperties",
            result);

        VR_Shutdown();
        return false;
    }

    Com_Printf(
        0,
        "[VR] OpenXR runtime: %s.\n",
        runtimeProperties.runtimeName);

    XrSystemGetInfo systemInfo{
        XR_TYPE_SYSTEM_GET_INFO
    };

    systemInfo.formFactor =
        XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;

    result =
        xrGetSystem(
            g_vrInstance,
            &systemInfo,
            &g_vrSystemId);

    if (XR_FAILED(result))
    {
        VR_LogXrFailure("xrGetSystem", result);
        VR_Shutdown();
        return false;
    }

    XrSystemProperties systemProperties{
        XR_TYPE_SYSTEM_PROPERTIES
    };

    result =
        xrGetSystemProperties(
            g_vrInstance,
            g_vrSystemId,
            &systemProperties);

    if (XR_FAILED(result))
    {
        VR_LogXrFailure(
            "xrGetSystemProperties",
            result);

        VR_Shutdown();
        return false;
    }

    Com_Printf(
        0,
        "[VR] OpenXR system: %s.\n",
        systemProperties.systemName);

    if (!VR_CreateControllerActions())
    {
        Com_PrintWarning(
            0,
            "[VR] Controller action setup failed. "
            "Rendering will continue without "
            "controller tracking.\n");
    }

    if (!VR_CreateSession())
    {
        VR_Shutdown();
        return false;
    }

    if (!VR_CreateSwapchains())
    {
        VR_Shutdown();
        return false;
    }

    if (!VR_CreateHeadTrackedScene())
    {
        VR_Shutdown();
        return false;
    }

    if (!VR_CreateCapturedFrameBlitResources())
    {
        VR_Shutdown();
        return false;
    }

    if (!VR_CreateControllerProxyResources())
    {
        Com_PrintWarning(
            0,
            "[VR] Controller proxy renderer could "
            "not be created. Controller tracking "
            "will continue without visible markers.\n");
    }

    VR_D3D9CaptureSetEnabled(true);

    g_vrInitialized = true;

    Com_Printf(
        0,
        "[VR] OpenXR D3D11 session and swapchains are ready.\n");

    return true;
}

void VR_Frame()
{
    if (!g_vrInitialized ||
        g_vrInstance == XR_NULL_HANDLE ||
        g_vrSession == XR_NULL_HANDLE)
    {
        return;
    }

    VR_PollEvents();

    if (!g_vrSessionRunning ||
        g_vrExitRequested)
    {
        return;
    }

    XrFrameWaitInfo waitInfo{XR_TYPE_FRAME_WAIT_INFO};
    XrFrameState frameState{XR_TYPE_FRAME_STATE};

    XrResult result =
        xrWaitFrame(
            g_vrSession,
            &waitInfo,
            &frameState);

    if (XR_FAILED(result))
    {
        VR_LogXrFailure("xrWaitFrame", result);
        return;
    }

    XrFrameBeginInfo beginInfo{XR_TYPE_FRAME_BEGIN_INFO};

    result =
        xrBeginFrame(g_vrSession, &beginInfo);

    if (XR_FAILED(result))
    {
        VR_LogXrFailure("xrBeginFrame", result);
        return;
    }

    XrCompositionLayerProjection projectionLayer{
        XR_TYPE_COMPOSITION_LAYER_PROJECTION
    };

    std::vector<XrCompositionLayerProjectionView>
        projectionViews;

    bool submittedProjectionLayer = false;

    if (frameState.shouldRender)
    {
        submittedProjectionLayer =
            VR_RenderSolidColorFrame(
                frameState,
                projectionLayer,
                projectionViews);
    }

    const XrCompositionLayerBaseHeader* layers[1] = {};

    uint32_t layerCount = 0;

    if (submittedProjectionLayer)
    {
        layers[0] =
            reinterpret_cast<
                const XrCompositionLayerBaseHeader*>(
                    &projectionLayer);

        layerCount = 1;
    }

    XrFrameEndInfo endInfo{XR_TYPE_FRAME_END_INFO};

    endInfo.displayTime =
        frameState.predictedDisplayTime;
    endInfo.environmentBlendMode = g_vrBlendMode;
    endInfo.layerCount = layerCount;
    endInfo.layers =
        layerCount > 0
            ? layers
            : nullptr;

    result =
        xrEndFrame(g_vrSession, &endInfo);

    if (XR_FAILED(result))
    {
        VR_LogXrFailure("xrEndFrame", result);
    }
}

void VR_Shutdown()
{
    VR_ResetHeadOrientation();
    if (g_vrInstance == XR_NULL_HANDLE &&
        g_vrSession == XR_NULL_HANDLE)
    {
        VR_ResetState();
        return;
    }

    Com_Printf(
        0,
        "[VR] Shutting down OpenXR D3D11 subsystem...\n");

    VR_D3D9CaptureSetEnabled(false);

    g_vrSessionRunning = false;

    VR_DestroyControllerInput();

    for (VrEyeSwapchain& eyeSwapchain :
         g_vrEyeSwapchains)
    {
        eyeSwapchain.depthStencilViews.clear();
        eyeSwapchain.depthTextures.clear();
        eyeSwapchain.renderTargetViews.clear();
        eyeSwapchain.images.clear();

        if (eyeSwapchain.handle != XR_NULL_HANDLE)
        {
            xrDestroySwapchain(eyeSwapchain.handle);
            eyeSwapchain.handle = XR_NULL_HANDLE;
        }
    }

    g_vrEyeSwapchains.clear();
    g_vrViews.clear();
    g_vrViewConfigs.clear();

    if (g_vrAppSpace != XR_NULL_HANDLE)
    {
        xrDestroySpace(g_vrAppSpace);
        g_vrAppSpace = XR_NULL_HANDLE;
    }

    if (g_vrSession != XR_NULL_HANDLE)
    {
        xrDestroySession(g_vrSession);
        g_vrSession = XR_NULL_HANDLE;
    }

    if (g_vrD3dContext)
    {
        g_vrD3dContext->ClearState();
        g_vrD3dContext->Flush();
    }

    g_vrCapturedStereoView.Reset();
    g_vrCapturedStereoTexture.Reset();

    g_vrBlitSampler.Reset();

    for (auto& vertexBuffer :
         g_vrBlitVertexBuffers)
    {
        vertexBuffer.Reset();
    }

    g_vrBlitInputLayout.Reset();
    g_vrBlitPixelShader.Reset();
    g_vrBlitVertexShader.Reset();

    g_vrCapturedStereoWidth = 0u;
    g_vrCapturedStereoHeight = 0u;
    g_vrUploadedStereoSerial = 0u;
    g_vrLoggedFirstStereoUpload = false;

    g_vrControllerProxyVertexBuffer.Reset();
    g_vrControllerProxyInputLayout.Reset();
    g_vrControllerProxyPixelShader.Reset();
    g_vrControllerProxyVertexShader.Reset();
    g_vrControllerProxyResourcesReady = false;
    g_vrLoggedFirstControllerProxyDraw = false;
    g_vrControllerRenderPoses = {};

    g_vrTestDepthStencilState.Reset();
    g_vrTestRasterizerState.Reset();
    g_vrTestConstantBuffer.Reset();
    g_vrGridVertexBuffer.Reset();
    g_vrTestIndexBuffer.Reset();
    g_vrTestVertexBuffer.Reset();
    g_vrTestInputLayout.Reset();
    g_vrTestPixelShader.Reset();
    g_vrTestVertexShader.Reset();

    g_vrTestIndexCount = 0;
    g_vrGridVertexCount = 0;
    g_vrLoggedFirstTestFrame = false;

    g_vrD3dContext.Reset();
    g_vrD3dDevice.Reset();

    if (g_vrInstance != XR_NULL_HANDLE)
    {
        xrDestroyInstance(g_vrInstance);
        g_vrInstance = XR_NULL_HANDLE;
    }

    VR_ResetState();
}

bool VR_IsInitialized()
{
    return g_vrInitialized;
}
