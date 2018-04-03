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
    class GlobalIllumination : public Effect
    {
    public:
        GlobalIllumination(vk::Device device,
            vk::DescriptorPool descpool,
            vk::CommandPool cmdpool,
            vk::PipelineCache pipeline_cache,
            RtBuffers const& rt_buffers,
            VkMemoryAlloc& alloc,
            rr_instance intersector) :
            Effect(device, descpool, cmdpool, pipeline_cache, rt_buffers, alloc, intersector)
            , m_num_bounces(4u)
        {
            {
                vk::DescriptorSetLayoutBinding layout_binding[] =
                {
                    vk::DescriptorSetLayoutBinding(0u, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eCompute),
                    vk::DescriptorSetLayoutBinding(1u, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eCompute),
                    vk::DescriptorSetLayoutBinding(2u, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eCompute),
                    vk::DescriptorSetLayoutBinding(5u, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eCompute),
                    vk::DescriptorSetLayoutBinding(6u, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eCompute),
                    vk::DescriptorSetLayoutBinding(7u, vk::DescriptorType::eUniformBuffer, 1, vk::ShaderStageFlagBits::eCompute),
                    vk::DescriptorSetLayoutBinding(8u, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eCompute),
                };

                vk::DescriptorSetLayoutBinding layout_binding_rays[] =
                {
                    vk::DescriptorSetLayoutBinding(3u, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eCompute),
                    vk::DescriptorSetLayoutBinding(4u, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eCompute)
                };

                // Create descriptor set layout
                vk::DescriptorSetLayoutCreateInfo desc_layout_create_info;
                desc_layout_create_info
                    .setBindingCount(sizeof(layout_binding) / sizeof(vk::DescriptorSetLayoutBinding))
                    .setPBindings(layout_binding);
                spawn_rays_.desc_layout_ = device_
                    .createDescriptorSetLayout(desc_layout_create_info);

                // Create descriptor set layout
                desc_layout_create_info
                    .setBindingCount(sizeof(layout_binding_rays) / sizeof(vk::DescriptorSetLayoutBinding))
                    .setPBindings(layout_binding_rays);
                spawn_rays_.desc_layout_rays_ = device_
                    .createDescriptorSetLayout(desc_layout_create_info);

                // Allocate descriptors
                vk::DescriptorSetAllocateInfo desc_alloc_info;
                desc_alloc_info.setDescriptorPool(descpool_);
                desc_alloc_info.setDescriptorSetCount(1);
                desc_alloc_info.setPSetLayouts(&spawn_rays_.desc_layout_);
                spawn_rays_.descsets_ = device_.allocateDescriptorSets(desc_alloc_info);

                desc_alloc_info.setDescriptorPool(descpool_);
                desc_alloc_info.setDescriptorSetCount(1);
                desc_alloc_info.setPSetLayouts(&spawn_rays_.desc_layout_rays_);
                spawn_rays_.descsets_rays_ = device_.allocateDescriptorSets(desc_alloc_info);

                vk::DescriptorSetLayout layouts[] =
                {
                    spawn_rays_.desc_layout_,
                    spawn_rays_.desc_layout_rays_
                };

                // Create pipeline layout
                vk::PipelineLayoutCreateInfo pipeline_layout_create_info;
                pipeline_layout_create_info
                    .setSetLayoutCount(sizeof(layouts) / sizeof(vk::DescriptorSetLayout))
                    .setPSetLayouts(layouts);
                spawn_rays_.pipeline_layout_ = device_
                    .createPipelineLayout(pipeline_layout_create_info);

                // Load intersection shader module
                std::string path = "../Baikal/Kernels/Vk/shaders/gi_spawn_rays_from_gbuffer.comp.spv";
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

                vk::DescriptorBufferInfo bufferInfos[] =
                {
                    vk::DescriptorBufferInfo(rt_buffers_.rays[0].buffer, 0u, rt_buffers_.rays[0].size),
                    vk::DescriptorBufferInfo(rt_buffers_.ray_count[0].buffer, 0u, rt_buffers_.ray_count[0].size)
                };

                // Write buffer descriptors
                std::vector<vk::WriteDescriptorSet> descWrites(1);
                descWrites[0]
                    .setDescriptorCount(sizeof(bufferInfos) / sizeof(vk::DescriptorBufferInfo))
                    .setDescriptorType(vk::DescriptorType::eStorageBuffer)
                    .setDstSet(spawn_rays_.descsets_rays_[0])
                    .setDstBinding(3)
                    .setPBufferInfo(&bufferInfos[0]);

                device_.updateDescriptorSets(descWrites, nullptr);
            }

            // Create sample pipeline
            {
                vk::DescriptorSetLayoutBinding layout_binding[] =
                {
                    vk::DescriptorSetLayoutBinding(0u, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eCompute),
                    vk::DescriptorSetLayoutBinding(1u, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eCompute),
                    vk::DescriptorSetLayoutBinding(2u, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eCompute),
                    vk::DescriptorSetLayoutBinding(3u, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eCompute),
                    vk::DescriptorSetLayoutBinding(4u, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eCompute),
                    vk::DescriptorSetLayoutBinding(7u, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eCompute),
                    vk::DescriptorSetLayoutBinding(8u, vk::DescriptorType::eUniformBuffer, 1, vk::ShaderStageFlagBits::eCompute),
                    vk::DescriptorSetLayoutBinding(9u, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eCompute),
                    vk::DescriptorSetLayoutBinding(10u, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eCompute),
                    vk::DescriptorSetLayoutBinding(11u, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eCompute),
                    vk::DescriptorSetLayoutBinding(12u, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eCompute),
                    vk::DescriptorSetLayoutBinding(13u, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eCompute),
                    vk::DescriptorSetLayoutBinding(14u, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eCompute),
                    vk::DescriptorSetLayoutBinding(15u, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eCompute),
                    vk::DescriptorSetLayoutBinding(16u, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eCompute),
                    vk::DescriptorSetLayoutBinding(17u, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eCompute),
                    vk::DescriptorSetLayoutBinding(18u, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eCompute),
                    vk::DescriptorSetLayoutBinding(19u, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eCompute)
                };

                vk::DescriptorSetLayoutBinding layout_binding_rays[] =
                {
                    vk::DescriptorSetLayoutBinding(5u, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eCompute),
                    vk::DescriptorSetLayoutBinding(6u, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eCompute),
                    vk::DescriptorSetLayoutBinding(20u, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eCompute),
                    vk::DescriptorSetLayoutBinding(21u, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eCompute),
                };

                // Create descriptor set layout
                vk::DescriptorSetLayoutCreateInfo desc_layout_create_info;
                desc_layout_create_info
                    .setBindingCount(sizeof(layout_binding) / sizeof(vk::DescriptorSetLayoutBinding))
                    .setPBindings(layout_binding);
                sample_lights_.desc_layout_ = device_
                    .createDescriptorSetLayout(desc_layout_create_info);
                desc_layout_create_info
                    .setBindingCount(sizeof(layout_binding_rays) / sizeof(vk::DescriptorSetLayoutBinding))
                    .setPBindings(layout_binding_rays);
                sample_lights_.desc_layout_rays_ = device_
                    .createDescriptorSetLayout(desc_layout_create_info);

                // Allocate descriptors
                vk::DescriptorSetAllocateInfo desc_alloc_info;
                desc_alloc_info.setDescriptorPool(descpool_);
                desc_alloc_info.setDescriptorSetCount(1);
                desc_alloc_info.setPSetLayouts(&sample_lights_.desc_layout_);
                sample_lights_.descsets_ = device_.allocateDescriptorSets(desc_alloc_info);


                vk::DescriptorSetLayout layouts_rays[] =
                {
                    sample_lights_.desc_layout_rays_,
                    sample_lights_.desc_layout_rays_
                };

                desc_alloc_info.setDescriptorPool(descpool_);
                desc_alloc_info.setDescriptorSetCount(sizeof(layouts_rays) / sizeof(vk::DescriptorSetLayout));
                desc_alloc_info.setPSetLayouts(layouts_rays);
                sample_lights_.descsets_rays_ = device_.allocateDescriptorSets(desc_alloc_info);

                // Ray count is a push constant, so create a range for it
                vk::PushConstantRange push_constant_range;
                push_constant_range.setOffset(0)
                    .setSize(sizeof(std::uint32_t))
                    .setStageFlags(vk::ShaderStageFlagBits::eCompute);

                vk::DescriptorSetLayout layouts[] =
                {
                    sample_lights_.desc_layout_,
                    sample_lights_.desc_layout_rays_
                };

                // Create pipeline layout
                vk::PipelineLayoutCreateInfo pipeline_layout_create_info;
                pipeline_layout_create_info
                    .setSetLayoutCount(sizeof(layouts) / sizeof (vk::DescriptorSetLayout))
                    .setPSetLayouts(layouts)
                    .setPushConstantRangeCount(1)
                    .setPPushConstantRanges(&push_constant_range);

                sample_lights_.pipeline_layout_ = device_
                    .createPipelineLayout(pipeline_layout_create_info);

                // Load intersection shader module
                std::string path = "../Baikal/Kernels/Vk/shaders/gi_sample_lights.comp.spv";
                sample_lights_.shader_ = RadeonRays::LoadShaderModule(device_, path);

                // Create pipeline 
                vk::PipelineShaderStageCreateInfo shader_stage_create_info;
                shader_stage_create_info
                    .setStage(vk::ShaderStageFlagBits::eCompute)
                    .setModule(sample_lights_.shader_)
                    .setPName("main");

                vk::ComputePipelineCreateInfo pipeline_create_info;
                pipeline_create_info
                    .setLayout(sample_lights_.pipeline_layout_)
                    .setStage(shader_stage_create_info);

                sample_lights_.pipeline_ = device_.createComputePipeline(
                    pipeline_cache_,
                    pipeline_create_info);

                vk::DescriptorBufferInfo rtBufferInfos0[] =
                {
                    vk::DescriptorBufferInfo(rt_buffers_.rays[0].buffer, 0u, rt_buffers_.rays[0].size),
                    vk::DescriptorBufferInfo(rt_buffers_.ray_count[0].buffer, 0u, rt_buffers_.ray_count[0].size),
                };

                vk::DescriptorBufferInfo rtBufferInfos1[] =
                {
                    vk::DescriptorBufferInfo(rt_buffers_.rays[1].buffer, 0u, rt_buffers_.rays[1].size),
                    vk::DescriptorBufferInfo(rt_buffers_.ray_count[1].buffer, 0u, rt_buffers_.ray_count[1].size),
                };

                // Write buffer descriptors
                std::vector<vk::WriteDescriptorSet> descWrites(4);
                descWrites[0]
                    .setDescriptorCount(sizeof(rtBufferInfos0) / sizeof(VkDescriptorBufferInfo))
                    .setDescriptorType(vk::DescriptorType::eStorageBuffer)
                    .setDstSet(sample_lights_.descsets_rays_[0])
                    .setDstBinding(5)
                    .setPBufferInfo(rtBufferInfos0);
                descWrites[1]
                    .setDescriptorCount(sizeof(rtBufferInfos1) / sizeof(VkDescriptorBufferInfo))
                    .setDescriptorType(vk::DescriptorType::eStorageBuffer)
                    .setDstSet(sample_lights_.descsets_rays_[1])
                    .setDstBinding(5)
                    .setPBufferInfo(rtBufferInfos1);
                descWrites[2]
                    .setDescriptorCount(sizeof(rtBufferInfos0) / sizeof(VkDescriptorBufferInfo))
                    .setDescriptorType(vk::DescriptorType::eStorageBuffer)
                    .setDstSet(sample_lights_.descsets_rays_[1])
                    .setDstBinding(20)
                    .setPBufferInfo(rtBufferInfos0);
                descWrites[3]
                    .setDescriptorCount(sizeof(rtBufferInfos1) / sizeof(VkDescriptorBufferInfo))
                    .setDescriptorType(vk::DescriptorType::eStorageBuffer)
                    .setDstSet(sample_lights_.descsets_rays_[0])
                    .setDstBinding(20)
                    .setPBufferInfo(rtBufferInfos1);

                device_.updateDescriptorSets(descWrites, nullptr);
            }

            // Create accumulate pipeline
            {
                vk::DescriptorSetLayoutBinding layout_binding[] =
                {
                    vk::DescriptorSetLayoutBinding(0u, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eCompute),
                    vk::DescriptorSetLayoutBinding(1u, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eCompute),
                    vk::DescriptorSetLayoutBinding(2u, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eCompute),
                    vk::DescriptorSetLayoutBinding(3u, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eCompute),
                    vk::DescriptorSetLayoutBinding(4u, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eCompute),
                    vk::DescriptorSetLayoutBinding(5u, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eCompute)
                };

                // Create descriptor set layout
                vk::DescriptorSetLayoutCreateInfo desc_layout_create_info;
                desc_layout_create_info
                    .setBindingCount(sizeof(layout_binding) / sizeof(vk::DescriptorSetLayoutBinding))
                    .setPBindings(layout_binding);
                accumulate_contribution_.desc_layout_ = device_
                    .createDescriptorSetLayout(desc_layout_create_info);

                // Allocate descriptors
                vk::DescriptorSetAllocateInfo desc_alloc_info;
                desc_alloc_info.setDescriptorPool(descpool_);
                desc_alloc_info.setDescriptorSetCount(1);
                desc_alloc_info.setPSetLayouts(&accumulate_contribution_.desc_layout_);
                accumulate_contribution_.descsets_ = device_.allocateDescriptorSets(desc_alloc_info);

                // Create pipeline layout
                vk::PipelineLayoutCreateInfo pipeline_layout_create_info;
                pipeline_layout_create_info
                    .setSetLayoutCount(1)
                    .setPSetLayouts(&accumulate_contribution_.desc_layout_);

                accumulate_contribution_.pipeline_layout_ = device_
                    .createPipelineLayout(pipeline_layout_create_info);

                // Load intersection shader module
                std::string path = "../Baikal/Kernels/Vk/shaders/gi_accumulate_samples.comp.spv";
                accumulate_contribution_.shader_ = RadeonRays::LoadShaderModule(device_, path);

                // Create pipeline 
                vk::PipelineShaderStageCreateInfo shader_stage_create_info;
                shader_stage_create_info
                    .setStage(vk::ShaderStageFlagBits::eCompute)
                    .setModule(accumulate_contribution_.shader_)
                    .setPName("main");

                vk::ComputePipelineCreateInfo pipeline_create_info;
                pipeline_create_info
                    .setLayout(accumulate_contribution_.pipeline_layout_)
                    .setStage(shader_stage_create_info);

                accumulate_contribution_.pipeline_ = device_.createComputePipeline(
                    pipeline_cache_,
                    pipeline_create_info);
            }
        }

        std::uint32_t  GetCommandBuffersCount() override
        {
            return 1 + m_num_bounces * 4;
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

        ~GlobalIllumination()
        {
            alloc_.deallocate(througput_);
            alloc_.deallocate(light_samples_);

            device_.destroyPipeline(spawn_rays_.pipeline_);
            device_.destroyPipelineLayout(spawn_rays_.pipeline_layout_);
            device_.destroyDescriptorSetLayout(spawn_rays_.desc_layout_);
            device_.destroyDescriptorSetLayout(spawn_rays_.desc_layout_rays_);
            device_.destroyShaderModule(spawn_rays_.shader_);

            device_.destroyPipeline(sample_lights_.pipeline_);
            device_.destroyPipelineLayout(sample_lights_.pipeline_layout_);
            device_.destroyDescriptorSetLayout(sample_lights_.desc_layout_);
            device_.destroyDescriptorSetLayout(sample_lights_.desc_layout_rays_);
            device_.destroyShaderModule(sample_lights_.shader_);
        }

        std::vector<vk::CommandBuffer> Apply(rte_scene const& scene,
            rte_gbuffer const& gbuffer,
            rte_output const& output) override
        {
            CheckAndReallocThroughputBuffer(output.width * output.height * sizeof(float) * 4);

            // Allocate command buffer
            vk::CommandBufferAllocateInfo cmdbuffer_alloc_info;
            cmdbuffer_alloc_info
                .setCommandBufferCount(GetCommandBuffersCount())
                .setCommandPool(cmdpool_)
                .setLevel(vk::CommandBufferLevel::ePrimary);

            auto cmdbuffers = device_.allocateCommandBuffers(cmdbuffer_alloc_info);

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
                    vk::DescriptorBufferInfo(rt_buffers_.rng.buffer, 0u, rt_buffers_.rng.size),
                    vk::DescriptorBufferInfo(througput_.buffer, 0u, througput_.size),
                };

                vk::DescriptorBufferInfo uniformBufferInfos[] =
                {
                    vk::DescriptorBufferInfo(scene.camera)
                };

                vk::DescriptorImageInfo resultImageInfos[] =
                {
                    vk::DescriptorImageInfo(output.sampleCounter)
                };

                // Write buffer descriptors
                std::vector<vk::WriteDescriptorSet> descWrites(4);
                descWrites[0]
                    .setDescriptorCount(sizeof(bufferInfos) / sizeof(vk::DescriptorBufferInfo))
                    .setDescriptorType(vk::DescriptorType::eStorageBuffer)
                    .setDstSet(spawn_rays_.descsets_[0])
                    .setDstBinding(5)
                    .setPBufferInfo(&bufferInfos[0]);
                descWrites[1]
                    .setDescriptorCount(sizeof(uniformBufferInfos) / sizeof(vk::DescriptorBufferInfo))
                    .setDescriptorType(vk::DescriptorType::eUniformBuffer)
                    .setDstSet(spawn_rays_.descsets_[0])
                    .setDstBinding(7)
                    .setPBufferInfo(&uniformBufferInfos[0]);
                // Write image descriptors
                descWrites[2]
                    .setDescriptorCount(sizeof(imageInfos) / sizeof(vk::DescriptorImageInfo))
                    .setDescriptorType(vk::DescriptorType::eCombinedImageSampler)
                    .setDstSet(spawn_rays_.descsets_[0])
                    .setDstBinding(0)
                    .setPImageInfo(&imageInfos[0]);
                descWrites[3]
                    .setDescriptorCount(sizeof(resultImageInfos) / sizeof(vk::DescriptorImageInfo))
                    .setDescriptorType(vk::DescriptorType::eStorageImage)
                    .setDstSet(spawn_rays_.descsets_[0])
                    .setDstBinding(8)
                    .setPImageInfo(&resultImageInfos[0]);
                device_.updateDescriptorSets(descWrites, nullptr);
            }

            {
                // Update descriptors
                vk::DescriptorImageInfo gbufferImageInfo[] =
                {
                    vk::DescriptorImageInfo(gbuffer.depthAndNormal),
                    vk::DescriptorImageInfo(gbuffer.albedo),
                    vk::DescriptorImageInfo(gbuffer.pbrInputs)
                };

                vk::DescriptorImageInfo resultImageInfos[] =
                {
                    vk::DescriptorImageInfo(output.image),
                    vk::DescriptorImageInfo(output.sampleCounter)
                };

                vk::DescriptorBufferInfo rtBufferInfos[] =
                {
                    vk::DescriptorBufferInfo(rt_buffers_.hits.buffer, 0u, rt_buffers_.hits.size)
                };

                vk::DescriptorBufferInfo uniformBufferInfos[] =
                {
                    vk::DescriptorBufferInfo(scene.camera)
                };

                vk::DescriptorBufferInfo sceneBufferInfos[] =
                {
                    vk::DescriptorBufferInfo(scene.shapes),
                    vk::DescriptorBufferInfo(scene.materials),
                    vk::DescriptorBufferInfo(scene.indices),
                    vk::DescriptorBufferInfo(scene.vertices),
                    vk::DescriptorBufferInfo(scene.textureDescs),
                    vk::DescriptorBufferInfo(scene.textures),
                    vk::DescriptorBufferInfo(througput_.buffer, 0u, througput_.size),
                    vk::DescriptorBufferInfo(rt_buffers_.rng.buffer, 0u, rt_buffers_.rng.size),
                    vk::DescriptorBufferInfo(scene.lights),
                    vk::DescriptorBufferInfo(rt_buffers_.shadow_rays.buffer, 0u, rt_buffers_.shadow_rays.size),
                    vk::DescriptorBufferInfo(light_samples_.buffer, 0u, light_samples_.size)
                };

                // Write buffer descriptors
                std::vector<vk::WriteDescriptorSet> descWrites(5);
                descWrites[0]
                    .setDescriptorCount(sizeof(gbufferImageInfo) / sizeof(VkDescriptorImageInfo))
                    .setDescriptorType(vk::DescriptorType::eCombinedImageSampler)
                    .setDstSet(sample_lights_.descsets_[0])
                    .setDstBinding(0)
                    .setPImageInfo(&gbufferImageInfo[0]);
                descWrites[1]
                    .setDescriptorCount(sizeof(resultImageInfos) / sizeof(VkDescriptorImageInfo))
                    .setDescriptorType(vk::DescriptorType::eStorageImage)
                    .setDstSet(sample_lights_.descsets_[0])
                    .setDstBinding(3)
                    .setPImageInfo(&resultImageInfos[0]);
                descWrites[2]
                    .setDescriptorCount(sizeof(rtBufferInfos) / sizeof(VkDescriptorBufferInfo))
                    .setDescriptorType(vk::DescriptorType::eStorageBuffer)
                    .setDstSet(sample_lights_.descsets_[0])
                    .setDstBinding(7)
                    .setPBufferInfo(&rtBufferInfos[0]);
                descWrites[3]
                    .setDescriptorCount(sizeof(uniformBufferInfos) / sizeof(VkDescriptorBufferInfo))
                    .setDescriptorType(vk::DescriptorType::eUniformBuffer)
                    .setDstSet(sample_lights_.descsets_[0])
                    .setDstBinding(8)
                    .setPBufferInfo(&uniformBufferInfos[0]);
                descWrites[4]
                    .setDescriptorCount(sizeof(sceneBufferInfos) / sizeof(VkDescriptorBufferInfo))
                    .setDescriptorType(vk::DescriptorType::eStorageBuffer)
                    .setDstSet(sample_lights_.descsets_[0])
                    .setDstBinding(9)
                    .setPBufferInfo(&sceneBufferInfos[0]);

                device_.updateDescriptorSets(descWrites, nullptr);
            }

            {
                // Update descriptors
                vk::DescriptorImageInfo resultImageInfos[] =
                {
                    vk::DescriptorImageInfo(output.image),
                    vk::DescriptorImageInfo(output.sampleCounter)
                };

                vk::DescriptorBufferInfo bufferInfos[] =
                {
                    vk::DescriptorBufferInfo(rt_buffers_.shadow_rays.buffer, 0u, rt_buffers_.shadow_rays.size),
                    vk::DescriptorBufferInfo(rt_buffers_.ray_count[0].buffer, 0u, rt_buffers_.ray_count[0].size),
                    vk::DescriptorBufferInfo(rt_buffers_.hits.buffer, 0u, rt_buffers_.hits.size),
                    vk::DescriptorBufferInfo(light_samples_.buffer, 0u, light_samples_.size),
                };

                // Write buffer descriptors
                std::vector<vk::WriteDescriptorSet> descWrites(2);
                descWrites[0]
                    .setDescriptorCount(sizeof(resultImageInfos) / sizeof(VkDescriptorImageInfo))
                    .setDescriptorType(vk::DescriptorType::eStorageImage)
                    .setDstSet(accumulate_contribution_.descsets_[0])
                    .setDstBinding(0)
                    .setPImageInfo(&resultImageInfos[0]);
                descWrites[1]
                    .setDescriptorCount(sizeof(bufferInfos) / sizeof(VkDescriptorBufferInfo))
                    .setDescriptorType(vk::DescriptorType::eStorageBuffer)
                    .setDstSet(accumulate_contribution_.descsets_[0])
                    .setDstBinding(2)
                    .setPBufferInfo(&bufferInfos[0]);

                device_.updateDescriptorSets(descWrites, nullptr);
            }

            // Spawn rays pass
            {
                // Begin command buffer
                vk::CommandBufferBeginInfo cmdbuffer_buffer_begin_info;
                cmdbuffers[0].begin(cmdbuffer_buffer_begin_info);

                cmdbuffers[0].fillBuffer(rt_buffers_.ray_count[0].buffer,
                    0u,
                    sizeof(std::uint32_t),
                    0u);
                cmdbuffers[0].fillBuffer(rt_buffers_.ray_count[1].buffer,
                    0u,
                    sizeof(std::uint32_t),
                    0u);

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

                auto desc_set = spawn_rays_.descsets_;
                desc_set.insert(desc_set.end(), spawn_rays_.descsets_rays_.begin(), spawn_rays_.descsets_rays_.end());

                cmdbuffers[0].bindDescriptorSets(vk::PipelineBindPoint::eCompute,
                    spawn_rays_.pipeline_layout_,
                    0,
                    desc_set,
                    nullptr);

                uint32_t groupSize = 16;
                uint32_t groupCountX = (output.width + groupSize - 1) / groupSize;
                uint32_t groupCountY = (output.height + groupSize - 1) / groupSize;

                cmdbuffers[0].dispatch(groupCountX, groupCountY, 1);

                {
                    vk::BufferMemoryBarrier memory_barrier;
                    memory_barrier
                        .setBuffer(rt_buffers_.ray_count[0].buffer)
                        .setOffset(0u)
                        .setSize(sizeof(std::uint32_t))
                        .setSrcAccessMask(vk::AccessFlagBits::eShaderWrite)
                        .setDstAccessMask(vk::AccessFlagBits::eShaderRead);

                    cmdbuffers[0].pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader,
                        vk::PipelineStageFlagBits::eComputeShader,
                        vk::DependencyFlags{},
                        nullptr,
                        memory_barrier,
                        nullptr);
                }

                cmdbuffers[0].end();
            }


            auto cmdbuffer_idx = 1;
            for (auto pass = 0; pass < m_num_bounces; ++pass)
            {
                // Trace rays
                {
                    VkDescriptorBufferInfo rays_desc
                    {
                        rt_buffers_.rays[pass & 1].buffer, 0u, rt_buffers_.rays[pass & 1].size,
                    };

                    VkDescriptorBufferInfo hits_desc
                    {
                        rt_buffers_.hits.buffer, 0u, rt_buffers_.hits.size,
                    };

                    VkDescriptorBufferInfo ray_count_desc
                    {
                        rt_buffers_.ray_count[pass & 1].buffer, 0u, rt_buffers_.ray_count[pass & 1].size,
                    };

                    rrBindBuffers(intersector_, rays_desc, hits_desc, ray_count_desc);
                    VkCommandBuffer cmdBuf = cmdbuffers[cmdbuffer_idx++];
                    rrTraceRays(intersector_, RR_QUERY_INTERSECT,
                        output.width * output.height,
                        &cmdBuf);
                }

                // Sample pass
                {


                    // Begin command buffer
                    vk::CommandBufferBeginInfo cmdbuffer_buffer_begin_info;
                    cmdbuffers[cmdbuffer_idx].begin(cmdbuffer_buffer_begin_info);

                    cmdbuffers[cmdbuffer_idx].bindPipeline(vk::PipelineBindPoint::eCompute, sample_lights_.pipeline_);

                    auto desc_set = sample_lights_.descsets_;
                    desc_set.push_back(sample_lights_.descsets_rays_[pass & 1]);

                    cmdbuffers[cmdbuffer_idx].bindDescriptorSets(vk::PipelineBindPoint::eCompute,
                        sample_lights_.pipeline_layout_,
                        0,
                        desc_set,
                        nullptr);

                    auto N = scene.numLights;
                    cmdbuffers[cmdbuffer_idx].pushConstants(
                        sample_lights_.pipeline_layout_,
                        vk::ShaderStageFlagBits::eCompute,
                        0u,
                        sizeof(std::uint32_t),
                        &N);

                    uint32_t groupSize = 16;
                    uint32_t groupCountX = (output.width + groupSize - 1) / groupSize;
                    uint32_t groupCountY = (output.height + groupSize - 1) / groupSize;

                    cmdbuffers[cmdbuffer_idx].dispatch(groupCountX, groupCountY, 1);
                    cmdbuffers[cmdbuffer_idx++].end();
                }

                // Trace shadow rays
                {
                    VkDescriptorBufferInfo rays_desc
                    {
                        rt_buffers_.shadow_rays.buffer, 0u, rt_buffers_.shadow_rays.size,
                    };

                    VkDescriptorBufferInfo hits_desc
                    {
                        rt_buffers_.hits.buffer, 0u, rt_buffers_.hits.size,
                    };

                    VkDescriptorBufferInfo ray_count_desc
                    {
                        rt_buffers_.ray_count[pass & 1].buffer, 0u, rt_buffers_.ray_count[pass & 1].size,
                    };

                    rrBindBuffers(intersector_, rays_desc, hits_desc, ray_count_desc);
                    VkCommandBuffer cmdBuf = cmdbuffers[cmdbuffer_idx++];
                    rrTraceRays(intersector_, RR_QUERY_INTERSECT,
                        output.width * output.height,
                        &cmdBuf);
                }

                // Accumulate pass
                {


                    // Begin command buffer
                    vk::CommandBufferBeginInfo cmdbuffer_buffer_begin_info;
                    cmdbuffers[cmdbuffer_idx].begin(cmdbuffer_buffer_begin_info);

                    cmdbuffers[cmdbuffer_idx].bindPipeline(vk::PipelineBindPoint::eCompute, accumulate_contribution_.pipeline_);

                    cmdbuffers[cmdbuffer_idx].bindDescriptorSets(vk::PipelineBindPoint::eCompute,
                        accumulate_contribution_.pipeline_layout_,
                        0,
                        accumulate_contribution_.descsets_,
                        nullptr);

                    uint32_t groupSize = 16;
                    uint32_t groupCountX = (output.width + groupSize - 1) / groupSize;
                    uint32_t groupCountY = (output.height + groupSize - 1) / groupSize;

                    cmdbuffers[cmdbuffer_idx].dispatch(groupCountX, groupCountY, 1);
                    cmdbuffers[cmdbuffer_idx++].end();
                }
            }

            return cmdbuffers;
        }

    private:
        void CheckAndReallocThroughputBuffer(std::size_t required_size)
        {
            if (througput_.size < required_size)
            {
                if (througput_.size > 0u)
                {
                    alloc_.deallocate(througput_);
                    alloc_.deallocate(light_samples_);
                }

                througput_ = alloc_.allocate(vk::MemoryPropertyFlagBits::eDeviceLocal,
                    vk::BufferUsageFlagBits::eStorageBuffer,
                    required_size,
                    16u);
                light_samples_ = alloc_.allocate(vk::MemoryPropertyFlagBits::eDeviceLocal,
                    vk::BufferUsageFlagBits::eStorageBuffer,
                    required_size,
                    16u);
            }
        }

        struct Pass
        {
            // Intersector compute pipeline layout
            vk::PipelineLayout pipeline_layout_;
            // Intersector compute pipeline
            vk::Pipeline pipeline_;
            // Descriptors layout
            vk::DescriptorSetLayout desc_layout_;
            vk::DescriptorSetLayout desc_layout_rays_;
            // Shader module
            vk::ShaderModule shader_;
            // Descriptor sets
            std::vector<vk::DescriptorSet> descsets_;
            std::vector<vk::DescriptorSet> descsets_rays_;
        };

        Pass spawn_rays_;
        Pass sample_lights_;
        Pass accumulate_contribution_;

        VkMemoryAlloc::StorageBlock througput_;
        VkMemoryAlloc::StorageBlock light_samples_;

        std::uint32_t m_num_bounces;
    };
}
