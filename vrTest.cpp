#include <iostream>
#include <string_view>
#include <vector>
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

    const char* requestedExtensions[] = {
        "XR_FB_passthrough"
    };    

    XrInstanceCreateInfo createInfo = {};
    createInfo.type = XR_TYPE_INSTANCE_CREATE_INFO;
    createInfo.applicationInfo = appInfo;
    createInfo.enabledExtensionCount = 1; 
    createInfo.enabledExtensionNames = requestedExtensions;    

    XrInstance instance = XR_NULL_HANDLE;
    XrResult result = xrCreateInstance(&createInfo, &instance);

    if (XR_SUCCEEDED(result)) {
        std::cout << "SUCEESS: OpenXR Instance created. Connected to runtine!" << std::endl;

        XrSystemGetInfo systemInfo = {};
        systemInfo.type = XR_TYPE_SYSTEM_GET_INFO;
        systemInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;

        XrSystemId systemId;
        XrResult sysResult = xrGetSystem(instance, &systemInfo, &systemId);

        if (XR_SUCCEEDED(sysResult)) {
            std::cout << "SUCCESS: Found VR Headset. System ID: " << systemId << std::endl;


            // --- STEP 3: LOAD PROPRIETARY EXTENSION FUNCTIONS ---
            PFN_xrCreatePassthroughFB pfnCreatePassthroughFB = nullptr;
            
            XrResult procResult = xrGetInstanceProcAddr(
                instance, 
                "xrCreatePassthroughFB", 
                (PFN_xrVoidFunction*)&pfnCreatePassthroughFB
            );

            if (XR_SUCCEEDED(procResult) && pfnCreatePassthroughFB != nullptr) {
                std::cout << "SUCCESS: Meta Passthrough API successfully unlocked and loaded!" << std::endl;
            } else {
                std::cout << "FAILED: Extension accepted, but could not load function pointers." << std::endl;
            }

        } else {
            std::cout << "FAILED: Could not find a connected headset." << std::endl;
        }

        xrDestroyInstance(instance);
    } else {
        std::cout << "FAILED: Could not create OpenXR Instance. Error code: " << result << std::endl;
    }

    return 0;
}