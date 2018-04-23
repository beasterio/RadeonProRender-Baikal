#include <vulkan/vulkan.h>
#include <iostream>
#include <fstream>
#include <stdexcept>
#include <vector>
#include <functional>

#include "spirv_msl.hpp"
#include "vk_scoped_object.h"
#include "vk_memory_allocator.h"
#include "vk_memory_manager.h"
#include "vk_shader_manager.h"
#include "vk_descriptor_manager.h"
#include "vk_pipeline_manager.h"
#include "vk_render_target_manager.h"
#include "vk_command_buffer_builder.h"
#include "vk_execution_manager.h"

using namespace vkw;

VkScopedObject<VkInstance> create_instance()
{
    // Initialize usual Vulkan crap
    VkApplicationInfo app_info;
    app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.pNext = NULL;
    app_info.pApplicationName = "VK test";
    app_info.applicationVersion = 1;
    app_info.pEngineName = "Vk test";
    app_info.engineVersion = 1;
    app_info.apiVersion = VK_API_VERSION_1_0;
    
    VkInstanceCreateInfo instance_info;
    instance_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instance_info.pNext = NULL;
    instance_info.flags = 0;
    instance_info.pApplicationInfo = &app_info;
    instance_info.enabledExtensionCount = 0;
    instance_info.ppEnabledExtensionNames = NULL;
    instance_info.enabledLayerCount = 0;
    instance_info.ppEnabledLayerNames = NULL;
    
    VkInstance instance = nullptr;
    VkResult res = vkCreateInstance(&instance_info, nullptr, &instance);
    if (res == VK_ERROR_INCOMPATIBLE_DRIVER)
    {
        throw std::runtime_error("Cannot find a compatible Vulkan ICD\n");
    }
    else if (res)
    {
        throw std::runtime_error("Unknown error\n");
    }
    
    return VkScopedObject<VkInstance>(instance,
                                      [](VkInstance instance)
                                      {
                                          vkDestroyInstance(instance, nullptr);
                                      });
}

VkScopedObject<VkDevice> create_device(VkInstance instance,
                                       std::uint32_t& queue_family_index,
                                       VkPhysicalDevice* opt_physical_device = nullptr)
{
    // Enumerate devices
    auto gpu_count = 0u;
    auto res = vkEnumeratePhysicalDevices(instance, &gpu_count, nullptr);
    
    if (gpu_count == 0)
    {
        throw std::runtime_error("No compatible devices found\n");
    }
    
    std::vector<VkPhysicalDevice> gpus(gpu_count);
    res = vkEnumeratePhysicalDevices(instance, &gpu_count, gpus.data());
    
    float queue_priority = 0.f;
    VkDeviceQueueCreateInfo queue_create_info;
    queue_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue_create_info.pNext = nullptr;
    queue_create_info.flags = 0;
    queue_create_info.queueCount = 1u;
    queue_create_info.pQueuePriorities = &queue_priority;
    
    auto queue_family_count = 0u;
    vkGetPhysicalDeviceQueueFamilyProperties(gpus[0], &queue_family_count, nullptr);
    
    std::vector<VkQueueFamilyProperties> queue_props(queue_family_count);
    vkGetPhysicalDeviceQueueFamilyProperties(gpus[0], &queue_family_count, queue_props.data());
    
    // Look for a queue supporing both compute and transfer
    bool found = false;
    for (unsigned int i = 0; i < queue_family_count; i++)
    {
        if (queue_props[i].queueFlags & (VK_QUEUE_GRAPHICS_BIT))
        {
            queue_create_info.queueFamilyIndex = i;
            found = true;
            break;
        }
    }
    
    if (!found)
    {
        throw std::runtime_error("No compute/transfer queues found\n");
    }
    
    VkDeviceCreateInfo device_create_info;
    device_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    device_create_info.pNext = nullptr;
    device_create_info.flags = 0;
    device_create_info.queueCreateInfoCount = 1u;
    device_create_info.pQueueCreateInfos = &queue_create_info;
    device_create_info.enabledLayerCount = 0u;
    device_create_info.ppEnabledLayerNames = nullptr;
    device_create_info.enabledExtensionCount = 0u;
    device_create_info.ppEnabledExtensionNames = nullptr;
    device_create_info.pEnabledFeatures = nullptr;
    
    VkDevice device = nullptr;
    res = vkCreateDevice(gpus[0], &device_create_info, nullptr, &device);
    
    if (res != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create Vulkan device\n");
    }
    
    if (opt_physical_device)
    {
        *opt_physical_device = gpus[0];
    }
    
    return VkScopedObject<VkDevice>(device,
                                    [](VkDevice device)
                                    {
                                        vkDestroyDevice(device, nullptr);
                                    });
}


int main(int argc, const char * argv[])
{
    auto instance = create_instance();
    
    VkPhysicalDevice physical_device;
    std::uint32_t queue_family_index = 0u;
    auto device = create_device(instance, queue_family_index, &physical_device);
    
    MemoryAllocator allocator(device, physical_device);
    MemoryManager memory_manager(device, queue_family_index, allocator);
    DescriptorManager descriptor_manager(device);
    ShaderManager shader_manager(device, descriptor_manager);
    PipelineManager pipeline_manager(device);

    RenderTargetManager render_target_manager(device, memory_manager);
    
    const uint32_t window_width = 1920;
    const uint32_t window_height = 1080;
    
    auto image = memory_manager.CreateImage({window_width, window_height, 1},
                                            VK_FORMAT_R16G16B16A16_SFLOAT,
                                            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
    
    std::vector<RenderTargetCreateInfo> attachments = {
        {window_width, window_height, VK_FORMAT_R16G16B16A16_UINT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT},
        {window_width, window_height, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT},
        {window_width, window_height, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT}
    };
    
    RenderTarget render_target = render_target_manager.CreateRenderTarget(attachments);
    
    std::vector<VkPushConstantRange> push_constant_ranges;

    CommandBufferBuilder command_buffer_builder(device, queue_family_index);
    ExecutionManager exec_manager(device, queue_family_index);
    
    auto shader = shader_manager.CreateShader(VK_SHADER_STAGE_COMPUTE_BIT, "../vkw/add.comp.spv");
    auto pipeline = pipeline_manager.CreateComputePipeline(shader, 64u, 1u, 1u);
    
    auto shader_vs = shader_manager.CreateShader(VK_SHADER_STAGE_VERTEX_BIT, "../Baikal/Kernels/VK/shaders/mrt.vert.spv");
    auto shader_ps = shader_manager.CreateShader(VK_SHADER_STAGE_FRAGMENT_BIT, "../Baikal/Kernels/VK/shaders/mrt.frag.spv");
    auto graphics_pipeline = pipeline_manager.CreateGraphicsPipeline(shader_vs, shader_ps, render_target.render_pass);
    
    std::vector<int> fake_data{1, 2, 3, 4, 5};
    auto buffer_a = memory_manager.CreateBuffer(fake_data.size() * sizeof(int),
                                                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                                fake_data.data());
    
    auto buffer_b = memory_manager.CreateBuffer(fake_data.size() * sizeof(int),
                                                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                                VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                                fake_data.data());
    
    auto buffer_c = memory_manager.CreateBuffer(fake_data.size() * sizeof(int),
                                                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                                fake_data.data());
    
    shader.SetArg(0u, buffer_a);
    shader.SetArg(1u, buffer_b);
    shader.SetArg(2u, buffer_c);

    auto buffer = memory_manager.CreateBuffer(fake_data.size() * sizeof(int),
                                                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                                VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                                fake_data.data());
    
    std::vector<int> data(5);
    memory_manager.ReadBuffer(buffer, 0u, fake_data.size() * sizeof(int), data.data());
    
    std::vector<int> new_data{5, 4, 3, 2, 1};
    memory_manager.WriteBuffer(buffer, 0u, new_data.size() * sizeof(int), new_data.data());
    memory_manager.ReadBuffer(buffer, 0u, new_data.size() * sizeof(int), data.data());
    
    
    command_buffer_builder.BeginCommandBuffer();
    command_buffer_builder.Dispatch(pipeline, shader, 1u, 1u, 1u);
    command_buffer_builder.Barrier(buffer_c,
                                   VK_ACCESS_SHADER_WRITE_BIT,
                                   VK_ACCESS_TRANSFER_READ_BIT,
                                   VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                   VK_PIPELINE_STAGE_TRANSFER_BIT);

    
    auto command_buffer = command_buffer_builder.EndCommandBuffer();
    exec_manager.Submit(command_buffer);
    exec_manager.WaitIdle();
    
    std::vector<int> result(5);
    memory_manager.ReadBuffer(buffer_c, 0u, 5 * sizeof(int), result.data());
    
    for(auto& v : result)
    {
        std::cout << v << " ";
    }
    
    std::cout << "\n";
    
    return 0;
}
