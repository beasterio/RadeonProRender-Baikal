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
#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include "CLW.h"
#include "RenderFactory/clw_render_factory.h"
#include "RenderFactory/vk_render_factory.h"
#include "Renderers/renderer.h"
#include <vector>
#include <memory>

namespace vks
{
    class VulkanDevice;
}


class ConfigManager
{
public:

    enum DeviceType
    {
        kPrimary,
        kSecondary
    };

    enum Mode
    {
        kUseAll,
        kUseGpus,
        kUseSingleGpu,
        kUseSingleCpu,
        kUseCpus
    };

    struct Config
    {
        DeviceType type;
        std::unique_ptr<Baikal::Renderer<Baikal::ClwScene>> renderer;
        std::unique_ptr<Baikal::SceneController<Baikal::ClwScene>> controller;
        std::unique_ptr<Baikal::RenderFactory<Baikal::ClwScene>> factory;
        CLWContext context;
        bool caninterop;

        Config() = default;

        Config(Config&& cfg) = default;

        ~Config()
        {
        }
    };

    struct VkConfig
    {
        DeviceType type;
        std::unique_ptr<Baikal::Renderer<Baikal::VkScene>> renderer;
        std::unique_ptr<Baikal::SceneController<Baikal::VkScene>> controller;
        std::unique_ptr<Baikal::RenderFactory<Baikal::VkScene>> factory;
        bool caninterop;

        VkInstance instance;
        // Stores physical device properties (for e.g. checking device limits)
        VkPhysicalDeviceProperties device_properties;
        // Stores the features available on the selected physical device (for e.g. checking if a feature is available)
        VkPhysicalDeviceFeatures device_features;
        // Stores all available memory (type) properties for the physical device
        VkPhysicalDeviceMemoryProperties device_memory_roperties;
        vks::VulkanDevice *vulkan_device;
        VkQueue queue;

        std::vector<const char*> enabled_extensions;
        VkPhysicalDeviceFeatures enabled_features{};

        VkConfig() = default;

        VkConfig(VkConfig&& cfg) = default;

        ~VkConfig()
        {
        }

        VkResult CreateInstance(bool enableValidation);
        void GetEnabledFeatures();
    };

    static void CreateConfigs(
        Mode mode,
        bool interop,
        std::vector<Config>& renderers,
        int initial_num_bounces,
        int req_platform_index = -1,
        int req_device_index = -1);

    static void CreateConfigs(
        Mode mode,
        bool interop,
        std::vector<VkConfig>& renderers,
        int initial_num_bounces,
        int req_platform_index = -1,
        int req_device_index = -1);

private:

};

#endif // CONFIG_MANAGER_H
