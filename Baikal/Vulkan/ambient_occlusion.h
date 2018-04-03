#pragma once

#include "effect.h"
#include "radeonrays.h"
#include "vk_utils.h"
#include "vk_memory_allocator.h"

#include <random>
#include <vector>
#include <algorithm>

namespace RTE
{
    class AmbientOcclusion : public Effect
    {
    public:
        AmbientOcclusion(vk::Device device,
            vk::DescriptorPool descpool,
            vk::CommandPool cmdpool,
            vk::PipelineCache pipeline_cache,
            RtBuffers const& rt_buffers,
            VkMemoryAlloc& alloc,
            rr_instance intersector) :
            Effect(device, descpool, cmdpool, pipeline_cache, rt_buffers, alloc, intersector)
        {
            {
                vk::DescriptorSetLayoutBinding layout_binding[] =
                {
                    vk::DescriptorSetLayoutBinding(0u, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eCompute),
                    vk::DescriptorSetLayoutBinding(1u, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eCompute),
                    vk::DescriptorSetLayoutBinding(2u, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eCompute),
                    vk::DescriptorSetLayoutBinding(3u, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eCompute),
                    vk::DescriptorSetLayoutBinding(4u, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eCompute),
                    vk::DescriptorSetLayoutBinding(5u, vk::DescriptorType::eUniformBuffer, 1, vk::ShaderStageFlagBits::eCompute)
                };

                // Create AO rays pipeline

                // Create descriptor set layout
                vk::DescriptorSetLayoutCreateInfo desc_layout_create_info;
                desc_layout_create_info
                    .setBindingCount(sizeof(layout_binding) / sizeof(vk::DescriptorSetLayoutBinding))
                    .setPBindings(layout_binding);
                spawn_rays_.desc_layout_ = device_
                    .createDescriptorSetLayout(desc_layout_create_info);

                // Allocate descriptors
                vk::DescriptorSetAllocateInfo desc_alloc_info;
                desc_alloc_info.setDescriptorPool(descpool_);
                desc_alloc_info.setDescriptorSetCount(1);
                desc_alloc_info.setPSetLayouts(&spawn_rays_.desc_layout_);
                spawn_rays_.descsets_ = device_.allocateDescriptorSets(desc_alloc_info);

                // Create pipeline layout
                vk::PipelineLayoutCreateInfo pipeline_layout_create_info;
                pipeline_layout_create_info
                    .setSetLayoutCount(1)
                    .setPSetLayouts(&spawn_rays_.desc_layout_);
                spawn_rays_.pipeline_layout_ = device_
                    .createPipelineLayout(pipeline_layout_create_info);

                // Load intersection shader module
                std::string path = "../Baikal/Kernels/Vk/shaders/ao_spawn_rays.comp.spv";
                spawn_rays_.shader_ = RadeonRays::LoadShaderModule(device_, path);

                // Create pipeline 
                vk::PipelineShaderStageCreateInfo shader_stage_create_info;
                shader_stage_create_info
                    .setStage(vk::ShaderStageFlagBits::eCompute)
                    .setModule(spawn_rays_.shader_)
                    .setPName("main");

                vk::ComputePipelineCreateInfo pipeline_create_info;
                pipeline_create_info
                    .setLayout(spawn_rays_.pipeline_layout_)
                    .setStage(shader_stage_create_info);

                spawn_rays_.pipeline_ = device_.createComputePipeline(
                    pipeline_cache_,
                    pipeline_create_info);
            }

            // Create resolve pipeline
            {
                vk::DescriptorSetLayoutBinding layout_binding[] =
                {
                    vk::DescriptorSetLayoutBinding(0u, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eCompute),
                    vk::DescriptorSetLayoutBinding(1u, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eCompute),
                    vk::DescriptorSetLayoutBinding(2u, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eCompute),
                    vk::DescriptorSetLayoutBinding(3u, vk::DescriptorType::eUniformBuffer, 1, vk::ShaderStageFlagBits::eCompute)
                };

                // Create descriptor set layout
                vk::DescriptorSetLayoutCreateInfo desc_layout_create_info;
                desc_layout_create_info
                    .setBindingCount(sizeof(layout_binding) / sizeof(vk::DescriptorSetLayoutBinding))
                    .setPBindings(layout_binding);
                resolve_.desc_layout_ = device_
                    .createDescriptorSetLayout(desc_layout_create_info);

                // Allocate descriptors
                vk::DescriptorSetAllocateInfo desc_alloc_info;
                desc_alloc_info.setDescriptorPool(descpool_);
                desc_alloc_info.setDescriptorSetCount(1);
                desc_alloc_info.setPSetLayouts(&resolve_.desc_layout_);
                resolve_.descsets_ = device_.allocateDescriptorSets(desc_alloc_info);

                // Create pipeline layout
                vk::PipelineLayoutCreateInfo pipeline_layout_create_info;
                pipeline_layout_create_info
                    .setSetLayoutCount(1)
                    .setPSetLayouts(&resolve_.desc_layout_);

                resolve_.pipeline_layout_ = device_
                    .createPipelineLayout(pipeline_layout_create_info);

                // Load intersection shader module
                std::string path = "../Baikal/Kernels/Vk/shaders/ao_resolve.comp.spv";
                resolve_.shader_ = RadeonRays::LoadShaderModule(device_, path);

                // Create pipeline 
                vk::PipelineShaderStageCreateInfo shader_stage_create_info;
                shader_stage_create_info
                    .setStage(vk::ShaderStageFlagBits::eCompute)
                    .setModule(resolve_.shader_)
                    .setPName("main");

                vk::ComputePipelineCreateInfo pipeline_create_info;
                pipeline_create_info
                    .setLayout(resolve_.pipeline_layout_)
                    .setStage(shader_stage_create_info);

                resolve_.pipeline_ = device_.createComputePipeline(
                    pipeline_cache_,
                    pipeline_create_info);
            }
        }

        std::uint32_t  GetCommandBuffersCount() override
        {
            return 3u;
        }

        void Init(vk::CommandBuffer& commandBuffer) override
        {
            auto rng_buffer_size_in_bytes = rt_buffers_.rng.size;

            // Map staging buffer
            auto ptr = device_.mapMemory(
                rt_buffers_.rng_staging.memory,
                rt_buffers_.rng_staging.offset,
                rng_buffer_size_in_bytes);

            // Generate random data 
            {
                auto iter = reinterpret_cast<uint32_t*>(ptr);
                std::random_device random_device;
                std::mt19937 rng(random_device());
                std::uniform_int_distribution<> distribution(1u, 0x7fffffffu);
                std::generate(iter, iter + rng_buffer_size_in_bytes / sizeof(std::uint32_t), [&distribution, &rng]
                { return distribution(rng); });
            }

            auto mapped_range = vk::MappedMemoryRange{}
                .setMemory(rt_buffers_.rng_staging.memory)
                .setOffset(rt_buffers_.rng_staging.offset)
                .setSize(rng_buffer_size_in_bytes);

            // Flush range
            device_.flushMappedMemoryRanges(mapped_range);
            device_.unmapMemory(rt_buffers_.rng_staging.memory);

            // Copy BVH data from staging to local
            vk::BufferCopy cmd_copy;
            cmd_copy.setSize(rng_buffer_size_in_bytes);
            commandBuffer.copyBuffer(
                rt_buffers_.rng_staging.buffer,
                rt_buffers_.rng.buffer,
                cmd_copy);

            // Issue memory barrier for BVH data
            vk::BufferMemoryBarrier memory_barrier;
            memory_barrier
                .setBuffer(rt_buffers_.rng_staging.buffer)
                .setOffset(0)
                .setSize(rng_buffer_size_in_bytes)
                .setSrcAccessMask(vk::AccessFlagBits::eTransferWrite)
                .setDstAccessMask(vk::AccessFlagBits::eShaderRead);

            commandBuffer.pipelineBarrier(
                vk::PipelineStageFlagBits::eTransfer,
                vk::PipelineStageFlagBits::eComputeShader,
                vk::DependencyFlags{},
                nullptr,
                memory_barrier,
                nullptr
            );
        }

        ~AmbientOcclusion()
        {
            device_.destroyPipeline(spawn_rays_.pipeline_);
            device_.destroyPipelineLayout(spawn_rays_.pipeline_layout_);
            device_.destroyDescriptorSetLayout(spawn_rays_.desc_layout_);
            device_.destroyShaderModule(spawn_rays_.shader_);
        }

        std::vector<vk::CommandBuffer> Apply(rte_scene const& scene,
            rte_gbuffer const& gbuffer,
            rte_output const& output) override
        {
            // Allocate command buffer
            vk::CommandBufferAllocateInfo cmdbuffer_alloc_info;
            cmdbuffer_alloc_info
                .setCommandBufferCount(3)
                .setCommandPool(cmdpool_)
                .setLevel(vk::CommandBufferLevel::ePrimary);

            auto cmdbuffers = device_.allocateCommandBuffers(cmdbuffer_alloc_info);

            // Spawn rays pass
            {
                // Update descriptors
                vk::DescriptorImageInfo imageInfos[] =
                {
                    vk::DescriptorImageInfo(gbuffer.depthAndNormal),
                    vk::DescriptorImageInfo(gbuffer.albedo),
                    vk::DescriptorImageInfo(gbuffer.pbrInputs)
                };

                vk::DescriptorBufferInfo bufferInfos[] =
                {
                    vk::DescriptorBufferInfo(rt_buffers_.rays[0].buffer, 0u, rt_buffers_.rays[0].size),
                    vk::DescriptorBufferInfo(rt_buffers_.rng.buffer, 0u, rt_buffers_.rng.size),
                    vk::DescriptorBufferInfo(scene.camera)
                };

                // Write buffer descriptors
                std::vector<vk::WriteDescriptorSet> descWrites(3);
                descWrites[0]
                    .setDescriptorCount(2)
                    .setDescriptorType(vk::DescriptorType::eStorageBuffer)
                    .setDstSet(spawn_rays_.descsets_[0])
                    .setDstBinding(3)
                    .setPBufferInfo(&bufferInfos[0]);
                descWrites[1]
                    .setDescriptorCount(1)
                    .setDescriptorType(vk::DescriptorType::eUniformBuffer)
                    .setDstSet(spawn_rays_.descsets_[0])
                    .setDstBinding(5)
                    .setPBufferInfo(&bufferInfos[2]);
                // Write image descriptors
                descWrites[2]
                    .setDescriptorCount(sizeof(imageInfos) / sizeof(vk::DescriptorImageInfo))
                    .setDescriptorType(vk::DescriptorType::eCombinedImageSampler)
                    .setDstSet(spawn_rays_.descsets_[0])
                    .setDstBinding(0)
                    .setPImageInfo(&imageInfos[0]);

                device_.updateDescriptorSets(descWrites, nullptr);

                // Begin command buffer
                vk::CommandBufferBeginInfo cmdbuffer_buffer_begin_info;
                cmdbuffers[0].begin(cmdbuffer_buffer_begin_info);

                cmdbuffers[0].fillBuffer(rt_buffers_.ray_count[0].buffer, 
                                         0u,
                                         sizeof(std::uint32_t),
                                         output.height * output.width);

                {
                    vk::BufferMemoryBarrier memory_barrier;
                    memory_barrier
                        .setBuffer(rt_buffers_.ray_count[0].buffer)
                        .setOffset(0u)
                        .setSize(sizeof(std::uint32_t))
                        .setSrcAccessMask(vk::AccessFlagBits::eTransferWrite)
                        .setDstAccessMask(vk::AccessFlagBits::eShaderRead);

                    cmdbuffers[0].pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
                        vk::PipelineStageFlagBits::eComputeShader,
                        vk::DependencyFlags{},
                        nullptr,
                        memory_barrier,
                        nullptr);
                }

                cmdbuffers[0].bindPipeline(vk::PipelineBindPoint::eCompute, spawn_rays_.pipeline_);

                cmdbuffers[0].bindDescriptorSets(vk::PipelineBindPoint::eCompute,
                    spawn_rays_.pipeline_layout_,
                    0,
                    spawn_rays_.descsets_,
                    nullptr);

                uint32_t groupSize = 16;
                uint32_t groupCountX = (output.width + groupSize - 1) / groupSize;
                uint32_t groupCountY = (output.height + groupSize - 1) / groupSize;

                cmdbuffers[0].dispatch(groupCountX, groupCountY, 1);
                cmdbuffers[0].end();
            }


            // Trace rays
            {
                VkDescriptorBufferInfo rays_desc
                {
                    rt_buffers_.rays[0].buffer, 0u, rt_buffers_.rays[0].size,
                };

                VkDescriptorBufferInfo hits_desc
                {
                    rt_buffers_.hits.buffer, 0u, rt_buffers_.hits.size,
                };

                VkDescriptorBufferInfo ray_count_desc
                {
                    rt_buffers_.ray_count[0].buffer, 0u, rt_buffers_.ray_count[0].size,
                };

                rrBindBuffers(intersector_, rays_desc, hits_desc, ray_count_desc);

                VkCommandBuffer cmdBuf = cmdbuffers[1];
                rrTraceRays(intersector_, RR_QUERY_INTERSECT,
                    output.width * output.height, 
                    &cmdBuf);
            }

            // Resolve pass
            {
                // Update descriptors
                vk::DescriptorImageInfo imageInfos[] =
                {
                    vk::DescriptorImageInfo(output.image),
                    vk::DescriptorImageInfo(output.sampleCounter)
                };

                vk::DescriptorBufferInfo bufferInfos[] =
                {
                    vk::DescriptorBufferInfo(rt_buffers_.hits.buffer, 0u, rt_buffers_.hits.size),
                    vk::DescriptorBufferInfo(scene.camera)
                };

                // Write buffer descriptors
                std::vector<vk::WriteDescriptorSet> descWrites(3);
                descWrites[0]
                    .setDescriptorCount(1)
                    .setDescriptorType(vk::DescriptorType::eStorageBuffer)
                    .setDstSet(resolve_.descsets_[0])
                    .setDstBinding(2)
                    .setPBufferInfo(&bufferInfos[0]);
                descWrites[1]
                    .setDescriptorCount(1)
                    .setDescriptorType(vk::DescriptorType::eUniformBuffer)
                    .setDstSet(resolve_.descsets_[0])
                    .setDstBinding(3)
                    .setPBufferInfo(&bufferInfos[1]);
                // Write image descriptors
                descWrites[2]
                    .setDescriptorCount(sizeof(imageInfos) / sizeof(vk::DescriptorImageInfo))
                    .setDescriptorType(vk::DescriptorType::eStorageImage)
                    .setDstSet(resolve_.descsets_[0])
                    .setDstBinding(0)
                    .setPImageInfo(&imageInfos[0]);

                device_.updateDescriptorSets(descWrites, nullptr);

                // Begin command buffer
                vk::CommandBufferBeginInfo cmdbuffer_buffer_begin_info;
                cmdbuffers[2].begin(cmdbuffer_buffer_begin_info);

                cmdbuffers[2].bindPipeline(vk::PipelineBindPoint::eCompute, resolve_.pipeline_);
                cmdbuffers[2].bindDescriptorSets(vk::PipelineBindPoint::eCompute,
                    resolve_.pipeline_layout_,
                    0,
                    resolve_.descsets_,
                    nullptr);

                uint32_t groupSize = 16;
                uint32_t groupCountX = (output.width + groupSize - 1) / groupSize;
                uint32_t groupCountY = (output.height + groupSize - 1) / groupSize;

                cmdbuffers[2].dispatch(groupCountX, groupCountY, 1);
                cmdbuffers[2].end();
            }

            return cmdbuffers;
        }

    private:

        struct Pass
        {
            // Intersector compute pipeline layout
            vk::PipelineLayout pipeline_layout_;
            // Intersector compute pipeline
            vk::Pipeline pipeline_;
            // Descriptors layout
            vk::DescriptorSetLayout desc_layout_;
            // Shader module
            vk::ShaderModule shader_;
            // Descriptor sets
            std::vector<vk::DescriptorSet> descsets_;
        };

        Pass spawn_rays_;
        Pass resolve_;
    };
}
