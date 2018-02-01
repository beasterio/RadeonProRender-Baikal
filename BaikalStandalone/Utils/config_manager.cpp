/**********************************************************************
Copyright (c) 2016 Advanced Micro Devices, Inc. All rights reserved.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
********************************************************************/
#include "config_manager.h"

#include "CLW.h"
#include "RenderFactory/render_factory.h"
#include "SceneGraph/clwscene.h"
#include "Vulkan/VulkanDevice.hpp"

#ifndef APP_BENCHMARK

#ifdef __APPLE__
#include <OpenCL/OpenCL.h>
#include <OpenGL/OpenGL.h>
#elif WIN32
#define NOMINMAX
#include <Windows.h>
#include "GL/glew.h"
#else
#include <CL/cl.h>
#include <GL/glew.h>
#include <GL/glx.h>
#endif

#define VK_CHECK_RESULT(f)																				\
{																										\
	VkResult res = (f);																					\
	if (res != VK_SUCCESS)																				\
	{																									\
		assert(res == VK_SUCCESS);																		\
	}																									\
}

void ConfigManager::CreateConfigs(
    Mode mode,
    bool interop,
    std::vector<Config>& configs,
    int initial_num_bounces,
    int req_platform_index,
    int req_device_index)
{
    std::vector<CLWPlatform> platforms;

    CLWPlatform::CreateAllPlatforms(platforms);

    if (platforms.size() == 0)
    {
        throw std::runtime_error("No OpenCL platforms installed.");
    }

    configs.clear();

    if (req_platform_index >= (int)platforms.size())
        throw std::runtime_error("There is no such platform index");
    else if ((req_platform_index > 0) &&
        (req_device_index >= (int)platforms[req_platform_index].GetDeviceCount()))
        throw std::runtime_error("There is no such device index");

    bool hasprimary = false;

    int i = (req_platform_index >= 0) ? (req_platform_index) : 0;
    int d = (req_device_index >= 0) ? (req_device_index) : 0;

    int platforms_end = (req_platform_index >= 0) ?
        (req_platform_index + 1) : ((int)platforms.size());

    for (; i < platforms_end; ++i)
    {
        int device_end = 0;

        if (req_platform_index < 0 || req_device_index < 0)
            device_end = (int)platforms[i].GetDeviceCount();
        else
            device_end = req_device_index + 1;

        for (; d < device_end; ++d)
        {
            if (req_platform_index < 0)
            {
                if ((mode == kUseGpus || mode == kUseSingleGpu) && platforms[i].GetDevice(d).GetType() != CL_DEVICE_TYPE_GPU)
                    continue;

                if ((mode == kUseCpus || mode == kUseSingleCpu) && platforms[i].GetDevice(d).GetType() != CL_DEVICE_TYPE_CPU)
                    continue;
            }

            Config cfg;
            cfg.caninterop = false;

            if (platforms[i].GetDevice(d).HasGlInterop() && !hasprimary && interop)
            {
#ifdef WIN32
                cl_context_properties props[] =
                {
                    //OpenCL platform
                    CL_CONTEXT_PLATFORM, (cl_context_properties)((cl_platform_id)platforms[i]),
                    //OpenGL context
                    CL_GL_CONTEXT_KHR, (cl_context_properties)wglGetCurrentContext(),
                    //HDC used to create the OpenGL context
                    CL_WGL_HDC_KHR, (cl_context_properties)wglGetCurrentDC(),
                    0
                };
#elif __linux__
                cl_context_properties props[] =
                {
                    //OpenCL platform
                    CL_CONTEXT_PLATFORM, (cl_context_properties)((cl_platform_id)platforms[i]),
                    //OpenGL context
                    CL_GL_CONTEXT_KHR, (cl_context_properties)glXGetCurrentContext(),
                    //HDC used to create the OpenGL context
                    CL_GLX_DISPLAY_KHR, (cl_context_properties)glXGetCurrentDisplay(),
                    0
                };
#elif __APPLE__
                CGLContextObj kCGLContext = CGLGetCurrentContext();
                CGLShareGroupObj kCGLShareGroup = CGLGetShareGroup(kCGLContext);
                // Create CL context properties, add handle & share-group enum !
                cl_context_properties props[] = {
                    CL_CONTEXT_PROPERTY_USE_CGL_SHAREGROUP_APPLE,
                    (cl_context_properties)kCGLShareGroup, 0
                };
#endif
                cfg.context = CLWContext::Create(platforms[i].GetDevice(d), props);
                cfg.type = kPrimary;
                cfg.caninterop = true;
                hasprimary = true;
            }
            else
            {
                cfg.context = CLWContext::Create(platforms[i].GetDevice(d));
                cfg.type = kSecondary;
            }

            configs.push_back(std::move(cfg));

            if (mode == kUseSingleGpu || mode == kUseSingleCpu)
                break;
        }

        if (configs.size() == 1 && (mode == kUseSingleGpu || mode == kUseSingleCpu))
            break;
    }

    if (configs.size() == 0)
    {
        throw std::runtime_error(
            "No devices was selected (probably device index type does not correspond with real device type).");
    }

    if (!hasprimary)
    {
        configs[0].type = kPrimary;
    }

    for (int i = 0; i < configs.size(); ++i)
    {
        configs[i].factory = std::make_unique<Baikal::ClwRenderFactory>(configs[i].context, "cache");
        configs[i].controller = configs[i].factory->CreateSceneController();
        configs[i].renderer = configs[i].factory->CreateRenderer(Baikal::ClwRenderFactory::RendererType::kUnidirectionalPathTracer);
    }
}

#else
void ConfigManager::CreateConfigs(
    Mode mode,
    bool interop,
    std::vector<Config>& configs,
    int initial_num_bounces,
    int req_platform_index,
    int req_device_index)
{
    std::vector<CLWPlatform> platforms;

    CLWPlatform::CreateAllPlatforms(platforms);

    if (platforms.size() == 0)
    {
        throw std::runtime_error("No OpenCL platforms installed.");
    }

    configs.clear();

    bool hasprimary = false;
    for (int i = 0; i < (int)platforms.size(); ++i)
    {
        for (int d = 0; d < (int)platforms[i].GetDeviceCount(); ++d)
        {
            if ((mode == kUseGpus || mode == kUseSingleGpu) && platforms[i].GetDevice(d).GetType() != CL_DEVICE_TYPE_GPU)
                continue;

            if ((mode == kUseCpus || mode == kUseSingleCpu) && platforms[i].GetDevice(d).GetType() != CL_DEVICE_TYPE_CPU)
                continue;

            Config cfg;
            cfg.caninterop = false;
            cfg.context = CLWContext::Create(platforms[i].GetDevice(d));
            cfg.type = kSecondary;

            configs.push_back(std::move(cfg));

            if (mode == kUseSingleGpu || mode == kUseSingleCpu)
                break;
        }

        if (configs.size() == 1 && (mode == kUseSingleGpu || mode == kUseSingleCpu))
            break;
    }

    if (!hasprimary)
    {
        configs[0].type = kPrimary;
    }

    for (int i = 0; i < configs.size(); ++i)
    {
        configs[i].factory = std::make_unique<Baikal::ClwRenderFactory>(configs[i].context);
        configs[i].controller = configs[i].factory->CreateSceneController();
        configs[i].renderer = configs[i].factory->CreateRenderer(Baikal::ClwRenderFactory::RendererType::kUnidirectionalPathTracer);
    }
}
#endif //APP_BENCHMARK

void ConfigManager::CreateConfigs(
    Mode mode,
    bool interop,
    std::vector<VkConfig>& configs,
    int initial_num_bounces,
    int req_platform_index,
    int req_device_index)
{
    configs.clear();
    configs.push_back(VkConfig());
    //use only 1st available device
    VkConfig& conf = configs[0];

    VkResult err;
    bool enableValidation = true;
    // Vulkan instance
    err = configs[0].CreateInstance(enableValidation);
    if (err)
    {
        throw std::runtime_error("Could not create Vulkan instance.\n");
    }

#if defined(VK_USE_PLATFORM_ANDROID_KHR)
    vks::android::loadVulkanFunctions(instance);
#endif

    // If requested, we enable the default validation layers for debugging
    //if (enableValidation)
    //{
    //    // The report flags determine what type of messages for the layers will be displayed
    //    // For validating (debugging) an application the error and warning bits should suffice
    //    VkDebugReportFlagsEXT debugReportFlags = VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT;
    //    // Additional flags include performance info, loader and layer debug messages, etc.
    //    vks::debug::setupDebugging(conf.instance, debugReportFlags, VK_NULL_HANDLE);
    //}

    // Physical device
    uint32_t gpuCount = 0;
    // Get number of available physical devices
    VK_CHECK_RESULT(vkEnumeratePhysicalDevices(conf.instance, &gpuCount, nullptr));
    assert(gpuCount > 0);
    // Enumerate devices
    std::vector<VkPhysicalDevice> physicalDevices(gpuCount);
    err = vkEnumeratePhysicalDevices(conf.instance, &gpuCount, physicalDevices.data());
    if (err)
    {
        throw std::runtime_error("Could not enumerate physical devices.\n");
    }

    // GPU selection

    // Select physical device to be used for the Vulkan example
    // Defaults to the first device unless specified by command line
    uint32_t selectedDevice = 0;
    auto physicalDevice = physicalDevices[selectedDevice];

    // Store properties (including limits), features and memory properties of the physical device (so that examples can check against them)
    vkGetPhysicalDeviceProperties(physicalDevice, &conf.device_properties);
    vkGetPhysicalDeviceFeatures(physicalDevice, &conf.device_features);

    // Derived examples can override this to set actual features (based on above readings) to enable for logical device creation
    conf.GetEnabledFeatures();

    // Vulkan device creation
    // This is handled by a separate class that gets a logical device representation
    // and encapsulates functions related to a device
    conf.vulkan_device = new vks::VulkanDevice(physicalDevice);
    VkResult res = conf.vulkan_device->createLogicalDevice(conf.enabled_features, conf.enabled_extensions);
    if (res != VK_SUCCESS)
    {
        throw std::runtime_error("Could not create Vulkan device.\n");
    }
    auto& device = conf.vulkan_device->logicalDevice;

    // Get a graphics queue from the device
    vkGetDeviceQueue(device, conf.vulkan_device->queueFamilyIndices.graphics, 0, &conf.queue);

    for (int i = 0; i < configs.size(); ++i)
    {
        configs[i].factory = std::make_unique<Baikal::VkRenderFactory>();
        configs[i].controller = configs[i].factory->CreateSceneController();
        configs[i].renderer = configs[i].factory->CreateRenderer(Baikal::VkRenderFactory::RendererType::kUnidirectionalPathTracer);
    }
}

VkResult ConfigManager::VkConfig::CreateInstance(bool enableValidation)
{
    const char* app_name = "Baikal standalone.";
    VkApplicationInfo appInfo = {};
    appInfo.pApplicationName = app_name;
    appInfo.applicationVersion = VKEZ_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = app_name;
    appInfo.engineVersion = VKEZ_MAKE_VERSION(1, 0, 0);

    std::vector<const char*> instanceExtensions = { "VK_KHR_surface" };

    // Enable surface extensions depending on os
#if defined(_WIN32)
    instanceExtensions.push_back("VK_KHR_win32_surface");
#elif defined(VK_USE_PLATFORM_ANDROID_KHR)
    instanceExtensions.push_back(VK_KHR_ANDROID_SURFACE_EXTENSION_NAME);
#elif defined(_DIRECT2DISPLAY)
    instanceExtensions.push_back(VK_KHR_DISPLAY_EXTENSION_NAME);
#elif defined(VK_USE_PLATFORM_WAYLAND_KHR)
    instanceExtensions.push_back(VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME);
#elif defined(VK_USE_PLATFORM_XCB_KHR)
    instanceExtensions.push_back(VK_KHR_XCB_SURFACE_EXTENSION_NAME);
#elif defined(VK_USE_PLATFORM_IOS_MVK)
    instanceExtensions.push_back(VK_MVK_IOS_SURFACE_EXTENSION_NAME);
#elif defined(VK_USE_PLATFORM_MACOS_MVK)
    instanceExtensions.push_back(VK_MVK_MACOS_SURFACE_EXTENSION_NAME);
#endif

    VkInstanceCreateInfo instanceCreateInfo = {};
    instanceCreateInfo.pApplicationInfo = &appInfo;
    if (instanceExtensions.size() > 0)
    {
        if (enableValidation)
        {
            instanceExtensions.push_back("VK_EXT_debug_report");
        }
        instanceCreateInfo.enabledExtensionCount = (uint32_t)instanceExtensions.size();
        instanceCreateInfo.ppEnabledExtensionNames = instanceExtensions.data();
    }
    if (enableValidation)
    {
        //instanceCreateInfo.enabledLayerCount = vks::debug::validationLayerCount;
        //instanceCreateInfo.ppEnabledLayerNames = vks::debug::validationLayerNames;
    }
    return vkCreateInstance(&instanceCreateInfo, &instance);
}

void ConfigManager::VkConfig::GetEnabledFeatures()
{
    // Enable anisotropic filtering if supported
    if (device_features.samplerAnisotropy) {
        enabled_features.samplerAnisotropy = VK_TRUE;
    }
    // Enable texture compression  
    if (device_features.textureCompressionBC) {
        enabled_features.textureCompressionBC = VK_TRUE;
    }
    else if (device_features.textureCompressionASTC_LDR) {
        enabled_features.textureCompressionASTC_LDR = VK_TRUE;
    }
    else if (device_features.textureCompressionETC2) {
        enabled_features.textureCompressionETC2 = VK_TRUE;
    }

    if (device_features.shaderStorageImageExtendedFormats)
    {
        enabled_features.shaderStorageImageExtendedFormats = VK_TRUE;
    }
}