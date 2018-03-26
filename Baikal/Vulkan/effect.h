#pragma once
#include "rteffects.h"
#include "radeonrays.h"
#include "vk_utils.h"


#include <random>
#include <vector>
#include <algorithm>

namespace RTE
{
    struct RtBuffers
    {
        // RNG buffer
        VkMemoryAlloc::StorageBlock rng;
        VkMemoryAlloc::StorageBlock rng_staging;
        // Ray buffer 
        VkMemoryAlloc::StorageBlock rays[2];
        VkMemoryAlloc::StorageBlock shadow_rays;
        // Hit buffer
        VkMemoryAlloc::StorageBlock hits;
        VkMemoryAlloc::StorageBlock ray_count[2];
    };

    class Effect
    {
    public:
        Effect(vk::Device device,
               vk::DescriptorPool descpool,
               vk::CommandPool cmdpool,
               vk::PipelineCache pipeline_cache,
               RtBuffers const& rt_buffers,
               VkMemoryAlloc& alloc,
               rr_instance intersector)
            : device_(device)
            , descpool_(descpool)
            , cmdpool_(cmdpool)
            , pipeline_cache_(pipeline_cache)
            , rt_buffers_(rt_buffers)
            , alloc_(alloc)
            , intersector_(intersector)
        {
        }

        virtual ~Effect() = default;
        virtual void Init(vk::CommandBuffer& commandBuffer) = 0;
        virtual std::vector<vk::CommandBuffer> Apply(rte_scene const& scene,
                                                     rte_gbuffer const& gbuffer,
                                                     rte_output const& output) = 0;
        virtual std::uint32_t GetCommandBuffersCount() = 0;

        Effect(Effect const&) = delete;
        Effect& operator = (Effect const&) = delete;

    protected:
        // Intersection device
        vk::Device device_;
        // Allocate descriptors from here
        vk::DescriptorPool descpool_;
        // Allocate descriptors from here
        vk::CommandPool cmdpool_;
        // Pipeline cache, not used for now
        vk::PipelineCache pipeline_cache_;
        // Device memory allocator
        VkMemoryAlloc& alloc_;
        // Intersector
        rr_instance intersector_;
        // RT buffers
        RtBuffers rt_buffers_;
    };
}

