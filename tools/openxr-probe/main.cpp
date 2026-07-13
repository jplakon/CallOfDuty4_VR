#include <openxr/openxr.h>

#include <cstdio>
#include <iostream>

static const char* ResultName(const XrResult result)
{
    switch (result)
    {
        case XR_SUCCESS:
            return "XR_SUCCESS";
        case XR_ERROR_RUNTIME_UNAVAILABLE:
            return "XR_ERROR_RUNTIME_UNAVAILABLE";
        case XR_ERROR_INITIALIZATION_FAILED:
            return "XR_ERROR_INITIALIZATION_FAILED";
        case XR_ERROR_API_VERSION_UNSUPPORTED:
            return "XR_ERROR_API_VERSION_UNSUPPORTED";
        case XR_ERROR_FORM_FACTOR_UNAVAILABLE:
            return "XR_ERROR_FORM_FACTOR_UNAVAILABLE";
        case XR_ERROR_INSTANCE_LOST:
            return "XR_ERROR_INSTANCE_LOST";
        default:
            return "Unknown XrResult";
    }
}

int main()
{
    XrInstanceCreateInfo createInfo{XR_TYPE_INSTANCE_CREATE_INFO};

    std::snprintf(
        createInfo.applicationInfo.applicationName,
        XR_MAX_APPLICATION_NAME_SIZE,
        "%s",
        "KisakCOD OpenXR Probe");

    createInfo.applicationInfo.applicationVersion = 1;

    std::snprintf(
        createInfo.applicationInfo.engineName,
        XR_MAX_ENGINE_NAME_SIZE,
        "%s",
        "KisakCOD");

    createInfo.applicationInfo.engineVersion = 1;

    // Request OpenXR 1.0 for broad runtime compatibility.
    createInfo.applicationInfo.apiVersion = XR_MAKE_VERSION(1, 0, 0);

    XrInstance instance = XR_NULL_HANDLE;

    XrResult result = xrCreateInstance(&createInfo, &instance);

    if (XR_FAILED(result))
    {
        std::cerr
            << "xrCreateInstance failed: "
            << ResultName(result)
            << " (" << result << ")\n";

        return 1;
    }

    XrInstanceProperties instanceProperties{
        XR_TYPE_INSTANCE_PROPERTIES};

    result = xrGetInstanceProperties(instance, &instanceProperties);

    if (XR_FAILED(result))
    {
        std::cerr
            << "xrGetInstanceProperties failed: "
            << ResultName(result)
            << " (" << result << ")\n";

        xrDestroyInstance(instance);
        return 2;
    }

    std::cout
        << "OpenXR runtime: "
        << instanceProperties.runtimeName
        << "\nRuntime version: "
        << XR_VERSION_MAJOR(instanceProperties.runtimeVersion)
        << "."
        << XR_VERSION_MINOR(instanceProperties.runtimeVersion)
        << "."
        << XR_VERSION_PATCH(instanceProperties.runtimeVersion)
        << "\n";

    XrSystemGetInfo systemInfo{XR_TYPE_SYSTEM_GET_INFO};
    systemInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;

    XrSystemId systemId = XR_NULL_SYSTEM_ID;

    result = xrGetSystem(instance, &systemInfo, &systemId);

    if (XR_FAILED(result))
    {
        std::cerr
            << "xrGetSystem failed: "
            << ResultName(result)
            << " (" << result << ")\n";

        xrDestroyInstance(instance);
        return 3;
    }

    XrSystemProperties systemProperties{
        XR_TYPE_SYSTEM_PROPERTIES};

    result = xrGetSystemProperties(
        instance,
        systemId,
        &systemProperties);

    if (XR_FAILED(result))
    {
        std::cerr
            << "xrGetSystemProperties failed: "
            << ResultName(result)
            << " (" << result << ")\n";

        xrDestroyInstance(instance);
        return 4;
    }

    std::cout
        << "OpenXR system: "
        << systemProperties.systemName
        << "\nOrientation tracking: "
        << (systemProperties.trackingProperties.orientationTracking
                ? "yes"
                : "no")
        << "\nPosition tracking: "
        << (systemProperties.trackingProperties.positionTracking
                ? "yes"
                : "no")
        << "\nMaximum swapchain width: "
        << systemProperties.graphicsProperties.maxSwapchainImageWidth
        << "\nMaximum swapchain height: "
        << systemProperties.graphicsProperties.maxSwapchainImageHeight
        << "\nMaximum layer count: "
        << systemProperties.graphicsProperties.maxLayerCount
        << "\n";

    xrDestroyInstance(instance);

    std::cout << "OpenXR probe completed successfully.\n";
    return 0;
}
