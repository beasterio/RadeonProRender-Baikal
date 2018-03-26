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
#pragma once
#include <vulkan/vulkan.hpp>

#include "vk_utils.h"
#include "vk_memory_allocator.h"

#include "utils.h"
#include "intersector.h"
#include "bvh.h"
#include "bvh_encoder.h"

#include <map>

namespace RadeonRays {

    // 
    using RayBuffers = std::tuple<VkDescriptorBufferInfo,
        VkDescriptorBufferInfo,
        VkDescriptorBufferInfo>;

    struct CmpRayBuffers
    {
        bool operator()(RayBuffers const& lhs, RayBuffers const& rhs) const
        {
            return std::get<0>(lhs).buffer == std::get<0>(rhs).buffer ?
                (std::get<1>(lhs).buffer == std::get<1>(rhs).buffer ?
                    std::get<2>(lhs).buffer < std::get<2>(rhs).buffer :
                    std::get<1>(lhs).buffer < std::get<1>(rhs).buffer) :
                std::get<0>(lhs).buffer < std::get<0>(rhs).buffer;
        }
    };

    template <typename BVH, typename BVHTraits> 
    class IntersectorLDS : public Intersector {
        static std::uint32_t constexpr kNumBindings = 5u;
        static std::uint32_t constexpr kWorgGroupSize = 64u;

    public:
        IntersectorLDS(
            vk::Device device,
            vk::CommandPool cmdpool,
            vk::DescriptorPool descpool,
            vk::PipelineCache pipeline_cache,
            VkMemoryAlloc& alloc
        );

        ~IntersectorLDS() override;

        vk::CommandBuffer Commit(World const& world) override;
        void BindBuffers(VkDescriptorBufferInfo rays,
                         VkDescriptorBufferInfo hits,
                         VkDescriptorBufferInfo ray_count) override;
        void TraceRays(std::uint32_t max_rays,
                       VkCommandBuffer& command_buffer) override;

        void SetPerformanceQueryInfo(VkQueryPool query_pool, uint32_t begin_query, uint32_t end_query) override;

        IntersectorLDS(IntersectorLDS const&) = delete;
        IntersectorLDS& operator = (IntersectorLDS const&) = delete;

    private:
        // The function checks if BVH buffers have enough space and
        // reallocates if necessary.
        void CheckAndReallocBVH(std::size_t required_size) {
            if (bvh_staging_.size < required_size) {

                alloc_.deallocate(bvh_staging_);
                alloc_.deallocate(bvh_local_);

                bvh_staging_ = alloc_.allocate(
                    vk::MemoryPropertyFlagBits::eHostVisible,
                    vk::BufferUsageFlagBits::eTransferSrc,
                    required_size,
                    16u);

                bvh_local_ = alloc_.allocate(
                    vk::MemoryPropertyFlagBits::eDeviceLocal,
                    vk::BufferUsageFlagBits::eStorageBuffer |
                    vk::BufferUsageFlagBits::eTransferDst,
                    required_size,
                    16u);
            }
        }

        // Check if we have enough memory in the stack buffer
        void CheckAndReallocStackBuffer(std::size_t required_size) {
            if (stack_.size < required_size) {
                alloc_.deallocate(stack_);
                stack_ = alloc_.allocate(
                    vk::MemoryPropertyFlagBits::eDeviceLocal,
                    vk::BufferUsageFlagBits::eStorageBuffer,
                    required_size,
                    16u);
            }
        }

        static std::uint32_t constexpr kInitialWorkBufferSize = 1920u * 1080u;
        static std::uint32_t constexpr kGlobalStackSize = 32u;

        // Intersection device
        vk::Device device_;
        // Allocate command buffers from here
        vk::CommandPool cmdpool_;
        // Allocate descriptors from here
        vk::DescriptorPool descpool_;
        // Pipeline cache, not used for now
        vk::PipelineCache pipeline_cache_;
        // Device memory allocator
        VkMemoryAlloc& alloc_;

        // Intersector compute pipeline layout
        vk::PipelineLayout pipeline_layout_;
        // Intersector compute pipeline
        vk::Pipeline pipeline_;
        // Descriptors layout
        vk::DescriptorSetLayout desc_layout_user_;
        vk::DescriptorSetLayout desc_layout_lib_;
        // Shader module
        vk::ShaderModule shader_;
        // Descriptor sets
        std::vector<vk::DescriptorSet> desc_sets_;


        std::map<RayBuffers, std::vector<vk::DescriptorSet>, CmpRayBuffers> desc_map_;
        RayBuffers ray_buffers_;

        // Performance query info
        uint32_t    begin_query_idx_;
        uint32_t    end_query_idx_;
        VkQueryPool query_pool_;

        // Device local stack buffer
        VkMemoryAlloc::StorageBlock stack_;
        VkMemoryAlloc::StorageBlock bvh_staging_;
        VkMemoryAlloc::StorageBlock bvh_local_;
    };

    template <typename BVH, typename BVHTraits>
    inline IntersectorLDS<BVH, BVHTraits>::IntersectorLDS(
        vk::Device device,
        vk::CommandPool cmdpool,
        vk::DescriptorPool descpool,
        vk::PipelineCache pipeline_cache,
        VkMemoryAlloc& alloc
    )
        : device_(device)
        , cmdpool_(cmdpool)
        , descpool_(descpool)
        , pipeline_cache_(pipeline_cache)
        , alloc_(alloc)
        , begin_query_idx_(-1)
        , end_query_idx_(-1)
        , query_pool_(nullptr) {
        // Initialize bindings
        // Rays, hits and ray count
        vk::DescriptorSetLayoutBinding desc_set_layout_user[] =
        {
            vk::DescriptorSetLayoutBinding(0u, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eCompute),
            vk::DescriptorSetLayoutBinding(1u, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eCompute),
            vk::DescriptorSetLayoutBinding(2u, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eCompute)
        };

        // BVH and stack
        vk::DescriptorSetLayoutBinding desc_set_layout_lib[] =
        {
            vk::DescriptorSetLayoutBinding(3u, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eCompute),
            vk::DescriptorSetLayoutBinding(4u, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eCompute)
        };

        // Create descriptor set layout
        vk::DescriptorSetLayoutCreateInfo desc_layout_create_info;
        desc_layout_create_info
            .setBindingCount(sizeof(desc_set_layout_user) / sizeof(vk::DescriptorSetLayoutBinding))
            .setPBindings(desc_set_layout_user);
        desc_layout_user_ = device_
            .createDescriptorSetLayout(desc_layout_create_info);
        desc_layout_create_info
            .setBindingCount(sizeof(desc_set_layout_lib) / sizeof(vk::DescriptorSetLayoutBinding))
            .setPBindings(desc_set_layout_lib);
        desc_layout_lib_ = device_
            .createDescriptorSetLayout(desc_layout_create_info);

        vk::DescriptorSetLayout desc_set_layouts[] = 
        {
            desc_layout_user_,
            desc_layout_lib_
        };

        // Allocate descriptors
        vk::DescriptorSetAllocateInfo desc_alloc_info;
        desc_alloc_info.setDescriptorPool(descpool_);
        desc_alloc_info.setDescriptorSetCount(1);
        desc_alloc_info.setPSetLayouts(&desc_set_layouts[1]);
        desc_sets_ = device_.allocateDescriptorSets(desc_alloc_info);

        // Ray count is a push constant, so create a range for it
        vk::PushConstantRange push_constant_range;
        push_constant_range.setOffset(0)
            .setSize(sizeof(std::uint32_t))
            .setStageFlags(vk::ShaderStageFlagBits::eCompute);

        // Create pipeline layout
        vk::PipelineLayoutCreateInfo pipeline_layout_create_info;
        pipeline_layout_create_info
            .setSetLayoutCount(sizeof(desc_set_layouts) / sizeof(vk::DescriptorSetLayout))
            .setPSetLayouts(desc_set_layouts)
            .setPushConstantRangeCount(1)
            .setPPushConstantRanges(&push_constant_range);
        pipeline_layout_ = device_
            .createPipelineLayout(pipeline_layout_create_info);

        // Load intersection shader module
        std::string path = "../external/radeonrays-next/shaders/";
        path.append(BVHTraits::GetGPUTraversalFileName());
        shader_ = LoadShaderModule(device_, path);

        // Create pipeline 
        vk::PipelineShaderStageCreateInfo shader_stage_create_info;
        shader_stage_create_info
            .setStage(vk::ShaderStageFlagBits::eCompute)
            .setModule(shader_)
            .setPName("main");

        vk::ComputePipelineCreateInfo pipeline_create_info;
        pipeline_create_info
            .setLayout(pipeline_layout_)
            .setStage(shader_stage_create_info);

        pipeline_ = device_.createComputePipeline(
            pipeline_cache_,
            pipeline_create_info);

        stack_ = alloc_.allocate(
            vk::MemoryPropertyFlagBits::eDeviceLocal,
            vk::BufferUsageFlagBits::eStorageBuffer,
            kInitialWorkBufferSize * kGlobalStackSize * sizeof(std::uint32_t),
            16u);
    }

    template <typename BVH, typename BVHTraits>
    inline IntersectorLDS<BVH, BVHTraits>::~IntersectorLDS() {
        alloc_.deallocate(stack_);
        alloc_.deallocate(bvh_local_);
        alloc_.deallocate(bvh_staging_);
        device_.destroyPipelineLayout(pipeline_layout_);
        device_.destroyPipeline(pipeline_);
        device_.destroyDescriptorSetLayout(desc_layout_user_);
        device_.destroyDescriptorSetLayout(desc_layout_lib_);
        device_.destroyShaderModule(shader_);
    }
    template <typename BVH, typename BVHTraits>
    inline
    void  IntersectorLDS<BVH, BVHTraits>::BindBuffers(VkDescriptorBufferInfo rays,
                                                      VkDescriptorBufferInfo hits,
                                                      VkDescriptorBufferInfo ray_count)
    {
        auto ray_buffers = std::make_tuple(rays, hits, ray_count);

        ray_buffers_ = ray_buffers;

        auto ray_buffers_desc_iter = desc_map_.find(ray_buffers_);

        if (ray_buffers_desc_iter == desc_map_.cend())
        {
            vk::DescriptorSetLayout desc_set_layouts[] =
            {
                desc_layout_user_
            };

            // Allocate descriptors
            vk::DescriptorSetAllocateInfo desc_alloc_info;
            desc_alloc_info.setDescriptorPool(descpool_);
            desc_alloc_info.setDescriptorSetCount(sizeof(desc_set_layouts) / sizeof(vk::DescriptorSetLayout));
            desc_alloc_info.setPSetLayouts(desc_set_layouts);
            auto desc_sets = device_.allocateDescriptorSets(desc_alloc_info);

            vk::DescriptorBufferInfo desc_buffer_info[] =
            {
                rays, hits, ray_count
            };

            // Update in-lib descriptor sets
            vk::WriteDescriptorSet desc_writes;
            desc_writes
                .setDescriptorCount(sizeof(desc_buffer_info) / sizeof(vk::DescriptorBufferInfo))
                .setDescriptorType(vk::DescriptorType::eStorageBuffer)
                .setDstSet(desc_sets[0])
                .setDstBinding(0)
                .setPBufferInfo(desc_buffer_info);

            device_.updateDescriptorSets(desc_writes, nullptr);

            desc_map_[ray_buffers_] = std::move(desc_sets);
        }
    }

    template <typename BVH, typename BVHTraits>
    inline
    vk::CommandBuffer IntersectorLDS<BVH, BVHTraits>::Commit(World const& world) {
        BVH bvh;

        // Build BVH
        bvh.Build(world.cbegin(), world.cend());

        // Calculate BVH size
        auto bvh_size_in_bytes = BVHTraits::GetSizeInBytes(bvh);

#ifdef TEST
        std::cout << "BVH size is " << bvh_size_in_bytes / 1024.f / 1024.f << "MB";
#endif
        // Check if we have enough space for BVH 
        // and realloc buffers if necessary
        CheckAndReallocBVH(bvh_size_in_bytes);

        // Map staging buffer
        auto ptr = device_.mapMemory(
                bvh_staging_.memory,
                bvh_staging_.offset,
                bvh_size_in_bytes);

        // Copy BVH data
        BVHTraits::StreamBVH(bvh, ptr);

        auto mapped_range = vk::MappedMemoryRange{}
            .setMemory(bvh_staging_.memory)
            .setOffset(bvh_staging_.offset)
            .setSize(bvh_size_in_bytes);

        // Flush range
        device_.flushMappedMemoryRanges(mapped_range);
        device_.unmapMemory(bvh_staging_.memory);

        vk::DescriptorBufferInfo desc_buffer_info[] =
        {
            vk::DescriptorBufferInfo(bvh_local_.buffer, 0u, bvh_local_.size),
            vk::DescriptorBufferInfo(stack_.buffer, 0u, stack_.size)
        };

        // Update in-lib descriptor sets
        vk::WriteDescriptorSet desc_writes;
        desc_writes
            .setDescriptorCount(sizeof(desc_buffer_info) / sizeof(vk::DescriptorBufferInfo))
            .setDescriptorType(vk::DescriptorType::eStorageBuffer)
            .setDstSet(desc_sets_[0])
            .setDstBinding(3)
            .setPBufferInfo(desc_buffer_info);

        device_.updateDescriptorSets(desc_writes, nullptr);

        // Allocate command buffer
        vk::CommandBufferAllocateInfo cmdbuffer_alloc_info;
        cmdbuffer_alloc_info
            .setCommandBufferCount(1)
            .setCommandPool(cmdpool_)
            .setLevel(vk::CommandBufferLevel::ePrimary);

        auto cmdbuffers
            = device_.allocateCommandBuffers(cmdbuffer_alloc_info);

        // Begin command buffer
        vk::CommandBufferBeginInfo cmdbuffer_buffer_begin_info;
        cmdbuffers[0].begin(cmdbuffer_buffer_begin_info);

        // Copy BVH data from staging to local
        vk::BufferCopy cmd_copy;
        cmd_copy.setSize(bvh_size_in_bytes);
        cmdbuffers[0].copyBuffer(
            bvh_staging_.buffer,
            bvh_local_.buffer,
            cmd_copy);

        // Issue memory barrier for BVH data
        vk::BufferMemoryBarrier memory_barrier;
        memory_barrier
            .setBuffer(bvh_local_.buffer)
            .setOffset(0)
            .setSize(bvh_size_in_bytes)
            .setSrcAccessMask(vk::AccessFlagBits::eTransferWrite)
            .setDstAccessMask(vk::AccessFlagBits::eShaderRead);

        cmdbuffers[0].pipelineBarrier(
            vk::PipelineStageFlagBits::eTransfer,
            vk::PipelineStageFlagBits::eComputeShader,
            vk::DependencyFlags{},
            nullptr,
            memory_barrier,
            nullptr
        );

        // End command buffer
        cmdbuffers[0].end();

        return cmdbuffers[0];
    }

    template <typename BVH, typename BVHTraits>
    inline
    void IntersectorLDS<BVH, BVHTraits>::TraceRays(std::uint32_t max_rays,
                                                   VkCommandBuffer& command_buffer)
    {
        // Check if we have enough stack memory
        auto stack_size_in_bytes =
            max_rays * kGlobalStackSize * sizeof(std::uint32_t);
        CheckAndReallocStackBuffer(stack_size_in_bytes);

        vk::CommandBuffer cmdbuffers[1] = { command_buffer };

        // Begin command buffer recording
        vk::CommandBufferBeginInfo cmdbuffer_begin_info;
        cmdbuffers[0].begin(cmdbuffer_begin_info);

        // Bind intersection pipeline
        cmdbuffers[0].bindPipeline(
            vk::PipelineBindPoint::eCompute,
            pipeline_);

        auto desc_set = desc_map_[ray_buffers_];
        desc_set.insert(desc_set.end(), desc_sets_.begin(), desc_sets_.end());

        // Bind descriptor sets
        cmdbuffers[0].bindDescriptorSets(
            vk::PipelineBindPoint::eCompute,
            pipeline_layout_,
            0,
            desc_set,
            nullptr);

        // Push constants
        auto N = static_cast<std::uint32_t>(max_rays);
        cmdbuffers[0].pushConstants(
            pipeline_layout_,
            vk::ShaderStageFlagBits::eCompute,
            0u,
            sizeof(std::uint32_t),
            &N);

        if (begin_query_idx_ != -1 && end_query_idx_ != -1 && query_pool_ != nullptr)
        {
            cmdbuffers[0].resetQueryPool(query_pool_, begin_query_idx_, 1);
            cmdbuffers[0].writeTimestamp(vk::PipelineStageFlagBits::eAllCommands, query_pool_, begin_query_idx_);
        }

        // Dispatch intersection shader
        auto num_groups = (max_rays + kWorgGroupSize - 1) / kWorgGroupSize;
        cmdbuffers[0].dispatch(num_groups, 1, 1);

        if (begin_query_idx_ != -1 && end_query_idx_ != -1 && query_pool_ != nullptr)
        {
            cmdbuffers[0].resetQueryPool(query_pool_, end_query_idx_, 1);
            cmdbuffers[0].writeTimestamp(vk::PipelineStageFlagBits::eAllCommands, query_pool_, end_query_idx_);
        }

        // End command buffer
        cmdbuffers[0].end();
    }

    template <typename BVH, typename BVHTraits>
    inline void IntersectorLDS<BVH, BVHTraits>::SetPerformanceQueryInfo(VkQueryPool query_pool, uint32_t begin_query_idx, uint32_t end_query_idx) {
        begin_query_idx_ = begin_query_idx;
        end_query_idx_ = end_query_idx;
        query_pool_ = query_pool;
    }
}
