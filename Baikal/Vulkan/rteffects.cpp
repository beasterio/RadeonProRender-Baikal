#include "rteffects.h"

#include "vk_memory_allocator.h"
#include "vk_utils.h"
#include "ambient_occlusion.h"
#include "global_illumination.h"
#include "global_illumination_glossy.h"
#include "direct_illumination.h"

#include <unordered_map>

namespace
{
    auto constexpr kMaxDescriptors = 64u;
    auto constexpr kMaxDescriptorSets = 64u;
    auto constexpr kInitialWorkBufferSize = 1920u * 1080u;
    auto constexpr kRngBufferSize = 256u * 256u;

    struct Instance
    {
        // Vulkan device to run queries on
        vk::Device device = nullptr;
        // Command pool to allocate command buffers
        vk::CommandPool cmd_pool = nullptr;
        // Pipeline cache
        vk::PipelineCache pipeline_cache = nullptr;
        // Descriptor pool for RR descriptor sets
        vk::DescriptorPool desc_pool = nullptr;
        // Allocator
        std::unique_ptr<VkMemoryAlloc> alloc = nullptr;
        // Intersector
        rr_instance intersector = nullptr;
        // Scene
        rte_scene scene;

        // GBuffer
        rte_gbuffer gbuffer;

        // Output
        rte_output output;

        // 
        RTE::RtBuffers rt_buffers;

        // Effects
        std::unordered_map<rte_effect, std::unique_ptr<RTE::Effect>> effects;
    };

    static void InitInstance(Instance* instance,
                             VkDevice device,
                             VkPhysicalDevice physical_device,
                             VkCommandPool command_pool) 
    {
        instance->device = device;
        instance->cmd_pool = command_pool;

        auto& dev = instance->device;

        // Create pipeline cache
        vk::PipelineCacheCreateInfo cache_create_info;
        instance->pipeline_cache = dev.createPipelineCache(cache_create_info);

        // Create descriptor pool
        vk::DescriptorPoolSize pool_sizes[] =
        {
            vk::DescriptorPoolSize(vk::DescriptorType::eStorageBuffer, 64),
            vk::DescriptorPoolSize(vk::DescriptorType::eCombinedImageSampler, 64),
            vk::DescriptorPoolSize(vk::DescriptorType::eStorageImage, 64),
            vk::DescriptorPoolSize(vk::DescriptorType::eUniformBuffer, 8),
        };

        vk::DescriptorPoolCreateInfo descpool_create_info;
        descpool_create_info
            .setMaxSets(kMaxDescriptorSets)
            .setPoolSizeCount(sizeof(pool_sizes) / sizeof(vk::DescriptorPoolSize))
            .setPPoolSizes(pool_sizes);
        instance->desc_pool =
            dev.createDescriptorPool(descpool_create_info);

        instance->alloc.reset(new VkMemoryAlloc(device, physical_device));

        // Ray buffers
        instance->rt_buffers.rays[0] = instance->alloc->allocate(
            vk::MemoryPropertyFlagBits::eDeviceLocal,
            vk::BufferUsageFlagBits::eStorageBuffer,
            kInitialWorkBufferSize * sizeof(Ray),
            16u);
        instance->rt_buffers.rays[1] = instance->alloc->allocate(
            vk::MemoryPropertyFlagBits::eDeviceLocal,
            vk::BufferUsageFlagBits::eStorageBuffer,
            kInitialWorkBufferSize * sizeof(Ray),
            16u);
        instance->rt_buffers.shadow_rays = instance->alloc->allocate(
            vk::MemoryPropertyFlagBits::eDeviceLocal,
            vk::BufferUsageFlagBits::eStorageBuffer,
            kInitialWorkBufferSize * sizeof(Ray),
            16u);
        instance->rt_buffers.hits = instance->alloc->allocate(
            vk::MemoryPropertyFlagBits::eDeviceLocal,
            vk::BufferUsageFlagBits::eStorageBuffer,
            kInitialWorkBufferSize * sizeof(Hit),
            16u);

        auto rng_buffer_size_in_bytes = kRngBufferSize * sizeof(uint32_t);
        instance->rt_buffers.rng = instance->alloc->allocate(
            vk::MemoryPropertyFlagBits::eDeviceLocal,
            vk::BufferUsageFlagBits::eStorageBuffer |
            vk::BufferUsageFlagBits::eTransferDst,
            rng_buffer_size_in_bytes,
            16u);
        instance->rt_buffers.rng_staging = instance->alloc->allocate(
            vk::MemoryPropertyFlagBits::eHostVisible,
            vk::BufferUsageFlagBits::eTransferSrc,
            rng_buffer_size_in_bytes,
            16u);
        instance->rt_buffers.ray_count[0] = instance->alloc->allocate(
            vk::MemoryPropertyFlagBits::eDeviceLocal,
            vk::BufferUsageFlagBits::eTransferDst | 
            vk::BufferUsageFlagBits::eStorageBuffer,
            sizeof(std::uint32_t),
            16u);
        instance->rt_buffers.ray_count[1] = instance->alloc->allocate(
            vk::MemoryPropertyFlagBits::eDeviceLocal,
            vk::BufferUsageFlagBits::eTransferDst |
            vk::BufferUsageFlagBits::eStorageBuffer,
            sizeof(std::uint32_t),
            16u);

        
    }

    static void CheckAndReallocRayBuffers(Instance* instance, std::size_t required_size)
    {
        if (instance->rt_buffers.rays[0].size < required_size) {
            instance->alloc->deallocate(instance->rt_buffers.rays[0]);
            instance->alloc->deallocate(instance->rt_buffers.shadow_rays);
            instance->alloc->deallocate(instance->rt_buffers.hits);
            instance->rt_buffers.rays[0] = instance->alloc->allocate(
                vk::MemoryPropertyFlagBits::eDeviceLocal,
                vk::BufferUsageFlagBits::eStorageBuffer,
                required_size,
                16u);
            instance->rt_buffers.rays[1] = instance->alloc->allocate(
                vk::MemoryPropertyFlagBits::eDeviceLocal,
                vk::BufferUsageFlagBits::eStorageBuffer,
                required_size,
                16u);
            instance->rt_buffers.shadow_rays = instance->alloc->allocate(
                vk::MemoryPropertyFlagBits::eDeviceLocal,
                vk::BufferUsageFlagBits::eStorageBuffer,
                required_size,
                16u);
            instance->rt_buffers.hits = instance->alloc->allocate(
                vk::MemoryPropertyFlagBits::eDeviceLocal,
                vk::BufferUsageFlagBits::eStorageBuffer,
                required_size,
                16u);
        }
    }
}

// Initialize and instance of library.
rte_status rteInitInstance(// GPU to run ray queries on
                           VkDevice device,
                           //
                           VkPhysicalDevice physical_device,
                           // Command pool to allocate command buffers
                           VkCommandPool command_pool,
                           // Intersector
                           rr_instance intersector,
                           // Enabled effects
                           rte_effect effects,
                           // Resulting instance
                           rte_instance* out_instance)
{
    if (!device || !command_pool || !intersector)
    {
        *out_instance = nullptr;
        return RTE_ERROR_INVALID_VALUE;
    }

    auto instance = new Instance{};
    InitInstance(instance, device, physical_device, command_pool);

    instance->intersector = intersector;

    if (effects & RTE_EFFECT_AO)
    {
        instance->effects.emplace(std::make_pair(RTE_EFFECT_AO, 
                                                 std::make_unique<RTE::AmbientOcclusion>(instance->device,
                                                                                         instance->desc_pool,
                                                                                         instance->cmd_pool,
                                                                                         instance->pipeline_cache,
                                                                                         instance->rt_buffers,
                                                                                         *instance->alloc,
                                                                                         instance->intersector)));
    }

    if (effects & (RTE_EFFECT_INDIRECT_DIFFUSE) == (RTE_EFFECT_INDIRECT_DIFFUSE))
    {
        instance->effects.emplace(std::make_pair(RTE_EFFECT_INDIRECT_DIFFUSE,
                                                 std::make_unique<RTE::GlobalIllumination>(instance->device,
                                                                                           instance->desc_pool,
                                                                                           instance->cmd_pool,
                                                                                           instance->pipeline_cache,
                                                                                           instance->rt_buffers,
                                                                                           *instance->alloc,
                                                                                           instance->intersector)));
    }

    if (effects & (RTE_EFFECT_INDIRECT_GLOSSY) == (RTE_EFFECT_INDIRECT_GLOSSY))
    {
        instance->effects.emplace(std::make_pair(RTE_EFFECT_INDIRECT_GLOSSY,
            std::make_unique<RTE::GlobalIlluminationGlossy>(instance->device,
                instance->desc_pool,
                instance->cmd_pool,
                instance->pipeline_cache,
                instance->rt_buffers,
                *instance->alloc,
                instance->intersector)));
    }

    *out_instance = reinterpret_cast<rte_instance>(instance);
    return RR_SUCCESS;

}

rte_status rteSetScene(// API instance
                       rte_instance inst,
                       // Scene
                       rte_scene* scene)
{
    if (!inst || !scene)
    {
        return RTE_ERROR_INVALID_VALUE;
    }

    auto instance = reinterpret_cast<Instance*>(inst);

    instance->scene = *scene;

    return RTE_SUCCESS;
}

rte_status rteCalculate(// API instance
                          rte_instance inst,
                          rte_effect eff,
                          // Scene creation command buffer
                          VkCommandBuffer* out_command_buffer,
                          uint32_t command_buffers_count,
                          uint32_t* command_buffers_required)
{
    if (!inst || !out_command_buffer)
    {
        return RTE_ERROR_INVALID_VALUE;
    }

    auto instance = reinterpret_cast<Instance*>(inst);

    auto iter = instance->effects.find(eff);

    if (iter == instance->effects.cend())
    {
        return RTE_ERROR_INVALID_VALUE;
    }

    auto& effect = iter->second;

    auto num_cbs = effect->GetCommandBuffersCount();

    if (!out_command_buffer)
    {

        *command_buffers_required = num_cbs;
        return RR_SUCCESS;
    }

    if (command_buffers_count < num_cbs)
    {
        return RR_ERROR_INVALID_VALUE;
    }

     auto cbs = effect->Apply(instance->scene,
                              instance->gbuffer,
                              instance->output);

     for (auto i = 0u; i < num_cbs; ++i)
     {
         out_command_buffer[i] = cbs[i];
     }

    return RR_SUCCESS;
}

// Commit changes for the current scene maintained by an API.
RTE_API rte_status rteCommit(// API instance
                             rte_instance inst,
                             // Effect
                             rte_effect eff,
                             // Scene creation command buffer
                             VkCommandBuffer* out_command_buffer)
{
    if (!inst || !out_command_buffer)
    {
        return RTE_ERROR_INVALID_VALUE;
    }

    auto instance = reinterpret_cast<Instance*>(inst);

    auto iter = instance->effects.find(eff);

    if (iter == instance->effects.cend())
    {
        return RTE_ERROR_INVALID_VALUE;
    }

    auto& effect = iter->second;

    // Allocate command buffer
    vk::CommandBufferAllocateInfo cmdbuffer_alloc_info;
    cmdbuffer_alloc_info
        .setCommandBufferCount(1)
        .setCommandPool(instance->cmd_pool)
        .setLevel(vk::CommandBufferLevel::ePrimary);

    auto cmdbuffers
        = instance->device.allocateCommandBuffers(cmdbuffer_alloc_info);

    // Begin command buffer
    vk::CommandBufferBeginInfo cmdbuffer_buffer_begin_info;
    cmdbuffer_buffer_begin_info.setFlags(vk::CommandBufferUsageFlagBits::eSimultaneousUse);
    cmdbuffers[0].begin(cmdbuffer_buffer_begin_info);

    effect->Init(cmdbuffers[0]);

    cmdbuffers[0].end();

    *out_command_buffer = cmdbuffers[0];
    return RR_SUCCESS;

}

rte_status rteSetGBuffer(// API instance
                          rte_instance inst,
                          // 
                          rte_gbuffer* gbuffer)
{
    if (!inst || !gbuffer)
    {
        return RTE_ERROR_INVALID_VALUE;
    }

    auto instance = reinterpret_cast<Instance*>(inst);

    instance->gbuffer = *gbuffer;

    return RTE_SUCCESS;
}

rte_status rteSetOutput(// API instance
                        rte_instance inst,
                        rte_output* output)
{
    if (!inst || !output)
    {
        return RTE_ERROR_INVALID_VALUE;
    }

    auto instance = reinterpret_cast<Instance*>(inst);

    instance->output = *output;

    CheckAndReallocRayBuffers(instance, output->width * output->height * sizeof(Ray));

    return RTE_SUCCESS;
}

rte_status rteShutdownInstance(rte_instance instance)
{
    delete instance;
    return RTE_SUCCESS;
}
