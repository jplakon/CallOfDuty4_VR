#include "vr/vr_input_bindings.h"
#include "vr/vr_openvr_input.h"
#include "vr/vr_openxr_profiles.h"

#include <Windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <shellapi.h>
#include <wrl/client.h>

#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

namespace input = kisak::vr::input;
using Microsoft::WRL::ComPtr;

namespace
{

constexpr std::uint64_t kDefaultTimeoutMilliseconds = 45000u;

enum class CaptureBackend
{
    Automatic,
    OpenXr,
    OpenVr,
};

struct Options
{
    input::ValueType valueType = input::ValueType::Boolean;
    CaptureBackend backend = CaptureBackend::Automatic;
    std::filesystem::path outputPath;
    std::uint64_t timeoutMilliseconds =
        kDefaultTimeoutMilliseconds;
};

struct CaptureAction
{
    input::Source source = input::Source::Unbound;
    XrAction action = XR_NULL_HANDLE;
    XrPath handPath = XR_NULL_PATH;
    bool initialized = false;
    bool previousBoolean = false;
    float previousMagnitude = 0.0f;
};

struct CaptureResult
{
    bool success = false;
    input::Source source = input::Source::Unbound;
    std::string profile;
    std::string localizedName;
    std::string error;
    bool runtimeFallbackAllowed = false;
};

std::string SanitizeResultValue(std::string value)
{
    std::replace(value.begin(), value.end(), '\r', ' ');
    std::replace(value.begin(), value.end(), '\n', ' ');
    return value;
}

bool WriteResult(
    const std::filesystem::path& outputPath,
    const CaptureResult& result)
{
    if (outputPath.empty())
    {
        return false;
    }

    std::error_code error;
    std::filesystem::create_directories(
        outputPath.parent_path(),
        error);

    std::ofstream output(
        outputPath,
        std::ios::binary | std::ios::trunc);

    if (!output)
    {
        return false;
    }

    output << "status=" << (result.success ? "success" : "error")
           << "\r\n";

    if (result.success)
    {
        output << "source=" << input::SourceId(result.source) << "\r\n";
        output << "profile="
               << SanitizeResultValue(result.profile) << "\r\n";
        output << "localized="
               << SanitizeResultValue(result.localizedName) << "\r\n";
    }
    else
    {
        output << "message="
               << SanitizeResultValue(result.error) << "\r\n";
    }

    return static_cast<bool>(output);
}

bool ParseUnsigned(
    const wchar_t* text,
    std::uint64_t* value)
{
    if (text == nullptr || value == nullptr || text[0] == L'\0')
    {
        return false;
    }

    wchar_t* end = nullptr;
    const unsigned long long parsed =
        std::wcstoull(text, &end, 10);

    if (end == text || end == nullptr || *end != L'\0')
    {
        return false;
    }

    *value = static_cast<std::uint64_t>(parsed);
    return true;
}

bool ParseOptions(
    const int argumentCount,
    wchar_t** arguments,
    Options* options,
    std::string* error)
{
    if (options == nullptr || error == nullptr)
    {
        return false;
    }

    for (int index = 1; index < argumentCount; ++index)
    {
        const std::wstring_view argument(arguments[index]);

        if (argument == L"--capture" && index + 1 < argumentCount)
        {
            const std::wstring_view value(arguments[++index]);
            if (value == L"boolean")
            {
                options->valueType = input::ValueType::Boolean;
            }
            else if (value == L"vector2")
            {
                options->valueType = input::ValueType::Vector2;
            }
            else
            {
                *error = "--capture must be boolean or vector2.";
                return false;
            }
            continue;
        }

        if (argument == L"--output" && index + 1 < argumentCount)
        {
            options->outputPath = arguments[++index];
            continue;
        }

        if (argument == L"--backend" && index + 1 < argumentCount)
        {
            const std::wstring_view value(arguments[++index]);
            if (value == L"auto")
            {
                options->backend = CaptureBackend::Automatic;
            }
            else if (value == L"openxr")
            {
                options->backend = CaptureBackend::OpenXr;
            }
            else if (value == L"openvr")
            {
                options->backend = CaptureBackend::OpenVr;
            }
            else
            {
                *error = "--backend must be auto, openxr, or openvr.";
                return false;
            }
            continue;
        }

        if (argument == L"--timeout-ms" && index + 1 < argumentCount)
        {
            if (!ParseUnsigned(
                    arguments[++index],
                    &options->timeoutMilliseconds) ||
                options->timeoutMilliseconds < 5000u ||
                options->timeoutMilliseconds > 120000u)
            {
                *error = "--timeout-ms must be from 5000 through 120000.";
                return false;
            }
            continue;
        }

        *error = "Unknown or incomplete command-line option.";
        return false;
    }

    if (options->outputPath.empty())
    {
        *error = "--output is required.";
        return false;
    }

    return true;
}

class MapperRuntime
{
public:
    ~MapperRuntime()
    {
        Shutdown();
    }

    CaptureResult Capture(const Options& options)
    {
        CaptureResult result;

        if (!CreateInstance(&result.error) ||
            !CreateSystemAndDevice(&result.error) ||
            !CreateActions(options.valueType, &result.error) ||
            !SuggestBindings(&result.error) ||
            !CreateSession(&result.error))
        {
            result.runtimeFallbackAllowed = true;
            return result;
        }

        return RunCaptureLoop(options);
    }

private:
    XrInstance instance_ = XR_NULL_HANDLE;
    XrSystemId systemId_ = XR_NULL_SYSTEM_ID;
    XrSession session_ = XR_NULL_HANDLE;
    XrActionSet actionSet_ = XR_NULL_HANDLE;
    XrSessionState sessionState_ = XR_SESSION_STATE_UNKNOWN;
    bool sessionRunning_ = false;
    bool exitRequested_ = false;
    XrEnvironmentBlendMode blendMode_ =
        XR_ENVIRONMENT_BLEND_MODE_OPAQUE;

    std::array<XrPath, 2> handPaths_ = {
        XR_NULL_PATH,
        XR_NULL_PATH,
    };

    input::ValueType captureValueType_ =
        input::ValueType::Boolean;

    std::array<bool, input::kOpenXrProfileCount>
        enabledProfiles_ = {};

    std::vector<CaptureAction> captureActions_;
    ComPtr<IDXGIAdapter> adapter_;
    ComPtr<ID3D11Device> device_;
    ComPtr<ID3D11DeviceContext> context_;

    const char* ResultName(const XrResult result) const
    {
        static thread_local std::array<char, XR_MAX_RESULT_STRING_SIZE>
            resultText = {};

        resultText.fill('\0');

        if (instance_ != XR_NULL_HANDLE &&
            XR_SUCCEEDED(
                xrResultToString(
                    instance_,
                    result,
                    resultText.data())))
        {
            return resultText.data();
        }

        std::snprintf(
            resultText.data(),
            resultText.size(),
            "XrResult %d",
            static_cast<int>(result));

        return resultText.data();
    }

    bool FailXr(
        const char* operation,
        const XrResult result,
        std::string* error) const
    {
        if (error != nullptr)
        {
            *error = std::string(operation) + " failed: " +
                ResultName(result) + ".";
        }
        return false;
    }

    bool StringToPath(
        const char* text,
        XrPath* path,
        std::string* error) const
    {
        const XrResult result =
            xrStringToPath(instance_, text, path);

        return XR_SUCCEEDED(result)
            ? true
            : FailXr("xrStringToPath", result, error);
    }

    bool CreateInstance(std::string* error)
    {
        std::uint32_t extensionCount = 0u;
        XrResult result =
            xrEnumerateInstanceExtensionProperties(
                nullptr,
                0u,
                &extensionCount,
                nullptr);

        if (XR_FAILED(result))
        {
            return FailXr(
                "xrEnumerateInstanceExtensionProperties",
                result,
                error);
        }

        std::vector<XrExtensionProperties> extensions(extensionCount);
        for (XrExtensionProperties& extension : extensions)
        {
            extension = XrExtensionProperties{
                XR_TYPE_EXTENSION_PROPERTIES
            };
        }

        result = xrEnumerateInstanceExtensionProperties(
            nullptr,
            extensionCount,
            &extensionCount,
            extensions.data());

        if (XR_FAILED(result))
        {
            return FailXr(
                "xrEnumerateInstanceExtensionProperties",
                result,
                error);
        }

        const auto hasExtension = [&](const char* name)
        {
            return std::any_of(
                extensions.begin(),
                extensions.end(),
                [&](const XrExtensionProperties& extension)
                {
                    return std::strcmp(
                               extension.extensionName,
                               name) == 0;
                });
        };

        if (!hasExtension(XR_KHR_D3D11_ENABLE_EXTENSION_NAME))
        {
            *error = "The active OpenXR runtime does not support D3D11.";
            return false;
        }

        std::vector<const char*> enabledExtensions = {
            XR_KHR_D3D11_ENABLE_EXTENSION_NAME,
        };

        enabledProfiles_.fill(false);

        for (const input::OpenXrProfileDefinition& profile :
             input::OpenXrProfileDefinitions())
        {
            bool enabled = profile.requiredExtension == nullptr;

            if (!enabled && hasExtension(profile.requiredExtension))
            {
                const bool duplicate =
                    std::any_of(
                        enabledExtensions.begin(),
                        enabledExtensions.end(),
                        [&](const char* extension)
                        {
                            return std::strcmp(
                                       extension,
                                       profile.requiredExtension) == 0;
                        });

                if (!duplicate)
                {
                    enabledExtensions.push_back(
                        profile.requiredExtension);
                }
                enabled = true;
            }

            enabledProfiles_[
                static_cast<std::size_t>(profile.profile)] =
                enabled;
        }

        XrInstanceCreateInfo createInfo{
            XR_TYPE_INSTANCE_CREATE_INFO
        };

        std::snprintf(
            createInfo.applicationInfo.applicationName,
            XR_MAX_APPLICATION_NAME_SIZE,
            "%s",
            "KisakCOD VR Input Mapper");

        createInfo.applicationInfo.applicationVersion = 1u;

        std::snprintf(
            createInfo.applicationInfo.engineName,
            XR_MAX_ENGINE_NAME_SIZE,
            "%s",
            "KisakCOD");

        createInfo.applicationInfo.engineVersion = 1u;
        createInfo.applicationInfo.apiVersion =
            XR_MAKE_VERSION(1, 0, 0);
        createInfo.enabledExtensionCount =
            static_cast<std::uint32_t>(
                enabledExtensions.size());
        createInfo.enabledExtensionNames =
            enabledExtensions.data();

        result = xrCreateInstance(&createInfo, &instance_);

        return XR_SUCCEEDED(result)
            ? true
            : FailXr("xrCreateInstance", result, error);
    }

    bool CreateSystemAndDevice(std::string* error)
    {
        XrSystemGetInfo systemInfo{
            XR_TYPE_SYSTEM_GET_INFO
        };
        systemInfo.formFactor =
            XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;

        XrResult result =
            xrGetSystem(instance_, &systemInfo, &systemId_);

        if (XR_FAILED(result))
        {
            return FailXr("xrGetSystem", result, error);
        }

        PFN_xrGetD3D11GraphicsRequirementsKHR getRequirements = nullptr;

        result = xrGetInstanceProcAddr(
            instance_,
            "xrGetD3D11GraphicsRequirementsKHR",
            reinterpret_cast<PFN_xrVoidFunction*>(&getRequirements));

        if (XR_FAILED(result) || getRequirements == nullptr)
        {
            return FailXr(
                "xrGetInstanceProcAddr(xrGetD3D11GraphicsRequirementsKHR)",
                result,
                error);
        }

        XrGraphicsRequirementsD3D11KHR requirements{
            XR_TYPE_GRAPHICS_REQUIREMENTS_D3D11_KHR
        };

        result = getRequirements(
            instance_,
            systemId_,
            &requirements);

        if (XR_FAILED(result))
        {
            return FailXr(
                "xrGetD3D11GraphicsRequirementsKHR",
                result,
                error);
        }

        ComPtr<IDXGIFactory1> factory;
        HRESULT hr = CreateDXGIFactory1(
            IID_PPV_ARGS(factory.GetAddressOf()));

        if (FAILED(hr))
        {
            *error = "CreateDXGIFactory1 failed.";
            return false;
        }

        for (UINT index = 0u; ; ++index)
        {
            ComPtr<IDXGIAdapter1> candidate;
            if (factory->EnumAdapters1(
                    index,
                    candidate.GetAddressOf()) == DXGI_ERROR_NOT_FOUND)
            {
                break;
            }

            DXGI_ADAPTER_DESC1 description = {};
            if (FAILED(candidate->GetDesc1(&description)))
            {
                continue;
            }

            if (description.AdapterLuid.LowPart ==
                    requirements.adapterLuid.LowPart &&
                description.AdapterLuid.HighPart ==
                    requirements.adapterLuid.HighPart)
            {
                candidate.As(&adapter_);
                break;
            }
        }

        if (adapter_ == nullptr)
        {
            *error = "Could not find the graphics adapter required by OpenXR.";
            return false;
        }

        // Request the runtime's exact minimum. This also stays compatible
        // with projects that use the June 2010 DirectX SDK headers, which do
        // not define the later D3D_FEATURE_LEVEL_11_1 enumerator.
        D3D_FEATURE_LEVEL requestedFeatureLevel =
            requirements.minFeatureLevel;
        D3D_FEATURE_LEVEL selectedFeatureLevel =
            D3D_FEATURE_LEVEL_10_0;

        hr = D3D11CreateDevice(
            adapter_.Get(),
            D3D_DRIVER_TYPE_UNKNOWN,
            nullptr,
            D3D11_CREATE_DEVICE_BGRA_SUPPORT,
            &requestedFeatureLevel,
            1u,
            D3D11_SDK_VERSION,
            device_.GetAddressOf(),
            &selectedFeatureLevel,
            context_.GetAddressOf());

        if (FAILED(hr) || device_ == nullptr)
        {
            *error = "D3D11CreateDevice failed on the OpenXR adapter.";
            return false;
        }

        return true;
    }

    bool CreateActions(
        const input::ValueType valueType,
        std::string* error)
    {
        captureValueType_ = valueType;

        if (!StringToPath(
                "/user/hand/left",
                &handPaths_[0],
                error) ||
            !StringToPath(
                "/user/hand/right",
                &handPaths_[1],
                error))
        {
            return false;
        }

        XrActionSetCreateInfo actionSetInfo{
            XR_TYPE_ACTION_SET_CREATE_INFO
        };

        std::snprintf(
            actionSetInfo.actionSetName,
            XR_MAX_ACTION_SET_NAME_SIZE,
            "%s",
            "capture");

        std::snprintf(
            actionSetInfo.localizedActionSetName,
            XR_MAX_LOCALIZED_ACTION_SET_NAME_SIZE,
            "%s",
            "KisakCOD Binding Capture");

        XrResult result =
            xrCreateActionSet(
                instance_,
                &actionSetInfo,
                &actionSet_);

        if (XR_FAILED(result))
        {
            return FailXr("xrCreateActionSet", result, error);
        }

        for (const input::SourceDefinition& source :
             input::SourceDefinitions())
        {
            const bool booleanInput =
                valueType == input::ValueType::Boolean &&
                source.valueType == input::ValueType::Boolean &&
                !input::IsDirectionalSource(source.source);
            const bool directionAxis =
                valueType == input::ValueType::Boolean &&
                (source.source == input::Source::LeftPrimaryAxis ||
                 source.source == input::Source::RightPrimaryAxis);
            const bool vectorInput =
                valueType == input::ValueType::Vector2 &&
                source.valueType == input::ValueType::Vector2;

            if (source.source == input::Source::Unbound ||
                source.hand == input::Hand::None ||
                (!booleanInput && !directionAxis && !vectorInput))
            {
                continue;
            }

            CaptureAction capture;
            capture.source = source.source;
            capture.handPath = source.hand == input::Hand::Left
                ? handPaths_[0]
                : handPaths_[1];

            std::string actionName = "capture_";
            actionName += source.id;
            std::replace(
                actionName.begin(),
                actionName.end(),
                '.',
                '_');

            XrActionCreateInfo createInfo{
                XR_TYPE_ACTION_CREATE_INFO
            };

            createInfo.actionType =
                source.valueType == input::ValueType::Vector2
                    ? XR_ACTION_TYPE_VECTOR2F_INPUT
                    : XR_ACTION_TYPE_BOOLEAN_INPUT;

            std::snprintf(
                createInfo.actionName,
                XR_MAX_ACTION_NAME_SIZE,
                "%s",
                actionName.c_str());

            std::snprintf(
                createInfo.localizedActionName,
                XR_MAX_LOCALIZED_ACTION_NAME_SIZE,
                "%s",
                source.label);

            createInfo.countSubactionPaths = 1u;
            createInfo.subactionPaths = &capture.handPath;

            result = xrCreateAction(
                actionSet_,
                &createInfo,
                &capture.action);

            if (XR_FAILED(result))
            {
                return FailXr("xrCreateAction", result, error);
            }

            captureActions_.push_back(capture);
        }

        if (captureActions_.empty())
        {
            *error = "No compatible input sources were available to capture.";
            return false;
        }

        return true;
    }

    bool SuggestBindings(std::string* error)
    {
        std::uint32_t acceptedProfiles = 0u;

        for (const input::OpenXrProfileDefinition& profile :
             input::OpenXrProfileDefinitions())
        {
            const std::size_t profileIndex =
                static_cast<std::size_t>(profile.profile);

            if (!enabledProfiles_[profileIndex])
            {
                continue;
            }

            XrPath profilePath = XR_NULL_PATH;
            if (!StringToPath(profile.path, &profilePath, error))
            {
                return false;
            }

            std::vector<XrActionSuggestedBinding> bindings;

            for (const CaptureAction& capture : captureActions_)
            {
                const char* const component =
                    input::ResolveOpenXrComponent(
                        profile.profile,
                        capture.source);

                if (component == nullptr)
                {
                    continue;
                }

                const input::Hand hand =
                    input::GetSourceDefinition(capture.source).hand;

                std::array<char, 128> pathText = {};
                std::snprintf(
                    pathText.data(),
                    pathText.size(),
                    "/user/hand/%s%s",
                    hand == input::Hand::Left ? "left" : "right",
                    component);

                XrPath componentPath = XR_NULL_PATH;
                if (!StringToPath(
                        pathText.data(),
                        &componentPath,
                        error))
                {
                    return false;
                }

                bindings.push_back({
                    capture.action,
                    componentPath,
                });
            }

            if (bindings.empty())
            {
                continue;
            }

            XrInteractionProfileSuggestedBinding suggestion{
                XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING
            };

            suggestion.interactionProfile = profilePath;
            suggestion.countSuggestedBindings =
                static_cast<std::uint32_t>(bindings.size());
            suggestion.suggestedBindings = bindings.data();

            const XrResult result =
                xrSuggestInteractionProfileBindings(
                    instance_,
                    &suggestion);

            if (XR_SUCCEEDED(result))
            {
                ++acceptedProfiles;
            }
        }

        if (acceptedProfiles == 0u)
        {
            *error = "The OpenXR runtime did not accept any supported controller profile.";
            return false;
        }

        return true;
    }

    bool CreateSession(std::string* error)
    {
        XrGraphicsBindingD3D11KHR graphicsBinding{
            XR_TYPE_GRAPHICS_BINDING_D3D11_KHR
        };
        graphicsBinding.device = device_.Get();

        XrSessionCreateInfo sessionInfo{
            XR_TYPE_SESSION_CREATE_INFO
        };
        sessionInfo.next = &graphicsBinding;
        sessionInfo.systemId = systemId_;

        XrResult result =
            xrCreateSession(
                instance_,
                &sessionInfo,
                &session_);

        if (XR_FAILED(result))
        {
            return FailXr("xrCreateSession", result, error);
        }

        XrSessionActionSetsAttachInfo attachInfo{
            XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO
        };
        attachInfo.countActionSets = 1u;
        attachInfo.actionSets = &actionSet_;

        result = xrAttachSessionActionSets(
            session_,
            &attachInfo);

        if (XR_FAILED(result))
        {
            return FailXr(
                "xrAttachSessionActionSets",
                result,
                error);
        }

        std::uint32_t blendModeCount = 0u;
        result = xrEnumerateEnvironmentBlendModes(
            instance_,
            systemId_,
            XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
            0u,
            &blendModeCount,
            nullptr);

        if (XR_SUCCEEDED(result) && blendModeCount > 0u)
        {
            std::vector<XrEnvironmentBlendMode> modes(blendModeCount);
            result = xrEnumerateEnvironmentBlendModes(
                instance_,
                systemId_,
                XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
                blendModeCount,
                &blendModeCount,
                modes.data());

            if (XR_SUCCEEDED(result) && !modes.empty())
            {
                blendMode_ = modes.front();
            }
        }

        return true;
    }

    void PollEvents()
    {
        XrEventDataBuffer event{
            XR_TYPE_EVENT_DATA_BUFFER
        };

        while (xrPollEvent(instance_, &event) == XR_SUCCESS)
        {
            if (event.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED)
            {
                const auto* changed =
                    reinterpret_cast<
                        const XrEventDataSessionStateChanged*>(&event);

                sessionState_ = changed->state;

                if (sessionState_ == XR_SESSION_STATE_READY &&
                    !sessionRunning_)
                {
                    XrSessionBeginInfo beginInfo{
                        XR_TYPE_SESSION_BEGIN_INFO
                    };
                    beginInfo.primaryViewConfigurationType =
                        XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;

                    if (XR_SUCCEEDED(
                            xrBeginSession(
                                session_,
                                &beginInfo)))
                    {
                        sessionRunning_ = true;
                    }
                }
                else if (sessionState_ == XR_SESSION_STATE_STOPPING &&
                         sessionRunning_)
                {
                    xrEndSession(session_);
                    sessionRunning_ = false;
                    exitRequested_ = true;
                }
                else if (
                    sessionState_ == XR_SESSION_STATE_EXITING ||
                    sessionState_ == XR_SESSION_STATE_LOSS_PENDING)
                {
                    exitRequested_ = true;
                }
            }
            else if (event.type ==
                     XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING)
            {
                exitRequested_ = true;
            }

            event = XrEventDataBuffer{
                XR_TYPE_EVENT_DATA_BUFFER
            };
        }
    }

    bool SyncAndDetect(input::Source* captured)
    {
        XrActiveActionSet activeSet = {};
        activeSet.actionSet = actionSet_;
        activeSet.subactionPath = XR_NULL_PATH;

        XrActionsSyncInfo syncInfo{
            XR_TYPE_ACTIONS_SYNC_INFO
        };
        syncInfo.countActiveActionSets = 1u;
        syncInfo.activeActionSets = &activeSet;

        const XrResult syncResult =
            xrSyncActions(session_, &syncInfo);

        if (XR_FAILED(syncResult))
        {
            return false;
        }

        for (CaptureAction& capture : captureActions_)
        {
            if (input::GetSourceDefinition(capture.source).valueType ==
                input::ValueType::Boolean)
            {
                XrActionStateGetInfo getInfo{
                    XR_TYPE_ACTION_STATE_GET_INFO
                };
                getInfo.action = capture.action;
                getInfo.subactionPath = capture.handPath;

                XrActionStateBoolean state{
                    XR_TYPE_ACTION_STATE_BOOLEAN
                };

                if (XR_FAILED(
                        xrGetActionStateBoolean(
                            session_,
                            &getInfo,
                            &state)))
                {
                    continue;
                }

                const bool pressed =
                    state.isActive == XR_TRUE &&
                    state.currentState == XR_TRUE;

                if (capture.initialized &&
                    pressed &&
                    !capture.previousBoolean)
                {
                    *captured = capture.source;
                    return true;
                }

                capture.initialized = true;
                capture.previousBoolean = pressed;
            }
            else
            {
                XrActionStateGetInfo getInfo{
                    XR_TYPE_ACTION_STATE_GET_INFO
                };
                getInfo.action = capture.action;
                getInfo.subactionPath = capture.handPath;

                XrActionStateVector2f state{
                    XR_TYPE_ACTION_STATE_VECTOR2F
                };

                if (XR_FAILED(
                        xrGetActionStateVector2f(
                            session_,
                            &getInfo,
                            &state)))
                {
                    continue;
                }

                const float magnitude =
                    state.isActive == XR_TRUE
                        ? std::sqrt(
                              state.currentState.x * state.currentState.x +
                              state.currentState.y * state.currentState.y)
                        : 0.0f;

                if (capture.initialized &&
                    magnitude >= 0.65f &&
                    capture.previousMagnitude <= 0.25f)
                {
                    if (captureValueType_ == input::ValueType::Boolean)
                    {
                        const input::Source direction =
                            input::DirectionalSource(
                                capture.source,
                                state.currentState.x,
                                state.currentState.y,
                                0.65f,
                                0.08f);
                        if (direction != input::Source::Unbound)
                        {
                            *captured = direction;
                            return true;
                        }
                    }
                    else
                    {
                        *captured = capture.source;
                        return true;
                    }
                }

                capture.initialized = true;
                capture.previousMagnitude = magnitude;
            }
        }

        return false;
    }

    std::string PathToString(const XrPath path) const
    {
        if (path == XR_NULL_PATH)
        {
            return {};
        }

        std::uint32_t required = 0u;
        XrResult result =
            xrPathToString(
                instance_,
                path,
                0u,
                &required,
                nullptr);

        if (XR_FAILED(result) || required == 0u)
        {
            return {};
        }

        std::string value(required, '\0');
        result = xrPathToString(
            instance_,
            path,
            required,
            &required,
            value.data());

        if (XR_FAILED(result))
        {
            return {};
        }

        if (!value.empty() && value.back() == '\0')
        {
            value.pop_back();
        }

        return value;
    }

    void DescribeCapture(
        const input::Source source,
        CaptureResult* result)
    {
        const input::SourceDefinition& definition =
            input::GetSourceDefinition(source);

        const XrPath handPath = definition.hand == input::Hand::Left
            ? handPaths_[0]
            : handPaths_[1];

        XrInteractionProfileState profileState{
            XR_TYPE_INTERACTION_PROFILE_STATE
        };

        if (XR_SUCCEEDED(
                xrGetCurrentInteractionProfile(
                    session_,
                    handPath,
                    &profileState)))
        {
            result->profile =
                PathToString(profileState.interactionProfile);
        }

        const auto action = std::find_if(
            captureActions_.begin(),
            captureActions_.end(),
            [&](const CaptureAction& candidate)
            {
                return candidate.source ==
                    input::PhysicalSource(source);
            });

        if (action == captureActions_.end())
        {
            result->localizedName = definition.label;
            return;
        }

        XrBoundSourcesForActionEnumerateInfo enumerateInfo{
            XR_TYPE_BOUND_SOURCES_FOR_ACTION_ENUMERATE_INFO
        };
        enumerateInfo.action = action->action;

        std::uint32_t sourceCount = 0u;
        XrResult xrResult = xrEnumerateBoundSourcesForAction(
            session_,
            &enumerateInfo,
            0u,
            &sourceCount,
            nullptr);

        if (XR_FAILED(xrResult) || sourceCount == 0u)
        {
            result->localizedName = definition.label;
            return;
        }

        std::vector<XrPath> sources(sourceCount);
        xrResult = xrEnumerateBoundSourcesForAction(
            session_,
            &enumerateInfo,
            sourceCount,
            &sourceCount,
            sources.data());

        if (XR_FAILED(xrResult) || sources.empty())
        {
            result->localizedName = definition.label;
            return;
        }

        XrInputSourceLocalizedNameGetInfo nameInfo{
            XR_TYPE_INPUT_SOURCE_LOCALIZED_NAME_GET_INFO
        };
        nameInfo.sourcePath = sources.front();
        nameInfo.whichComponents =
            XR_INPUT_SOURCE_LOCALIZED_NAME_USER_PATH_BIT |
            XR_INPUT_SOURCE_LOCALIZED_NAME_INTERACTION_PROFILE_BIT |
            XR_INPUT_SOURCE_LOCALIZED_NAME_COMPONENT_BIT;

        std::uint32_t required = 0u;
        xrResult = xrGetInputSourceLocalizedName(
            session_,
            &nameInfo,
            0u,
            &required,
            nullptr);

        if (XR_FAILED(xrResult) || required == 0u)
        {
            result->localizedName = definition.label;
            return;
        }

        std::string localized(required, '\0');
        xrResult = xrGetInputSourceLocalizedName(
            session_,
            &nameInfo,
            required,
            &required,
            localized.data());

        if (XR_FAILED(xrResult))
        {
            result->localizedName = definition.label;
            return;
        }

        if (!localized.empty() && localized.back() == '\0')
        {
            localized.pop_back();
        }

        result->localizedName = localized;
    }

    CaptureResult RunCaptureLoop(const Options& options)
    {
        CaptureResult result;
        const ULONGLONG started = GetTickCount64();

        while (!exitRequested_)
        {
            PollEvents();

            if (GetAsyncKeyState(VK_ESCAPE) < 0)
            {
                result.error = "Binding capture was cancelled.";
                return result;
            }

            if (GetTickCount64() - started >=
                options.timeoutMilliseconds)
            {
                result.error =
                    "No controller input was detected before the capture timed out.";
                return result;
            }

            if (!sessionRunning_)
            {
                Sleep(10u);
                continue;
            }

            XrFrameWaitInfo waitInfo{
                XR_TYPE_FRAME_WAIT_INFO
            };
            XrFrameState frameState{
                XR_TYPE_FRAME_STATE
            };

            XrResult xrResult =
                xrWaitFrame(
                    session_,
                    &waitInfo,
                    &frameState);

            if (XR_FAILED(xrResult))
            {
                result.error = std::string("xrWaitFrame failed: ") +
                    ResultName(xrResult) + ".";
                return result;
            }

            XrFrameBeginInfo beginInfo{
                XR_TYPE_FRAME_BEGIN_INFO
            };

            xrResult = xrBeginFrame(session_, &beginInfo);
            if (XR_FAILED(xrResult))
            {
                result.error = std::string("xrBeginFrame failed: ") +
                    ResultName(xrResult) + ".";
                return result;
            }

            input::Source captured = input::Source::Unbound;
            const bool detected = SyncAndDetect(&captured);

            XrFrameEndInfo endInfo{
                XR_TYPE_FRAME_END_INFO
            };
            endInfo.displayTime = frameState.predictedDisplayTime;
            endInfo.environmentBlendMode = blendMode_;
            endInfo.layerCount = 0u;
            endInfo.layers = nullptr;

            xrEndFrame(session_, &endInfo);

            if (detected)
            {
                result.success = true;
                result.source = captured;
                DescribeCapture(captured, &result);
                return result;
            }
        }

        result.error = "The OpenXR runtime closed the capture session.";
        return result;
    }

    void Shutdown()
    {
        if (session_ != XR_NULL_HANDLE)
        {
            xrDestroySession(session_);
            session_ = XR_NULL_HANDLE;
        }

        if (actionSet_ != XR_NULL_HANDLE)
        {
            xrDestroyActionSet(actionSet_);
            actionSet_ = XR_NULL_HANDLE;
        }

        if (instance_ != XR_NULL_HANDLE)
        {
            xrDestroyInstance(instance_);
            instance_ = XR_NULL_HANDLE;
        }

        context_.Reset();
        device_.Reset();
        adapter_.Reset();
    }
};

class OpenVrMapperRuntime
{
public:
    ~OpenVrMapperRuntime()
    {
        if (system_ != nullptr)
        {
            vr::VR_Shutdown();
            system_ = nullptr;
            compositor_ = nullptr;
        }
    }

    CaptureResult Capture(const Options& options)
    {
        CaptureResult result;

        vr::EVRInitError initError = vr::VRInitError_None;
        system_ = vr::VR_Init(
            &initError,
            vr::VRApplication_Scene,
            "KisakCOD VR Input Mapper V57");

        if (initError != vr::VRInitError_None || system_ == nullptr)
        {
            result.error = "SteamVR input capture could not start: ";
            result.error +=
                vr::VR_GetVRInitErrorAsEnglishDescription(initError);
            result.error += ".";
            return result;
        }

        compositor_ = vr::VRCompositor();

        std::array<bool, input::kSourceCount> initialized = {};
        std::array<bool, input::kSourceCount> previousBoolean = {};
        std::array<float, input::kSourceCount> previousMagnitude = {};

        const ULONGLONG started = GetTickCount64();

        while (true)
        {
            if (GetAsyncKeyState(VK_ESCAPE) < 0)
            {
                result.error = "Binding capture was cancelled.";
                return result;
            }

            if (GetTickCount64() - started >=
                options.timeoutMilliseconds)
            {
                result.error =
                    "No SteamVR controller input was detected before the capture timed out.";
                return result;
            }

            vr::VREvent_t event = {};
            while (system_->PollNextEvent(&event, sizeof(event)))
            {
                if (event.eventType == vr::VREvent_Quit)
                {
                    system_->AcknowledgeQuit_Exiting();
                    result.error =
                        "SteamVR closed the binding capture session.";
                    return result;
                }
            }

            if (compositor_ != nullptr)
            {
                compositor_->WaitGetPoses(
                    poses_.data(),
                    static_cast<std::uint32_t>(poses_.size()),
                    nullptr,
                    0u);
            }

            input::RefreshOpenVrHandState(
                system_,
                input::Hand::Left,
                &hands_[0]);
            input::RefreshOpenVrHandState(
                system_,
                input::Hand::Right,
                &hands_[1]);

            for (const input::SourceDefinition& source :
                 input::SourceDefinitions())
            {
                if (source.source == input::Source::Unbound ||
                    source.valueType != options.valueType ||
                    source.hand == input::Hand::None)
                {
                    continue;
                }

                const std::size_t sourceIndex =
                    static_cast<std::size_t>(source.source);

                if (options.valueType == input::ValueType::Boolean)
                {
                    bool active = false;
                    const bool pressed =
                        input::GetOpenVrBooleanSourceState(
                            hands_,
                            source.source,
                            &active);

                    if (!active)
                    {
                        continue;
                    }

                    if (initialized[sourceIndex] &&
                        pressed &&
                        !previousBoolean[sourceIndex])
                    {
                        return DescribeCapture(source.source);
                    }

                    initialized[sourceIndex] = true;
                    previousBoolean[sourceIndex] = pressed;
                }
                else
                {
                    bool active = false;
                    const input::OpenVrVector2 value =
                        input::GetOpenVrVector2SourceState(
                            hands_,
                            source.source,
                            &active);

                    if (!active)
                    {
                        continue;
                    }

                    const float magnitude = std::sqrt(
                        value.x * value.x + value.y * value.y);

                    if (initialized[sourceIndex] &&
                        magnitude >= 0.65f &&
                        previousMagnitude[sourceIndex] <= 0.25f)
                    {
                        return DescribeCapture(source.source);
                    }

                    initialized[sourceIndex] = true;
                    previousMagnitude[sourceIndex] = magnitude;
                }
            }

            if (compositor_ == nullptr)
            {
                Sleep(8u);
            }
        }
    }

private:
    CaptureResult DescribeCapture(const input::Source source) const
    {
        CaptureResult result;
        result.success = true;
        result.source = source;

        const input::SourceDefinition& sourceDefinition =
            input::GetSourceDefinition(source);
        const std::size_t handIndex =
            sourceDefinition.hand == input::Hand::Right ? 1u : 0u;

        result.profile =
            input::OpenVrHandDescription(hands_[handIndex]);
        result.localizedName = sourceDefinition.label;
        result.localizedName += " (SteamVR compatibility input)";
        return result;
    }

    vr::IVRSystem* system_ = nullptr;
    vr::IVRCompositor* compositor_ = nullptr;
    std::array<input::OpenVrHandState, 2> hands_ = {};
    std::array<
        vr::TrackedDevicePose_t,
        vr::k_unMaxTrackedDeviceCount> poses_ = {};
};

} // namespace

int WINAPI wWinMain(
    HINSTANCE,
    HINSTANCE,
    PWSTR,
    int)
{
    int argumentCount = 0;
    wchar_t** arguments =
        CommandLineToArgvW(
            GetCommandLineW(),
            &argumentCount);

    Options options;
    std::string error;

    const bool valid =
        arguments != nullptr &&
        ParseOptions(
            argumentCount,
            arguments,
            &options,
            &error);

    if (arguments != nullptr)
    {
        LocalFree(arguments);
    }

    if (!valid)
    {
        CaptureResult result;
        result.error = error.empty()
            ? "Could not read the mapper command line."
            : error;
        WriteResult(options.outputPath, result);
        return 2;
    }

    CaptureResult result;

    if (options.backend != CaptureBackend::OpenVr)
    {
        MapperRuntime runtime;
        result = runtime.Capture(options);
    }

    const bool tryOpenVr =
        options.backend == CaptureBackend::OpenVr ||
        (options.backend == CaptureBackend::Automatic &&
         !result.success &&
         result.runtimeFallbackAllowed);

    if (tryOpenVr)
    {
        const std::string openXrError = result.error;

        OpenVrMapperRuntime runtime;
        result = runtime.Capture(options);

        if (!result.success && !openXrError.empty())
        {
            result.error =
                "OpenXR capture: " + openXrError +
                " SteamVR fallback: " + result.error;
        }
    }

    WriteResult(options.outputPath, result);
    return result.success ? 0 : 1;
}
