#include <iostream>
#include <string_view>
#include <openxr/openxr.h>

int main() {
    std::cout << "Starting VR Foundation" << std::endl;

    std::string_view appName = "VRDrummer";
    double appVersion = 1.0;
    double enginerVersion = 1.0;

    XrApplicationInfo appInfo = {};
    snprintf(appInfo.applicationName, XR_MAX_APPLICATION_NAME_SIZE, "%s", appName.data());
    appInfo.applicationVersion = appVersion;
    snprintf(appInfo.engineName, XR_MAX_APPLICATION_NAME_SIZE, "Native");
    appInfo.engineVersion = enginerVersion;
    appInfo.apiVersion = XR_CURRENT_API_VERSION;

    XrInstanceCreateInfo createInfo = {};
    createInfo.type = XR_TYPE_INSTANCE_CREATE_INFO;
    createInfo.applicationInfo = appInfo;

    XrInstance instance = XR_NULL_HANDLE;
    XrResult result = xrCreateInstance(&createInfo, &instance);

    if (XR_SUCCEEDED(result)) {
        std::cout << "SUCEESS: OpenXR Instance created. Connected to runtine!" << std::endl;
        xrDestroyInstance(instance);
    } else {
        std::cout << "FAILED: Could not create OpenXR Instance. Error code: " << result << std::endl;
    }

    return 0;
}