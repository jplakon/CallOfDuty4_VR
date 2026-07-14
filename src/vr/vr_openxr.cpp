#include "vr/vr_openxr.h"

#include "qcommon/qcommon.h"

#include <openxr/openxr.h>

#include <cstdio>

namespace
{
XrInstance g_vrInstance = XR_NULL_HANDLE;
XrSystemId g_vrSystemId = XR_NULL_SYSTEM_ID;
bool g_vrInitialized = false;

void VR_ResetHandles()
{
    g_vrInstance = XR_NULL_HANDLE;
    g_vrSystemId = XR_NULL_SYSTEM_ID;
    g_vrInitialized = false;
}
}

bool VR_Init()
{
    if (g_vrInitialized)
    {
        return true;
    }

    Com_Printf(
        0,
        "[VR] Initializing persistent OpenXR subsystem...\n");

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

    XrResult result =
        xrCreateInstance(&createInfo, &g_vrInstance);

    if (XR_FAILED(result))
    {
        Com_PrintWarning(
            0,
            "[VR] xrCreateInstance failed with result %d.\n",
            static_cast<int>(result));

        VR_ResetHandles();
        return false;
    }

    XrInstanceProperties runtimeProperties{
        XR_TYPE_INSTANCE_PROPERTIES
    };

    result = xrGetInstanceProperties(
        g_vrInstance,
        &runtimeProperties);

    if (XR_FAILED(result))
    {
        Com_PrintWarning(
            0,
            "[VR] xrGetInstanceProperties failed with result %d.\n",
            static_cast<int>(result));

        VR_Shutdown();
        return false;
    }

    Com_Printf(
        0,
        "[VR] OpenXR runtime: %s\n",
        runtimeProperties.runtimeName);

    XrSystemGetInfo systemInfo{
        XR_TYPE_SYSTEM_GET_INFO
    };

    systemInfo.formFactor =
        XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;

    result = xrGetSystem(
        g_vrInstance,
        &systemInfo,
        &g_vrSystemId);

    if (XR_FAILED(result))
    {
        Com_PrintWarning(
            0,
            "[VR] xrGetSystem failed with result %d.\n",
            static_cast<int>(result));

        VR_Shutdown();
        return false;
    }

    XrSystemProperties systemProperties{
        XR_TYPE_SYSTEM_PROPERTIES
    };

    result = xrGetSystemProperties(
        g_vrInstance,
        g_vrSystemId,
        &systemProperties);

    if (XR_FAILED(result))
    {
        Com_PrintWarning(
            0,
            "[VR] xrGetSystemProperties failed with result %d.\n",
            static_cast<int>(result));

        VR_Shutdown();
        return false;
    }

    Com_Printf(
        0,
        "[VR] OpenXR system: %s\n",
        systemProperties.systemName);

    Com_Printf(
        0,
        "[VR] Orientation tracking: %s\n",
        systemProperties.trackingProperties.orientationTracking
            ? "yes"
            : "no");

    Com_Printf(
        0,
        "[VR] Position tracking: %s\n",
        systemProperties.trackingProperties.positionTracking
            ? "yes"
            : "no");

    Com_Printf(
        0,
        "[VR] Maximum swapchain size: %u x %u\n",
        systemProperties.graphicsProperties.maxSwapchainImageWidth,
        systemProperties.graphicsProperties.maxSwapchainImageHeight);

    g_vrInitialized = true;

    Com_Printf(
        0,
        "[VR] Persistent OpenXR subsystem initialized successfully.\n");

    return true;
}

void VR_Frame()
{
    if (!g_vrInitialized ||
        g_vrInstance == XR_NULL_HANDLE)
    {
        return;
    }

    XrEventDataBuffer eventData{
        XR_TYPE_EVENT_DATA_BUFFER
    };

    while (true)
    {
        const XrResult result =
            xrPollEvent(g_vrInstance, &eventData);

        if (result == XR_EVENT_UNAVAILABLE)
        {
            break;
        }

        if (XR_FAILED(result))
        {
            Com_PrintWarning(
                0,
                "[VR] xrPollEvent failed with result %d.\n",
                static_cast<int>(result));

            break;
        }

        if (eventData.type ==
            XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING)
        {
            Com_PrintWarning(
                0,
                "[VR] OpenXR runtime reported instance loss pending.\n");
        }

        eventData = XrEventDataBuffer{
            XR_TYPE_EVENT_DATA_BUFFER
        };
    }
}

void VR_Shutdown()
{
    if (g_vrInstance != XR_NULL_HANDLE)
    {
        Com_Printf(
            0,
            "[VR] Shutting down OpenXR subsystem...\n");

        const XrResult result =
            xrDestroyInstance(g_vrInstance);

        if (XR_FAILED(result))
        {
            Com_PrintWarning(
                0,
                "[VR] xrDestroyInstance failed with result %d.\n",
                static_cast<int>(result));
        }
    }

    VR_ResetHandles();
}

bool VR_IsInitialized()
{
    return g_vrInitialized;
}
