#include "vr/vr_openxr.h"

#include "qcommon/qcommon.h"

#include <openxr/openxr.h>

#include <cstdio>

bool VR_OpenXR_RuntimeProbe()
{
    Com_Printf(
        0,
        "[VR] Beginning OpenXR runtime probe...\n");

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

    XrInstance instance = XR_NULL_HANDLE;

    XrResult result =
        xrCreateInstance(&createInfo, &instance);

    if (XR_FAILED(result))
    {
        Com_PrintWarning(
            0,
            "[VR] xrCreateInstance failed with result %d.\n",
            static_cast<int>(result));

        Com_PrintWarning(
            0,
            "[VR] KisakCOD will continue without VR.\n");

        return false;
    }

    XrInstanceProperties runtimeProperties{
        XR_TYPE_INSTANCE_PROPERTIES
    };

    result = xrGetInstanceProperties(
        instance,
        &runtimeProperties);

    if (XR_FAILED(result))
    {
        Com_PrintWarning(
            0,
            "[VR] xrGetInstanceProperties failed with result %d.\n",
            static_cast<int>(result));

        xrDestroyInstance(instance);
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

    XrSystemId systemId = XR_NULL_SYSTEM_ID;

    result = xrGetSystem(
        instance,
        &systemInfo,
        &systemId);

    if (XR_FAILED(result))
    {
        Com_PrintWarning(
            0,
            "[VR] xrGetSystem failed with result %d.\n",
            static_cast<int>(result));

        Com_PrintWarning(
            0,
            "[VR] Runtime detected, but no HMD was available.\n");

        xrDestroyInstance(instance);
        return false;
    }

    XrSystemProperties systemProperties{
        XR_TYPE_SYSTEM_PROPERTIES
    };

    result = xrGetSystemProperties(
        instance,
        systemId,
        &systemProperties);

    if (XR_FAILED(result))
    {
        Com_PrintWarning(
            0,
            "[VR] xrGetSystemProperties failed with result %d.\n",
            static_cast<int>(result));

        xrDestroyInstance(instance);
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

    xrDestroyInstance(instance);

    Com_Printf(
        0,
        "[VR] OpenXR runtime probe completed successfully.\n");

    return true;
}
