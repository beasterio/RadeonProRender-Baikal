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
#include "vk_renderer.h"
#include "SceneGraph/vkscene.h"
#include "Vulkan/gpu_profiler.h"

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace
{
    const std::string GetAssetPath()
    {
#if defined(VK_USE_PLATFORM_ANDROID_KHR)
        return "";
#elif defined(VK_EXAMPLE_DATA_DIR)
        return VK_EXAMPLE_DATA_DIR;
#else
        return "../Baikal/Kernels/VK/";
#endif
    }
}

namespace Baikal
{
    using namespace RadeonRays;
    
    VkRenderer::VkRenderer(vks::VulkanDevice* device, rr_instance* instance)
        : m_vulkan_device(device)
        , m_view_updated(true)
        , m_output_changed(true)
        , m_depth_bias_constant(25.0f)
        , m_depth_bias_slope(25.0f)
        , m_profiler(nullptr)
        , m_rr_instance(instance)
        , m_frame_counter(0)
    {
        PrepareUniformBuffers();
        m_profiler = new GPUProfiler(m_vulkan_device->logicalDevice, m_vulkan_device->physicalDevice, 128);

        CreatePipelineCache();
        SetupDescriptorPool();
        PrepareQuadBuffers();
        SetupDescriptorSetLayout();
        SetupVertexDescriptions();
    }

    VkRenderer::~VkRenderer()
    {
        delete m_profiler;
        m_profiler = nullptr;
    }

    // Renderer overrides
    void VkRenderer::Clear(RadeonRays::float3 const& val,
               Output& output) const
    {
        m_view_updated = true;

    }

    // Render the scene into the output
    void VkRenderer::Render(VkScene const& scene)
    {
        bool shapes_changed = scene.dirty_flags & VkScene::SHAPES;
        bool uniform_buffers_changed = scene.dirty_flags & VkScene::CAMERA ||
                                        scene.dirty_flags & VkScene::LIGHTS;
        bool scene_changed = scene.dirty_flags & VkScene::CURRENT_SCENE ||
                                scene.dirty_flags & VkScene::SCENE_ATTRIBUTES;
        bool textures_changed = scene.dirty_flags & VkScene::TEXTURES;

        m_view_updated |= m_output_changed;
        if (shapes_changed || m_output_changed)
        {
            PrepareRayBuffers();
        }
        if (textures_changed)
        {
            PrepareTextureBuffers(&scene);
        }

        if (shapes_changed || scene_changed || m_output_changed || textures_changed)
        {
            SetupDescriptorSet(&scene);
        }
        if (shapes_changed || scene_changed || m_output_changed)
        {
            PreparePipelines(&scene);
        }

        if (shapes_changed || scene_changed || m_output_changed || textures_changed)
        {
            BuildDrawCommandBuffers();
        }

        if (shapes_changed || m_output_changed || textures_changed)
        {
            BuildDeferredCommandBuffer(&scene);
        }
        if (uniform_buffers_changed)
        {
            UpdateUniformBuffers(&scene);
        }

        scene.dirty_flags = VkScene::NONE;
        m_output_changed = false;
        //if (scene.dirty_flags != VkScene::NONE)
        if (false)
        {
            if (scene.dirty_flags & VkScene::SHAPES)
            {

            }
            if (scene.dirty_flags & VkScene::CURRENT_SCENE ||
                scene.dirty_flags & VkScene::SCENE_ATTRIBUTES)
            {
                BuildDrawCommandBuffers();
            }
            if (scene.dirty_flags & VkScene::CAMERA ||
                scene.dirty_flags & VkScene::LIGHTS)
            {
            }
            if (scene.dirty_flags & VkScene::SHAPE_PROPERTIES)
            {
            }
            if (scene.dirty_flags & VkScene::MATERIALS)
            {
            }
            if (scene.dirty_flags & VkScene::TEXTURES)
            {
            }

            if (scene.dirty_flags & VkScene::VOLUMES)
            {
            }



        }


        Draw();
        m_view_updated = false;

        ++m_frame_counter;
    }

    // Render single tile
    void VkRenderer::RenderTile(VkScene const& scene,
                    RadeonRays::int2 const& tile_origin,
                    RadeonRays::int2 const& tile_size)
    {

    }


    void VkRenderer::SetRandomSeed(std::uint32_t seed)
    {
        
    }

    void VkRenderer::SetOutput(OutputType type, Output* output)
    {
        Renderer<VkScene>::SetOutput(type, output);

        m_output_changed = true;
    }
    void VkRenderer::Draw()
    {
        auto& device = m_vulkan_device->logicalDevice;
        
        VkQueue graphics_queue;
        vkGetDeviceQueue(device, m_vulkan_device->queueFamilyIndices.graphics, 0, &graphics_queue);

        VkQueue compute_queue;
        vkGetDeviceQueue(device, m_vulkan_device->queueFamilyIndices.compute, 0, &compute_queue);

        VkSubmitInfo submit_info = vks::initializers::submitInfo();
        // Gbuffer rendering
        submit_info.pWaitSemaphores = VK_NULL_HANDLE;
        submit_info.waitSemaphoreCount = 0;
        VkPipelineStageFlags stageFlags = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        submit_info.pWaitDstStageMask = &stageFlags;
        submit_info.pSignalSemaphores = &m_offscreen_semaphore;
        submit_info.signalSemaphoreCount = 1;
        //submit_info.signalSemaphoreCount = 0;
        submit_info.pCommandBuffers = &m_command_buffers.deferred;
        submit_info.commandBufferCount = 1;
        VK_CHECK_RESULT(vkQueueSubmit(graphics_queue, 1, &submit_info, VK_NULL_HANDLE));

        // Shadow map pass
        submit_info.pWaitSemaphores = nullptr;
        submit_info.waitSemaphoreCount = 0;
        submit_info.pSignalSemaphores = &m_shadow_semaphore;
        submit_info.signalSemaphoreCount = 1;
        submit_info.commandBufferCount = LIGHT_COUNT;
        submit_info.pCommandBuffers = &m_command_buffers.shadow[0];
        VK_CHECK_RESULT(vkQueueSubmit(graphics_queue, 1, &submit_info, VK_NULL_HANDLE));

        // GI Ray generation
        submit_info.pWaitSemaphores = &m_offscreen_semaphore;
        submit_info.waitSemaphoreCount = 1;
        stageFlags = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        submit_info.pWaitDstStageMask = &stageFlags;
        submit_info.pSignalSemaphores = &m_gi_complete;
        submit_info.signalSemaphoreCount = 1;
        submit_info.commandBufferCount = 1;
        submit_info.pCommandBuffers = &m_command_buffers.gi;
        VK_CHECK_RESULT(vkQueueSubmit(compute_queue, 1, &submit_info, VK_NULL_HANDLE));


#ifdef _DEBUG
        //#define CHECK_RAYS
#ifdef CHECK_RAYS
        // Data transfer
        submit_info.pWaitSemaphores = nullptr;
        submit_info.waitSemaphoreCount = 0;
        submit_info.pSignalSemaphores = nullptr;
        submit_info.signalSemaphoreCount = 0;
        submit_info.commandBufferCount = 1;
        submit_info.pCommandBuffers = &m_command_buffers.dbg_transferRaysToHost;
        VK_CHECK_RESULT(vkQueueSubmit(computeQueue, 1, &submit_info, VK_NULL_HANDLE));
        vkDeviceWaitIdle(device);


        // DEBUG code
        vkDeviceWaitIdle(device);
        {
            void* data = nullptr;
            VK_CHECK_RESULT(vkMapMemory(device, m_buffers.raysStaging.memory, 0, VK_WHOLE_SIZE, 0, &data));
            std::vector<Ray> rays(width * height);
            memcpy(rays.data(), data, width * height * sizeof(Ray));

            for (auto& r : rays)
            {
                assert(!std::isnan(r.direction[0]));
                assert(!std::isnan(r.direction[1]));
                assert(!std::isnan(r.direction[2]));

                assert(!std::isnan(r.origin[0]));
                assert(!std::isnan(r.origin[1]));
                assert(!std::isnan(r.origin[2]));
            }

            vkUnmapMemory(device, m_buffers.raysStaging.memory);
        }
#endif
#endif
        vkDeviceWaitIdle(device);

        // Trace BVH
        {
            submit_info.pCommandBuffers = &m_command_buffers.traceRays;
            submit_info.commandBufferCount = 1;
            submit_info.pWaitSemaphores = &m_gi_complete;
            submit_info.waitSemaphoreCount = 1;
            VkSemaphore signalSemaphores[] = { m_gi_trace_complete[0] };
            submit_info.pSignalSemaphores = signalSemaphores;
            submit_info.signalSemaphoreCount = 1;
            VkPipelineStageFlags stageFlags = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
            submit_info.pWaitDstStageMask = &stageFlags;
            vkQueueSubmit(compute_queue, 1, &submit_info, VK_NULL_HANDLE);
        }

        // Resolve gi rays
        {
            submit_info.pWaitSemaphores = &m_gi_trace_complete[0];
            submit_info.waitSemaphoreCount = 1;
            VkPipelineStageFlags stageFlags = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
            submit_info.pWaitDstStageMask = &stageFlags;
            submit_info.pSignalSemaphores = &m_gi_resolve_complete;
            submit_info.signalSemaphoreCount = 1;
            submit_info.pCommandBuffers = m_view_updated ? &m_command_buffers.giResolveAndClear : &m_command_buffers.giResolve;
            submit_info.commandBufferCount = 1;
            VK_CHECK_RESULT(vkQueueSubmit(compute_queue, 1, &submit_info, VK_NULL_HANDLE));
        }

        // Filter GI rays
        {
            submit_info.pWaitSemaphores = &m_gi_resolve_complete;
            submit_info.waitSemaphoreCount = 1;
            VkPipelineStageFlags stageFlags = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
            submit_info.pWaitDstStageMask = &stageFlags;
            submit_info.pSignalSemaphores = &m_bilateral_filter_complete;
            submit_info.signalSemaphoreCount = 1;
            submit_info.pCommandBuffers = &m_command_buffers.bilateralFilter;
            submit_info.commandBufferCount = 1;
            VK_CHECK_RESULT(vkQueueSubmit(compute_queue, 1, &submit_info, VK_NULL_HANDLE));
        }

        // AO Ray generation
        {
            submit_info.pWaitSemaphores = &m_offscreen_semaphore;
            submit_info.waitSemaphoreCount = 0;
            stageFlags = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            submit_info.pWaitDstStageMask = &stageFlags;
            submit_info.pSignalSemaphores = &m_ao_complete;
            submit_info.signalSemaphoreCount = 1;
            submit_info.commandBufferCount = 1;
            submit_info.pCommandBuffers = &m_command_buffers.ao;
            VK_CHECK_RESULT(vkQueueSubmit(compute_queue, 1, &submit_info, VK_NULL_HANDLE));
        }

        vkDeviceWaitIdle(device);

        // Trace BVH
        {
            submit_info.pCommandBuffers = &m_command_buffers.traceRays;
            submit_info.commandBufferCount = 1;
            VkSemaphore waitSemaphores[] = { m_ao_complete };
            submit_info.pWaitSemaphores = waitSemaphores;
            submit_info.waitSemaphoreCount = 1;
            submit_info.pSignalSemaphores = &m_ao_trace_complete;
            submit_info.signalSemaphoreCount = 1;
            VkPipelineStageFlags stageFlags = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
            submit_info.pWaitDstStageMask = &stageFlags;
            vkQueueSubmit(compute_queue, 1, &submit_info, VK_NULL_HANDLE);
        }

        // Resolve ao rays
        {
            submit_info.pWaitSemaphores = &m_ao_trace_complete;
            submit_info.waitSemaphoreCount = 1;
            VkPipelineStageFlags stageFlags = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
            submit_info.pWaitDstStageMask = &stageFlags;
            submit_info.pSignalSemaphores = &m_ao_resolve_complete;
            submit_info.signalSemaphoreCount = 1;
            submit_info.pCommandBuffers = m_view_updated ? &m_command_buffers.aoResolveAndClear : &m_command_buffers.aoResolve;
            submit_info.commandBufferCount = 1;
            VK_CHECK_RESULT(vkQueueSubmit(compute_queue, 1, &submit_info, VK_NULL_HANDLE));
        }

        // Filter AO rays
        {
            submit_info.pWaitSemaphores = &m_ao_resolve_complete;
            submit_info.waitSemaphoreCount = 1;
            VkPipelineStageFlags stageFlags = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
            submit_info.pWaitDstStageMask = &stageFlags;
            submit_info.pSignalSemaphores = &m_bilateral_filter_ao_complete;
            submit_info.signalSemaphoreCount = 1;
            submit_info.pCommandBuffers = &m_command_buffers.bilateralFilterAO;
            submit_info.commandBufferCount = 1;
            VK_CHECK_RESULT(vkQueueSubmit(compute_queue, 1, &submit_info, VK_NULL_HANDLE));
        }

        {
            VkSemaphore waitSemaphores[] = { m_shadow_semaphore, m_bilateral_filter_complete, m_bilateral_filter_ao_complete };
            //VkSemaphore waitSemaphores[] = { m_shadow_semaphore, m_offscreen_semaphore};
            // Scene rendering
            submit_info.pWaitSemaphores = waitSemaphores;
            submit_info.waitSemaphoreCount = sizeof(waitSemaphores) / sizeof(VkSemaphore);
            //submit_info.pWaitSemaphores = waitSemaphores;
            //submit_info.waitSemaphoreCount = 2;
            
            
            VkPipelineStageFlags stageFlags[] = {
                VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT
            };

            submit_info.pWaitDstStageMask = stageFlags;
            submit_info.pSignalSemaphores = VK_NULL_HANDLE;
            submit_info.signalSemaphoreCount = 0;
            submit_info.pCommandBuffers = &m_command_buffers.drawCmdBuffers;
            submit_info.commandBufferCount = 1;
            VK_CHECK_RESULT(vkQueueSubmit(graphics_queue, 1, &submit_info, VK_NULL_HANDLE));
        }

        VK_CHECK_RESULT(vkQueueWaitIdle(graphics_queue));
    }

    void VkRenderer::BuildDeferredCommandBuffer(VkScene const* scene)
    {
        auto& device = m_vulkan_device->logicalDevice;
        auto vk_output = dynamic_cast<VkOutput*>(GetOutput(OutputType::kColor));
        auto const& framebuffers = vk_output->framebuffers;
        int width = vk_output->width();
        int height = vk_output->height();
        VkQueue graphics_queue;
        vkGetDeviceQueue(device, m_vulkan_device->queueFamilyIndices.graphics, 0, &graphics_queue);
        VkQueue compute_queue;
        vkGetDeviceQueue(device, m_vulkan_device->queueFamilyIndices.compute, 0, &compute_queue);

        uint32_t query_id = 0;

        const uint32_t startGenerateAOQuery = query_id++;
        const uint32_t endGenerateAOQuery = query_id++;
        const uint32_t startGenerateGIRays = query_id++;
        const uint32_t endGenerateGIRays = query_id++;
        const uint32_t startAOResolve = query_id++;
        const uint32_t endAOResolve = query_id++;
        const uint32_t startGIResolve = query_id++;
        const uint32_t endGIResolve = query_id++;
        const uint32_t startAOBilateralFilter = query_id++;
        const uint32_t endAOBilateralFilter = query_id++;
        const uint32_t startGIBilateralFilter = query_id++;
        const uint32_t endGIBilateralFilter = query_id++;

        if (m_command_buffers.deferred == VK_NULL_HANDLE)
        {
            m_command_buffers.deferred = m_vulkan_device->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, false);
        }

        // Create a semaphore used to synchronize offscreen rendering and usage
        VkSemaphoreCreateInfo semaphoreCreateInfo = vks::initializers::semaphoreCreateInfo();
        VK_CHECK_RESULT(vkCreateSemaphore(device, &semaphoreCreateInfo, nullptr, &m_offscreen_semaphore));

        VkCommandBufferBeginInfo cmdBufInfo = vks::initializers::commandBufferBeginInfo();

        VkRenderPassBeginInfo renderPassBeginInfo = vks::initializers::renderPassBeginInfo();
        std::array<VkClearValue, 4> clearValues = {};
        VkViewport viewport;
        VkRect2D scissor;

        // Deferred calculations
        // -------------------------------------------------------------------------------------------------------

        vkBeginCommandBuffer(m_command_buffers.deferred, &cmdBufInfo);

        // Clear values for all attachments written in the fragment sahder
        clearValues[0].color = { { 0.0f, 0.0f, 0.0f, 0.0f } };
        clearValues[1].color = { { 0.0f, 0.0f, 0.0f, 0.0f } };
        clearValues[2].color = { { 0.0f, 0.0f, 0.0f, 0.0f } };
        clearValues[3].depthStencil = { 1.0f, 0 };

        renderPassBeginInfo.renderPass = framebuffers.deferred->renderPass;
        renderPassBeginInfo.framebuffer = framebuffers.deferred->framebuffer;
        renderPassBeginInfo.renderArea.extent.width = framebuffers.deferred->width;
        renderPassBeginInfo.renderArea.extent.height = framebuffers.deferred->height;
        renderPassBeginInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
        renderPassBeginInfo.pClearValues = clearValues.data();

        vkCmdBeginRenderPass(m_command_buffers.deferred, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

        viewport = vks::initializers::viewport((float)framebuffers.deferred->width, (float)framebuffers.deferred->height, 0.0f, 1.0f);
        vkCmdSetViewport(m_command_buffers.deferred, 0, 1, &viewport);

        scissor = vks::initializers::rect2D(framebuffers.deferred->width, framebuffers.deferred->height, 0, 0);
        vkCmdSetScissor(m_command_buffers.deferred, 0, 1, &scissor);

        vkCmdBindPipeline(m_command_buffers.deferred, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelines.offscreen);
        RenderScene(scene, m_command_buffers.deferred, false);
        vkCmdEndRenderPass(m_command_buffers.deferred);

        VK_CHECK_RESULT(vkEndCommandBuffer(m_command_buffers.deferred));

        // Build shadow pass command buffers
        VK_CHECK_RESULT(vkCreateSemaphore(device, &semaphoreCreateInfo, nullptr, &m_shadow_semaphore));

        for (int i = 0; i < LIGHT_COUNT; i++)
        {
            m_command_buffers.shadow[i] = m_vulkan_device->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, false);

            VkCommandBufferBeginInfo cmdBufInfo = vks::initializers::commandBufferBeginInfo();

            VkClearValue clearValues[1];
            clearValues[0].depthStencil = { 1.0f, 0 };

            VkRenderPassBeginInfo renderPassBeginInfo = vks::initializers::renderPassBeginInfo();
            renderPassBeginInfo.renderPass = framebuffers.shadow[i]->renderPass;
            renderPassBeginInfo.framebuffer = framebuffers.shadow[i]->framebuffer;
            renderPassBeginInfo.renderArea.offset.x = 0;
            renderPassBeginInfo.renderArea.offset.y = 0;
            renderPassBeginInfo.renderArea.extent.width = framebuffers.shadow[i]->width;
            renderPassBeginInfo.renderArea.extent.height = framebuffers.shadow[i]->height;
            renderPassBeginInfo.clearValueCount = 2;
            renderPassBeginInfo.pClearValues = clearValues;

            VK_CHECK_RESULT(vkBeginCommandBuffer(m_command_buffers.shadow[i], &cmdBufInfo));

            VkViewport viewport = vks::initializers::viewport((float)framebuffers.shadow[i]->width, (float)framebuffers.shadow[i]->height, 0.0f, 1.0f);
            vkCmdSetViewport(m_command_buffers.shadow[i], 0, 1, &viewport);

            VkRect2D scissor = vks::initializers::rect2D(framebuffers.shadow[i]->width, framebuffers.shadow[i]->height, 0, 0);
            vkCmdSetScissor(m_command_buffers.shadow[i], 0, 1, &scissor);

            // Set depth bias (aka "Polygon offset")
            // Required to avoid shadow mapping artefacts
            vkCmdSetDepthBias(
                m_command_buffers.shadow[i],
                m_depth_bias_constant,
                0.0f,
                m_depth_bias_slope);

            vkCmdBeginRenderPass(m_command_buffers.shadow[i], &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

            VkPipelineLayout pipelineLayout = m_pipeline_layouts.shadow;
            vkCmdBindPipeline(m_command_buffers.shadow[i], VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelines.shadow);
            vkCmdBindDescriptorSets(m_command_buffers.shadow[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &m_descriptor_sets.shadow, 0, NULL);

            vkCmdPushConstants(m_command_buffers.shadow[i], pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(int), &i);

            VkDeviceSize offsets[1] = { 0 };

            // Render from global buffer using index offsets
            vkCmdBindVertexBuffers(m_command_buffers.shadow[i], VERTEX_BUFFER_BIND_ID, 1, &scene->vertex_buffer.buffer, offsets);
            vkCmdBindIndexBuffer(m_command_buffers.shadow[i], scene->index_buffer.buffer, 0, VK_INDEX_TYPE_UINT32);

            for (auto mesh : scene->meshes)
            {
                if (mesh.material->hasAlpha)
                {
                    continue;
                }
                //vkCmdBindDescriptorSets(shadowmapPass.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, scene->pipelineLayout, 0, 1, &mesh.descriptorSet, 0, NULL);
                //vkCmdDrawIndexed(m_command_buffers.shadow[i], mesh.indexCount, 1, 0, mesh.indexBase, 0);
                vkCmdDrawIndexed(m_command_buffers.shadow[i], mesh.indexCount, 1, mesh.indexBase, 0, 0);

            }

            vkCmdEndRenderPass(m_command_buffers.shadow[i]);

            VK_CHECK_RESULT(vkEndCommandBuffer(m_command_buffers.shadow[i]));
        }

        VK_CHECK_RESULT(vkCreateSemaphore(device, &semaphoreCreateInfo, nullptr, &m_ao_trace_complete));
        VK_CHECK_RESULT(vkCreateSemaphore(device, &semaphoreCreateInfo, nullptr, &m_gi_trace_complete[0]));
        VK_CHECK_RESULT(vkCreateSemaphore(device, &semaphoreCreateInfo, nullptr, &m_gi_trace_complete[1]));

        VkFenceCreateInfo fenceCreateInfo = vks::initializers::fenceCreateInfo();
        vkCreateFence(device, &fenceCreateInfo, nullptr, &fences.transferToHost);

        // Build transfer to host cmd buffer
        {
            VK_CHECK_RESULT(vkCreateSemaphore(device, &semaphoreCreateInfo, nullptr, &m_transfer_complete));

            if (m_command_buffers.transferToHost == VK_NULL_HANDLE)
            {
                VkCommandBufferAllocateInfo cmdBufAllocateInfo = vks::initializers::commandBufferAllocateInfo(m_vulkan_device->computeCommandPool, VK_COMMAND_BUFFER_LEVEL_PRIMARY, 1);
                VK_CHECK_RESULT(vkAllocateCommandBuffers(device, &cmdBufAllocateInfo, &m_command_buffers.transferToHost));
            }

            VkCommandBufferBeginInfo cmdBufInfo = vks::initializers::commandBufferBeginInfo();
            vkBeginCommandBuffer(m_command_buffers.transferToHost, &cmdBufInfo);

            VkBufferMemoryBarrier memBarries = vks::initializers::bufferMemoryBarrier();
            memBarries.buffer = m_buffers.hitsLocal.buffer;
            memBarries.offset = 0;
            memBarries.size = m_buffers.hitsLocal.size;
            memBarries.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            memBarries.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

            vkCmdPipelineBarrier(m_command_buffers.transferToHost, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 1, &memBarries, 0, nullptr);

            const uint32_t numRays = width * height;
            const uint32_t hitBufferSize = numRays * sizeof(Hit);

            VkBufferCopy cmdCopy = { 0, 0, hitBufferSize };
            vkCmdCopyBuffer(m_command_buffers.transferToHost, m_buffers.hitsLocal.buffer, m_buffers.hitsStaging.buffer, 1, &cmdCopy);

            VK_CHECK_RESULT(vkEndCommandBuffer(m_command_buffers.transferToHost));
        }

        // Build AO cmd buffer
        {
            VK_CHECK_RESULT(vkCreateSemaphore(device, &semaphoreCreateInfo, nullptr, &m_ao_complete));

            VkCommandBufferAllocateInfo cmdBufAllocateInfo =
                vks::initializers::commandBufferAllocateInfo(
                    m_vulkan_device->computeCommandPool,
                    VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                    1);

            VK_CHECK_RESULT(vkAllocateCommandBuffers(device, &cmdBufAllocateInfo, &m_command_buffers.ao));

            // Build command buffer
            VkCommandBufferBeginInfo cmdBufInfo = vks::initializers::commandBufferBeginInfo();
            VK_CHECK_RESULT(vkBeginCommandBuffer(m_command_buffers.ao, &cmdBufInfo));

            m_profiler->WriteTimestamp(m_command_buffers.ao, startGenerateAOQuery);

            vkCmdBindPipeline(m_command_buffers.ao, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipelines.ao);
            vkCmdBindDescriptorSets(m_command_buffers.ao, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipeline_layouts.generateRays, 0, 1, &m_descriptor_sets.ao, 0, 0);

            uint32_t groupSize = 16;
            uint32_t groupCountX = (width + groupSize - 1) / groupSize;
            uint32_t groupCountY = (height + groupSize - 1) / groupSize;

            vkCmdDispatch(m_command_buffers.ao, groupCountX, groupCountY, 1);

            m_profiler->WriteTimestamp(m_command_buffers.ao, endGenerateAOQuery);

            vkEndCommandBuffer(m_command_buffers.ao);
        }

        // Build GI cmd buffer
        {
            VK_CHECK_RESULT(vkCreateSemaphore(device, &semaphoreCreateInfo, nullptr, &m_gi_complete));

            VkCommandBufferAllocateInfo cmdBufAllocateInfo =
                vks::initializers::commandBufferAllocateInfo(
                    m_vulkan_device->computeCommandPool,
                    VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                    1);

            VK_CHECK_RESULT(vkAllocateCommandBuffers(device, &cmdBufAllocateInfo, &m_command_buffers.gi));

            // Build command buffer
            VkCommandBufferBeginInfo cmdBufInfo = vks::initializers::commandBufferBeginInfo();
            VK_CHECK_RESULT(vkBeginCommandBuffer(m_command_buffers.gi, &cmdBufInfo));

            m_profiler->WriteTimestamp(m_command_buffers.gi, startGenerateGIRays);

            vkCmdBindPipeline(m_command_buffers.gi, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipelines.gi);
            vkCmdBindDescriptorSets(m_command_buffers.gi, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipeline_layouts.generateRays, 0, 1, &m_descriptor_sets.gi, 0, 0);

            uint32_t groupSize = 16;
            uint32_t groupCountX = (width + groupSize - 1) / groupSize;
            uint32_t groupCountY = (height + groupSize - 1) / groupSize;

            vkCmdDispatch(m_command_buffers.gi, groupCountX, groupCountY, 1);

            m_profiler->WriteTimestamp(m_command_buffers.gi, endGenerateGIRays);

            vkEndCommandBuffer(m_command_buffers.gi);
        }

        // Build AO resolve cmd buffer
        {
            static const int clearBuffer = 0;
            static const int preserveBuffer = 1;

            VK_CHECK_RESULT(vkCreateSemaphore(device, &semaphoreCreateInfo, nullptr, &m_ao_resolve_complete));

            VkCommandBufferAllocateInfo cmdBufAllocateInfo =
                vks::initializers::commandBufferAllocateInfo(
                    m_vulkan_device->computeCommandPool,
                    VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                    1);

            VK_CHECK_RESULT(vkAllocateCommandBuffers(device, &cmdBufAllocateInfo, &m_command_buffers.aoResolve));

            // Build command buffer for ao resolve
            VkCommandBufferBeginInfo cmdBufInfo = vks::initializers::commandBufferBeginInfo();
            VK_CHECK_RESULT(vkBeginCommandBuffer(m_command_buffers.aoResolve, &cmdBufInfo));

            m_profiler->WriteTimestamp(m_command_buffers.aoResolve, startAOResolve);

            vkCmdBindPipeline(m_command_buffers.aoResolve, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipelines.aoResolve);
            vkCmdBindDescriptorSets(m_command_buffers.aoResolve, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipeline_layouts.aoResolve, 0, 1, &m_descriptor_sets.aoResolve, 0, 0);

            vkCmdPushConstants(m_command_buffers.aoResolve, m_pipeline_layouts.aoResolve, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(int), &clearBuffer);

            uint32_t groupSize = 16;
            uint32_t groupCountX = (width + groupSize - 1) / groupSize;
            uint32_t groupCountY = (height + groupSize - 1) / groupSize;

            vkCmdDispatch(m_command_buffers.aoResolve, groupCountX, groupCountY, 1);

            m_profiler->WriteTimestamp(m_command_buffers.aoResolve, endAOResolve);

            vkEndCommandBuffer(m_command_buffers.aoResolve);

            VK_CHECK_RESULT(vkAllocateCommandBuffers(device, &cmdBufAllocateInfo, &m_command_buffers.aoResolveAndClear));
            // Build command buffer for ao resolve and clear
            VK_CHECK_RESULT(vkBeginCommandBuffer(m_command_buffers.aoResolveAndClear, &cmdBufInfo));

            vkCmdBindPipeline(m_command_buffers.aoResolveAndClear, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipelines.aoResolve);
            vkCmdBindDescriptorSets(m_command_buffers.aoResolveAndClear, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipeline_layouts.aoResolve, 0, 1, &m_descriptor_sets.aoResolve, 0, 0);

            vkCmdPushConstants(m_command_buffers.aoResolveAndClear, m_pipeline_layouts.aoResolve, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(int), &preserveBuffer);

            vkCmdDispatch(m_command_buffers.aoResolveAndClear, groupCountX, groupCountY, 1);

            vkEndCommandBuffer(m_command_buffers.aoResolveAndClear);
        }

        // Build GI resolve cmd buffer
        {
            static const int clearBuffer = 0;
            static const int preserveBuffer = 1;

            uint32_t groupSize = 16;
            uint32_t groupCountX = (width + groupSize - 1) / groupSize;
            uint32_t groupCountY = (height + groupSize - 1) / groupSize;

            VK_CHECK_RESULT(vkCreateSemaphore(device, &semaphoreCreateInfo, nullptr, &m_gi_resolve_complete));

            VkCommandBufferAllocateInfo cmdBufAllocateInfo =
                vks::initializers::commandBufferAllocateInfo(
                    m_vulkan_device->computeCommandPool,
                    VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                    1);

            VK_CHECK_RESULT(vkAllocateCommandBuffers(device, &cmdBufAllocateInfo, &m_command_buffers.giResolve));

            // Build command buffer for ao resolve
            VkCommandBufferBeginInfo cmdBufInfo = vks::initializers::commandBufferBeginInfo();
            VK_CHECK_RESULT(vkBeginCommandBuffer(m_command_buffers.giResolve, &cmdBufInfo));

            m_profiler->WriteTimestamp(m_command_buffers.giResolve, startGIResolve);

            vkCmdBindPipeline(m_command_buffers.giResolve, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipelines.giResolve);
            vkCmdBindDescriptorSets(m_command_buffers.giResolve, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipeline_layouts.giResolve, 0, 1, &m_descriptor_sets.giResolve, 0, 0);

            vkCmdPushConstants(m_command_buffers.giResolve, m_pipeline_layouts.giResolve, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(int), &clearBuffer);

            vkCmdDispatch(m_command_buffers.giResolve, groupCountX, groupCountY, 1);

            m_profiler->WriteTimestamp(m_command_buffers.giResolve, endGIResolve);

            vkEndCommandBuffer(m_command_buffers.giResolve);

            VK_CHECK_RESULT(vkAllocateCommandBuffers(device, &cmdBufAllocateInfo, &m_command_buffers.giResolveAndClear));
            // Build command buffer for ao resolve and clear
            VK_CHECK_RESULT(vkBeginCommandBuffer(m_command_buffers.giResolveAndClear, &cmdBufInfo));

            vkCmdBindPipeline(m_command_buffers.giResolveAndClear, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipelines.giResolve);
            vkCmdBindDescriptorSets(m_command_buffers.giResolveAndClear, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipeline_layouts.giResolve, 0, 1, &m_descriptor_sets.giResolve, 0, 0);

            vkCmdPushConstants(m_command_buffers.giResolveAndClear, m_pipeline_layouts.giResolve, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(int), &preserveBuffer);

            vkCmdDispatch(m_command_buffers.giResolveAndClear, groupCountX, groupCountY, 1);

            vkEndCommandBuffer(m_command_buffers.giResolveAndClear);
        }

        // Build GI filter cmd buffer
        {
            VK_CHECK_RESULT(vkCreateSemaphore(device, &semaphoreCreateInfo, nullptr, &m_bilateral_filter_complete));

            VkCommandBufferAllocateInfo cmdBufAllocateInfo =
                vks::initializers::commandBufferAllocateInfo(
                    m_vulkan_device->computeCommandPool,
                    VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                    1);

            VK_CHECK_RESULT(vkAllocateCommandBuffers(device, &cmdBufAllocateInfo, &m_command_buffers.bilateralFilter));

            uint32_t groupSize = 16;
            uint32_t groupCountX = (width + groupSize - 1) / groupSize;
            uint32_t groupCountY = (height + groupSize - 1) / groupSize;

            // Build command buffer for ao resolve
            VkCommandBufferBeginInfo cmdBufInfo = vks::initializers::commandBufferBeginInfo();
            VK_CHECK_RESULT(vkBeginCommandBuffer(m_command_buffers.bilateralFilter, &cmdBufInfo));

            m_profiler->WriteTimestamp(m_command_buffers.bilateralFilter, startGIBilateralFilter);

            vkCmdBindPipeline(m_command_buffers.bilateralFilter, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipelines.bilateralFilter);
            vkCmdBindDescriptorSets(m_command_buffers.bilateralFilter, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipeline_layouts.bilateralFilter, 0, 1, &m_descriptor_sets.bilateralFilter, 0, 0);

            vkCmdDispatch(m_command_buffers.bilateralFilter, groupCountX, groupCountY, 1);

            m_profiler->WriteTimestamp(m_command_buffers.bilateralFilter, endGIBilateralFilter);

            vkEndCommandBuffer(m_command_buffers.bilateralFilter);
        }

        // Build AO filter cmd buffer
        {
            VK_CHECK_RESULT(vkCreateSemaphore(device, &semaphoreCreateInfo, nullptr, &m_bilateral_filter_ao_complete));

            VkCommandBufferAllocateInfo cmdBufAllocateInfo =
                vks::initializers::commandBufferAllocateInfo(
                    m_vulkan_device->computeCommandPool,
                    VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                    1);

            VK_CHECK_RESULT(vkAllocateCommandBuffers(device, &cmdBufAllocateInfo, &m_command_buffers.bilateralFilterAO));

            // Build command buffer for ao resolve
            VkCommandBufferBeginInfo cmdBufInfo = vks::initializers::commandBufferBeginInfo();
            VK_CHECK_RESULT(vkBeginCommandBuffer(m_command_buffers.bilateralFilterAO, &cmdBufInfo));

            m_profiler->WriteTimestamp(m_command_buffers.bilateralFilterAO, startAOBilateralFilter);

            vkCmdBindPipeline(m_command_buffers.bilateralFilterAO, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipelines.bilateralFilter);
            vkCmdBindDescriptorSets(m_command_buffers.bilateralFilterAO, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipeline_layouts.bilateralFilter, 0, 1, &m_descriptor_sets.bilateralFilterAO, 0, 0);

            uint32_t groupSize = 16;
            uint32_t groupCountX = (width + groupSize - 1) / groupSize;
            uint32_t groupCountY = (height + groupSize - 1) / groupSize;

            vkCmdDispatch(m_command_buffers.bilateralFilterAO, groupCountX, groupCountY, 1);

            m_profiler->WriteTimestamp(m_command_buffers.bilateralFilterAO, endAOBilateralFilter);

            vkEndCommandBuffer(m_command_buffers.bilateralFilterAO);
        }

        // Build transfer to host DBG cmd buffer
        {
            if (m_command_buffers.dbg_transferRaysToHost == VK_NULL_HANDLE)
            {
                VkCommandBufferAllocateInfo cmdBufAllocateInfo = vks::initializers::commandBufferAllocateInfo(m_vulkan_device->computeCommandPool, VK_COMMAND_BUFFER_LEVEL_PRIMARY, 1);
                VK_CHECK_RESULT(vkAllocateCommandBuffers(device, &cmdBufAllocateInfo, &m_command_buffers.dbg_transferRaysToHost));
            }

            VkCommandBufferBeginInfo cmdBufInfo = vks::initializers::commandBufferBeginInfo();
            vkBeginCommandBuffer(m_command_buffers.dbg_transferRaysToHost, &cmdBufInfo);

            VkBufferMemoryBarrier memBarries = vks::initializers::bufferMemoryBarrier();
            memBarries.buffer = m_buffers.raysLocal.buffer;
            memBarries.offset = 0;
            memBarries.size = m_buffers.raysLocal.size;
            memBarries.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            memBarries.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

            vkCmdPipelineBarrier(m_command_buffers.dbg_transferRaysToHost, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 1, &memBarries, 0, nullptr);

            const uint32_t numRays = width * height;
            const uint32_t rayBufferSize = numRays * sizeof(Ray);

            VkBufferCopy cmdCopy = { 0, 0, rayBufferSize };
            vkCmdCopyBuffer(m_command_buffers.dbg_transferRaysToHost, m_buffers.raysLocal.buffer, m_buffers.raysStaging.buffer, 1, &cmdCopy);

            VK_CHECK_RESULT(vkEndCommandBuffer(m_command_buffers.dbg_transferRaysToHost));
        }

        // Build texture repack cmd buffer
        {
            VkCommandBufferAllocateInfo cmdBufAllocateInfo =
                vks::initializers::commandBufferAllocateInfo(
                    m_vulkan_device->computeCommandPool,
                    VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                    1);

            VK_CHECK_RESULT(vkAllocateCommandBuffers(device, &cmdBufAllocateInfo, &m_command_buffers.textureRepack));
        }

        uint32_t num_rays = width * height;
        rrBindBuffers(*m_rr_instance, m_buffers.raysLocal.buffer, m_buffers.hitsLocal.buffer, num_rays);
        auto status = rrTraceRays(*m_rr_instance, RR_QUERY_INTERSECT, num_rays, &m_command_buffers.traceRays);
    }

    void VkRenderer::RenderScene(VkScene const* scene, VkCommandBuffer cmdBuffer, bool shadow)
    {
        VkDeviceSize offsets[1] = { 0 };

        // Render from global buffer using index offsets
        vkCmdBindVertexBuffers(cmdBuffer, VERTEX_BUFFER_BIND_ID, 1, &scene->vertex_buffer.buffer, offsets);
        vkCmdBindIndexBuffer(cmdBuffer, scene->index_buffer.buffer, 0, VK_INDEX_TYPE_UINT32);
        PushConsts pushConsts;
        for (auto mesh : scene->meshes)
        {
            pushConsts.baseDiffuse = mesh.material->baseDiffuse;
            pushConsts.baseRoughness = mesh.material->baseRoughness;
            pushConsts.baseMetallic = mesh.material->baseMetallic;

            uint32_t offset = pushConsts.meshID[0] * static_cast<uint32_t>(scene->transform_alignment);
            vkCmdBindDescriptorSets(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, scene->pipelineLayout, 0, 1, &mesh.descriptorSet, 1, &offset);
            vkCmdPushConstants(cmdBuffer, scene->pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConsts), &pushConsts);
            //vkCmdDrawIndexed(cmdBuffer, mesh.indexCount, 1, 0, mesh.indexBase, 0);
            vkCmdDrawIndexed(cmdBuffer, mesh.indexCount, 1, mesh.indexBase, 0, 0);

            pushConsts.meshID[0]++;
        }
    }
    void VkRenderer::PreparePipelines(VkScene const* scene)
    {
        auto& device = m_vulkan_device->logicalDevice;
        auto vk_output = dynamic_cast<VkOutput*>(GetOutput(OutputType::kColor));
        auto const& framebuffers = vk_output->framebuffers;

        VkPipelineInputAssemblyStateCreateInfo inputAssemblyState =
            vks::initializers::pipelineInputAssemblyStateCreateInfo(
                VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
                0,
                VK_FALSE);

        VkPipelineRasterizationStateCreateInfo rasterizationState =
            vks::initializers::pipelineRasterizationStateCreateInfo(
                VK_POLYGON_MODE_FILL,
                VK_CULL_MODE_NONE,
                VK_FRONT_FACE_CLOCKWISE,
                0);

        VkPipelineColorBlendAttachmentState blendAttachmentState =
            vks::initializers::pipelineColorBlendAttachmentState(
                0xf,
                VK_FALSE);

        VkPipelineColorBlendStateCreateInfo colorBlendState =
            vks::initializers::pipelineColorBlendStateCreateInfo(
                1,
                &blendAttachmentState);

        VkPipelineDepthStencilStateCreateInfo depthStencilState =
            vks::initializers::pipelineDepthStencilStateCreateInfo(
                VK_TRUE,
                VK_TRUE,
                VK_COMPARE_OP_LESS_OR_EQUAL);

        VkPipelineViewportStateCreateInfo viewportState =
            vks::initializers::pipelineViewportStateCreateInfo(1, 1, 0);

        VkPipelineMultisampleStateCreateInfo multisampleState =
            vks::initializers::pipelineMultisampleStateCreateInfo(
                VK_SAMPLE_COUNT_1_BIT,
                0);

        std::vector<VkDynamicState> dynamicStateEnables = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR
        };
        VkPipelineDynamicStateCreateInfo dynamicState =
            vks::initializers::pipelineDynamicStateCreateInfo(
                dynamicStateEnables.data(),
                static_cast<uint32_t>(dynamicStateEnables.size()),
                0);

        // Final fullscreen pass pipeline
        std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages;

        shaderStages[0] = LoadShader(GetAssetPath() + "shaders/deferred.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
        shaderStages[1] = LoadShader(GetAssetPath() + "shaders/deferred.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);

        VkGraphicsPipelineCreateInfo pipelineCreateInfo =
            vks::initializers::pipelineCreateInfo(
                m_pipeline_layouts.deferred,
                framebuffers.draw_render_pass,
                0);

        pipelineCreateInfo.pVertexInputState = &vertices.inputState;
        pipelineCreateInfo.pInputAssemblyState = &inputAssemblyState;
        pipelineCreateInfo.pRasterizationState = &rasterizationState;
        pipelineCreateInfo.pColorBlendState = &colorBlendState;
        pipelineCreateInfo.pMultisampleState = &multisampleState;
        pipelineCreateInfo.pViewportState = &viewportState;
        pipelineCreateInfo.pDepthStencilState = &depthStencilState;
        pipelineCreateInfo.pDynamicState = &dynamicState;
        pipelineCreateInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
        pipelineCreateInfo.pStages = shaderStages.data();

        VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, m_pipeline_cache, 1, &pipelineCreateInfo, nullptr, &m_pipelines.deferred));

        // Debug display pipeline
        shaderStages[0] = LoadShader(GetAssetPath() + "shaders/debug.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
        shaderStages[1] = LoadShader(GetAssetPath() + "shaders/debug.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
        VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, m_pipeline_cache, 1, &pipelineCreateInfo, nullptr, &m_pipelines.debug));

        // Offscreen pipeline
        shaderStages[0] = LoadShader(GetAssetPath() + "shaders/mrt.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
        shaderStages[1] = LoadShader(GetAssetPath() + "shaders/mrt.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);

        // Separate render pass
        pipelineCreateInfo.renderPass = framebuffers.deferred->renderPass;

        // Separate layout
        pipelineCreateInfo.layout = scene->pipelineLayout;// m_pipeline_layouts.offscreen;

                                                          // Blend attachment states required for all color attachments
                                                          // This is important, as color write mask will otherwise be 0x0 and you
                                                          // won't see anything rendered to the attachment
        std::array<VkPipelineColorBlendAttachmentState, 3> blendAttachmentStates =
        {
            vks::initializers::pipelineColorBlendAttachmentState(0xf, VK_FALSE),
            vks::initializers::pipelineColorBlendAttachmentState(0xf, VK_FALSE),
            vks::initializers::pipelineColorBlendAttachmentState(0xf, VK_FALSE)
        };

        colorBlendState.attachmentCount = static_cast<uint32_t>(blendAttachmentStates.size());
        colorBlendState.pAttachments = blendAttachmentStates.data();

        VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, m_pipeline_cache, 1, &pipelineCreateInfo, nullptr, &m_pipelines.offscreen));

        shaderStages[0] = LoadShader(GetAssetPath() + "shaders/shadow.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
        shaderStages[1] = LoadShader(GetAssetPath() + "shaders/shadow.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
        // No blend attachment states (no color attachments used)
        colorBlendState.attachmentCount = 0;
        // Cull front faces
        depthStencilState.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
        // Enable depth bias
        rasterizationState.depthBiasEnable = VK_TRUE;
        // Add depth bias to dynamic state, so we can change it at runtime
        dynamicStateEnables.push_back(VK_DYNAMIC_STATE_DEPTH_BIAS);
        dynamicState = vks::initializers::pipelineDynamicStateCreateInfo(dynamicStateEnables.data(), static_cast<uint32_t>(dynamicStateEnables.size()), 0);

        pipelineCreateInfo.layout = m_pipeline_layouts.shadow;
        pipelineCreateInfo.renderPass = framebuffers.shadow[0]->renderPass;

        VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, m_pipeline_cache, 1, &pipelineCreateInfo, nullptr, &m_pipelines.shadow));

        // Pipeline for AO
        {
            VkComputePipelineCreateInfo computePipelineCreateInfo = vks::initializers::computePipelineCreateInfo(m_pipeline_layouts.generateRays, 0);
            computePipelineCreateInfo.stage = LoadShader(GetAssetPath() + "shaders/ao.comp.spv", VK_SHADER_STAGE_COMPUTE_BIT);

            VK_CHECK_RESULT(vkCreateComputePipelines(device, m_pipeline_cache, 1, &computePipelineCreateInfo, nullptr, &m_pipelines.ao));
        }

        // Pipeline for GI
        {
            VkComputePipelineCreateInfo computePipelineCreateInfo = vks::initializers::computePipelineCreateInfo(m_pipeline_layouts.generateRays, 0);
            computePipelineCreateInfo.stage = LoadShader(GetAssetPath() + "shaders/gi.comp.spv", VK_SHADER_STAGE_COMPUTE_BIT);

            VK_CHECK_RESULT(vkCreateComputePipelines(device, m_pipeline_cache, 1, &computePipelineCreateInfo, nullptr, &m_pipelines.gi));
        }

        // Pipeline for AO resolve
        {
            VkComputePipelineCreateInfo computePipelineCreateInfo = vks::initializers::computePipelineCreateInfo(m_pipeline_layouts.aoResolve, 0);
            computePipelineCreateInfo.stage = LoadShader(GetAssetPath() + "shaders/ao_resolve.comp.spv", VK_SHADER_STAGE_COMPUTE_BIT);

            VK_CHECK_RESULT(vkCreateComputePipelines(device, m_pipeline_cache, 1, &computePipelineCreateInfo, nullptr, &m_pipelines.aoResolve));
        }

        // Pipeline for GI resolve
        {
            VkComputePipelineCreateInfo computePipelineCreateInfo = vks::initializers::computePipelineCreateInfo(m_pipeline_layouts.giResolve, 0);
            computePipelineCreateInfo.stage = LoadShader(GetAssetPath() + "shaders/gi_resolve.comp.spv", VK_SHADER_STAGE_COMPUTE_BIT);

            VK_CHECK_RESULT(vkCreateComputePipelines(device, m_pipeline_cache, 1, &computePipelineCreateInfo, nullptr, &m_pipelines.giResolve));
        }

        // Pipeline for filter
        {
            VkComputePipelineCreateInfo computePipelineCreateInfo = vks::initializers::computePipelineCreateInfo(m_pipeline_layouts.bilateralFilter, 0);
            computePipelineCreateInfo.stage = LoadShader(GetAssetPath() + "shaders/biltateral_filter.comp.spv", VK_SHADER_STAGE_COMPUTE_BIT);

            VK_CHECK_RESULT(vkCreateComputePipelines(device, m_pipeline_cache, 1, &computePipelineCreateInfo, nullptr, &m_pipelines.bilateralFilter));
        }

        // Pipeline for texture repack
        {
            VkComputePipelineCreateInfo computePipelineCreateInfo = vks::initializers::computePipelineCreateInfo(m_pipeline_layouts.textureRepack, 0);
            computePipelineCreateInfo.stage = LoadShader(GetAssetPath() + "shaders/texture_repack.comp.spv", VK_SHADER_STAGE_COMPUTE_BIT);

            VK_CHECK_RESULT(vkCreateComputePipelines(device, m_pipeline_cache, 1, &computePipelineCreateInfo, nullptr, &m_pipelines.textureRepack));
        }
    }

    VkPipelineShaderStageCreateInfo VkRenderer::LoadShader(std::string fileName, VkShaderStageFlagBits stage)
    {
        auto& device = m_vulkan_device->logicalDevice;

        VkPipelineShaderStageCreateInfo shaderStage = {};
        shaderStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shaderStage.stage = stage;
#if defined(VK_USE_PLATFORM_ANDROID_KHR)
        shaderStage.module = vks::tools::loadShader(androidApp->activity->assetManager, fileName.c_str(), device);
#else
        shaderStage.module = vks::tools::loadShader(fileName.c_str(), device);
#endif
        shaderStage.pName = "main"; // todo : make param
        assert(shaderStage.module != VK_NULL_HANDLE);
        m_shader_modules.push_back(shaderStage.module);
        return shaderStage;
    }

    void VkRenderer::CreatePipelineCache()
    {
        auto& device = m_vulkan_device->logicalDevice;

        VkPipelineCacheCreateInfo pipelineCacheCreateInfo = {};
        pipelineCacheCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
        VK_CHECK_RESULT(vkCreatePipelineCache(device, &pipelineCacheCreateInfo, nullptr, &m_pipeline_cache));
    }

    void VkRenderer::SetupDescriptorSetLayout()
    {
        auto& device = m_vulkan_device->logicalDevice;
        // Deferred shading layout
        {
            std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings =
            {
                // Binding 0: Vertex shader uniform buffer
                vks::initializers::descriptorSetLayoutBinding(
                    VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                    VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_GEOMETRY_BIT,
                    0),
                // Binding 1: Packed normals, roughness, metaliness texture
                vks::initializers::descriptorSetLayoutBinding(
                    VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                    VK_SHADER_STAGE_FRAGMENT_BIT,
                    1),
                // Binding 2: Albedo texture
                vks::initializers::descriptorSetLayoutBinding(
                    VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                    VK_SHADER_STAGE_FRAGMENT_BIT,
                    2),
                // Binding 3: Motion, roughness, metallic texture
                vks::initializers::descriptorSetLayoutBinding(
                    VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                    VK_SHADER_STAGE_FRAGMENT_BIT,
                    3),
                // Binding 4: Fragment shader uniform buffer
                vks::initializers::descriptorSetLayoutBinding(
                    VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                    VK_SHADER_STAGE_FRAGMENT_BIT,
                    4),
                // Binding 5: Shadow map 0
                vks::initializers::descriptorSetLayoutBinding(
                    VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                    VK_SHADER_STAGE_FRAGMENT_BIT,
                    5),
                // Binding 6: Shadow map 1
                vks::initializers::descriptorSetLayoutBinding(
                    VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                    VK_SHADER_STAGE_FRAGMENT_BIT,
                    6),
                // Binding 7: Shadow map 2
                vks::initializers::descriptorSetLayoutBinding(
                    VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                    VK_SHADER_STAGE_FRAGMENT_BIT,
                    7),
                // Binding 8: GI texture
                vks::initializers::descriptorSetLayoutBinding(
                    VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                    VK_SHADER_STAGE_FRAGMENT_BIT,
                    8),
                // Binding 9: AO texture
                vks::initializers::descriptorSetLayoutBinding(
                    VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                    VK_SHADER_STAGE_FRAGMENT_BIT,
                    9)

            };

            VkDescriptorSetLayoutCreateInfo descriptorLayout =
                vks::initializers::descriptorSetLayoutCreateInfo(
                    setLayoutBindings.data(),
                    static_cast<uint32_t>(setLayoutBindings.size()));

            VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorLayout, nullptr, &m_descriptor_set_layouts.deferred));

            VkPipelineLayoutCreateInfo pPipelineLayoutCreateInfo =
                vks::initializers::pipelineLayoutCreateInfo(
                    &m_descriptor_set_layouts.deferred,
                    1);

            VkPushConstantRange pushConstantRange = vks::initializers::pushConstantRange(VK_SHADER_STAGE_FRAGMENT_BIT, 2 * sizeof(int), 0);
            pPipelineLayoutCreateInfo.pushConstantRangeCount = 1;
            pPipelineLayoutCreateInfo.pPushConstantRanges = &pushConstantRange;

            VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pPipelineLayoutCreateInfo, nullptr, &m_pipeline_layouts.deferred));
        }

        // Offscreen rendering
        {

            std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings =
            {
                // Binding 0: Vertex shader uniform buffer
                vks::initializers::descriptorSetLayoutBinding(
                    VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                    VK_SHADER_STAGE_VERTEX_BIT,
                    0),
                // Binding 1: Albedo texture
                vks::initializers::descriptorSetLayoutBinding(
                    VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                    VK_SHADER_STAGE_FRAGMENT_BIT,
                    1),
                // Binding 2: Roughness texture
                vks::initializers::descriptorSetLayoutBinding(
                    VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                    VK_SHADER_STAGE_FRAGMENT_BIT,
                    2),
                // Binding 3: Normals texture
                vks::initializers::descriptorSetLayoutBinding(
                    VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                    VK_SHADER_STAGE_FRAGMENT_BIT,
                    3),
                // Binding 4: Metaliness texture
                vks::initializers::descriptorSetLayoutBinding(
                    VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                    VK_SHADER_STAGE_FRAGMENT_BIT,
                    4)
            };

            VkDescriptorSetLayoutCreateInfo descriptorLayout =
                vks::initializers::descriptorSetLayoutCreateInfo(
                    setLayoutBindings.data(),
                    static_cast<uint32_t>(setLayoutBindings.size()));

            VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorLayout, nullptr, &m_descriptor_set_layouts.offscreen));

            VkPipelineLayoutCreateInfo pPipelineLayoutCreateInfo =
                vks::initializers::pipelineLayoutCreateInfo(
                    &m_descriptor_set_layouts.offscreen,
                    1);

            // Offscreen (scene) rendering pipeline layout
            VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pPipelineLayoutCreateInfo, nullptr, &m_pipeline_layouts.offscreen));
        }

        // Shadow pipeline layout
        {
            // Shadow rendering
            std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings =
            {
                // Binding 0: Vertex shader uniform buffer
                vks::initializers::descriptorSetLayoutBinding(
                    VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                    VK_SHADER_STAGE_VERTEX_BIT,
                    0)
            };

            VkDescriptorSetLayoutCreateInfo descriptorLayout =
                vks::initializers::descriptorSetLayoutCreateInfo(
                    setLayoutBindings.data(),
                    static_cast<uint32_t>(setLayoutBindings.size()));

            VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorLayout, nullptr, &m_descriptor_set_layouts.shadow));

            VkPipelineLayoutCreateInfo pPipelineLayoutCreateInfo = vks::initializers::pipelineLayoutCreateInfo(&m_descriptor_set_layouts.shadow, 1);
            VkPushConstantRange pushConstantRange = vks::initializers::pushConstantRange(VK_SHADER_STAGE_VERTEX_BIT, sizeof(int), 0);
            pPipelineLayoutCreateInfo.pushConstantRangeCount = 1;
            pPipelineLayoutCreateInfo.pPushConstantRanges = &pushConstantRange;

            // Shadow rendering pipeline layout
            VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pPipelineLayoutCreateInfo, nullptr, &m_pipeline_layouts.shadow));
        }

        // Generate rays layout
        {
            std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings = {
                // Binding 0 : Sampled image (depth)
                vks::initializers::descriptorSetLayoutBinding(
                    VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                    VK_SHADER_STAGE_COMPUTE_BIT,
                    0),
                // Binding 2 : Uniform buffer
                vks::initializers::descriptorSetLayoutBinding(
                    VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                    VK_SHADER_STAGE_COMPUTE_BIT,
                    2),
                // Binding 3 : Ray buffer
                vks::initializers::descriptorSetLayoutBinding(
                    VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                    VK_SHADER_STAGE_COMPUTE_BIT,
                    3),
                // Binding 4 : RNG buffer
                vks::initializers::descriptorSetLayoutBinding(
                    VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                    VK_SHADER_STAGE_COMPUTE_BIT,
                    4)
            };

            VkDescriptorSetLayoutCreateInfo descriptorLayout =
                vks::initializers::descriptorSetLayoutCreateInfo(
                    setLayoutBindings.data(),
                    (uint32_t)setLayoutBindings.size());

            VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorLayout, nullptr, &m_descriptor_set_layouts.generateRays));

            VkPipelineLayoutCreateInfo pPipelineLayoutCreateInfo =
                vks::initializers::pipelineLayoutCreateInfo(
                    &m_descriptor_set_layouts.generateRays,
                    1);

            VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pPipelineLayoutCreateInfo, nullptr, &m_pipeline_layouts.generateRays));

            // AO resolve layout
            {
                std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings = {
                    // Binding 0 : AO storage image
                    vks::initializers::descriptorSetLayoutBinding(
                        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                        VK_SHADER_STAGE_COMPUTE_BIT,
                        0),
                    // Binding 1 : Uniform buffer
                    vks::initializers::descriptorSetLayoutBinding(
                        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                        VK_SHADER_STAGE_COMPUTE_BIT,
                        1),
                    // Binding 2 : Hit buffer
                    vks::initializers::descriptorSetLayoutBinding(
                        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                        VK_SHADER_STAGE_COMPUTE_BIT,
                        2),
                    // Binding 3 : Shapes buffer
                    vks::initializers::descriptorSetLayoutBinding(
                        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                        VK_SHADER_STAGE_COMPUTE_BIT,
                        3),
                    // Binding 4 : Material buffer
                    vks::initializers::descriptorSetLayoutBinding(
                        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                        VK_SHADER_STAGE_COMPUTE_BIT,
                        4),
                    // Binding 5 : Index buffer
                    vks::initializers::descriptorSetLayoutBinding(
                        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                        VK_SHADER_STAGE_COMPUTE_BIT,
                        5),
                    // Binding 6 : Material buffer
                    vks::initializers::descriptorSetLayoutBinding(
                        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                        VK_SHADER_STAGE_COMPUTE_BIT,
                        6),
                    // Binding 7 : Ray buffer
                    vks::initializers::descriptorSetLayoutBinding(
                        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                        VK_SHADER_STAGE_COMPUTE_BIT,
                        7),
                    // Binding 8 : Texture descriptors buffer
                    vks::initializers::descriptorSetLayoutBinding(
                        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                        VK_SHADER_STAGE_COMPUTE_BIT,
                        8),
                    // Binding 9 : Texture data buffer
                    vks::initializers::descriptorSetLayoutBinding(
                        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                        VK_SHADER_STAGE_COMPUTE_BIT,
                        9),
                    // Binding 10: Packed depth and mesh ID
                    vks::initializers::descriptorSetLayoutBinding(
                        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                        VK_SHADER_STAGE_COMPUTE_BIT,
                        10),
                    // Binding 11: Packed normals, roughness, metaliness texture
                    vks::initializers::descriptorSetLayoutBinding(
                        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                        VK_SHADER_STAGE_COMPUTE_BIT,
                        11),
                    // Binding 12: Albedo
                    vks::initializers::descriptorSetLayoutBinding(
                        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                        VK_SHADER_STAGE_COMPUTE_BIT,
                        12),
                    // Binding 5: Shadow map 0
                    vks::initializers::descriptorSetLayoutBinding(
                        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                        VK_SHADER_STAGE_COMPUTE_BIT,
                        13),
                    // Binding 6: Shadow map 1
                    vks::initializers::descriptorSetLayoutBinding(
                        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                        VK_SHADER_STAGE_COMPUTE_BIT,
                        14),
                    // Binding 7: Shadow map 2
                    vks::initializers::descriptorSetLayoutBinding(
                        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                        VK_SHADER_STAGE_COMPUTE_BIT,
                        15),
                };

                VkDescriptorSetLayoutCreateInfo descriptorLayout =
                    vks::initializers::descriptorSetLayoutCreateInfo(
                        setLayoutBindings.data(),
                        (uint32_t)setLayoutBindings.size());

                VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorLayout, nullptr, &m_descriptor_set_layouts.aoResolve));

                VkPipelineLayoutCreateInfo pPipelineLayoutCreateInfo =
                    vks::initializers::pipelineLayoutCreateInfo(
                        &m_descriptor_set_layouts.aoResolve,
                        1);

                VkPushConstantRange pushConstantRange = vks::initializers::pushConstantRange(VK_SHADER_STAGE_COMPUTE_BIT, sizeof(int), 0);
                pPipelineLayoutCreateInfo.pushConstantRangeCount = 1;
                pPipelineLayoutCreateInfo.pPushConstantRanges = &pushConstantRange;

                VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pPipelineLayoutCreateInfo, nullptr, &m_pipeline_layouts.aoResolve));
            }

            // GI resolve layout
            {
                std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings = {
                    // Binding 0 : GI storage image
                    vks::initializers::descriptorSetLayoutBinding(
                        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                        VK_SHADER_STAGE_COMPUTE_BIT,
                        0),
                    // Binding 1 : Uniform buffer
                    vks::initializers::descriptorSetLayoutBinding(
                        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                        VK_SHADER_STAGE_COMPUTE_BIT,
                        1),
                    // Binding 2 : Hit buffer
                    vks::initializers::descriptorSetLayoutBinding(
                        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                        VK_SHADER_STAGE_COMPUTE_BIT,
                        2),
                    // Binding 3 : Shapes buffer
                    vks::initializers::descriptorSetLayoutBinding(
                        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                        VK_SHADER_STAGE_COMPUTE_BIT,
                        3),
                    // Binding 4 : Material buffer
                    vks::initializers::descriptorSetLayoutBinding(
                        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                        VK_SHADER_STAGE_COMPUTE_BIT,
                        4),
                    // Binding 5 : Index buffer
                    vks::initializers::descriptorSetLayoutBinding(
                        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                        VK_SHADER_STAGE_COMPUTE_BIT,
                        5),
                    // Binding 6 : Material buffer
                    vks::initializers::descriptorSetLayoutBinding(
                        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                        VK_SHADER_STAGE_COMPUTE_BIT,
                        6),
                    // Binding 7 : Ray buffer
                    vks::initializers::descriptorSetLayoutBinding(
                        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                        VK_SHADER_STAGE_COMPUTE_BIT,
                        7),
                    // Binding 8 : Texture descriptors buffer
                    vks::initializers::descriptorSetLayoutBinding(
                        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                        VK_SHADER_STAGE_COMPUTE_BIT,
                        8),
                    // Binding 9 : Texture data buffer
                    vks::initializers::descriptorSetLayoutBinding(
                        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                        VK_SHADER_STAGE_COMPUTE_BIT,
                        9),
                    // Binding 10: Packed depth and mesh ID
                    vks::initializers::descriptorSetLayoutBinding(
                        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                        VK_SHADER_STAGE_COMPUTE_BIT,
                        10),
                    // Binding 11: Packed normals, roughness, metaliness texture
                    vks::initializers::descriptorSetLayoutBinding(
                        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                        VK_SHADER_STAGE_COMPUTE_BIT,
                        11),
                    // Binding 12: Albedo
                    vks::initializers::descriptorSetLayoutBinding(
                        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                        VK_SHADER_STAGE_COMPUTE_BIT,
                        12),
                    // Binding 5: Shadow map 0
                    vks::initializers::descriptorSetLayoutBinding(
                        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                        VK_SHADER_STAGE_COMPUTE_BIT,
                        13),
                    // Binding 6: Shadow map 1
                    vks::initializers::descriptorSetLayoutBinding(
                        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                        VK_SHADER_STAGE_COMPUTE_BIT,
                        14),
                    // Binding 7: Shadow map 2
                    vks::initializers::descriptorSetLayoutBinding(
                        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                        VK_SHADER_STAGE_COMPUTE_BIT,
                        15),
                };

                VkDescriptorSetLayoutCreateInfo descriptorLayout =
                    vks::initializers::descriptorSetLayoutCreateInfo(
                        setLayoutBindings.data(),
                        (uint32_t)setLayoutBindings.size());

                VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorLayout, nullptr, &m_descriptor_set_layouts.giResolve));

                VkPipelineLayoutCreateInfo pPipelineLayoutCreateInfo =
                    vks::initializers::pipelineLayoutCreateInfo(
                        &m_descriptor_set_layouts.giResolve,
                        1);

                VkPushConstantRange pushConstantRange = vks::initializers::pushConstantRange(VK_SHADER_STAGE_COMPUTE_BIT, sizeof(int), 0);
                pPipelineLayoutCreateInfo.pushConstantRangeCount = 1;
                pPipelineLayoutCreateInfo.pPushConstantRanges = &pushConstantRange;

                VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pPipelineLayoutCreateInfo, nullptr, &m_pipeline_layouts.giResolve));
            }

            // Filter pipeline layout
            {
                std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings = {
                    // Binding 0 : GI storage image
                    vks::initializers::descriptorSetLayoutBinding(
                        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                        VK_SHADER_STAGE_COMPUTE_BIT,
                        0),
                    // Binding 1 : Depth, normals, mesh id
                    vks::initializers::descriptorSetLayoutBinding(
                        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                        VK_SHADER_STAGE_COMPUTE_BIT,
                        1),
                    // Binding 2 : Uniform buffer
                    vks::initializers::descriptorSetLayoutBinding(
                        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                        VK_SHADER_STAGE_COMPUTE_BIT,
                        2),
                    // Binding 3 : GI filter result
                    vks::initializers::descriptorSetLayoutBinding(
                        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                        VK_SHADER_STAGE_COMPUTE_BIT,
                        3)
                };

                VkDescriptorSetLayoutCreateInfo descriptorLayout =
                    vks::initializers::descriptorSetLayoutCreateInfo(
                        setLayoutBindings.data(),
                        (uint32_t)setLayoutBindings.size());

                VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorLayout, nullptr, &m_descriptor_set_layouts.bilateralFilter));

                VkPipelineLayoutCreateInfo pPipelineLayoutCreateInfo =
                    vks::initializers::pipelineLayoutCreateInfo(
                        &m_descriptor_set_layouts.bilateralFilter,
                        1);

                VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pPipelineLayoutCreateInfo, nullptr, &m_pipeline_layouts.bilateralFilter));
            }

            // Texture repack pipeline layout
            {
                std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings = {
                    // Binding 0 : Texture to remap
                    vks::initializers::descriptorSetLayoutBinding(
                        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                        VK_SHADER_STAGE_COMPUTE_BIT,
                        0),
                    // Binding 1 :Texture descriptors buffer
                    vks::initializers::descriptorSetLayoutBinding(
                        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                        VK_SHADER_STAGE_COMPUTE_BIT,
                        1),
                    // Binding 2 : buffer 1D
                    vks::initializers::descriptorSetLayoutBinding(
                        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                        VK_SHADER_STAGE_COMPUTE_BIT,
                        2),
                };

                VkDescriptorSetLayoutCreateInfo descriptorLayout =
                    vks::initializers::descriptorSetLayoutCreateInfo(
                        setLayoutBindings.data(),
                        (uint32_t)setLayoutBindings.size());

                VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorLayout, nullptr, &m_descriptor_set_layouts.textureRepack));

                VkPipelineLayoutCreateInfo pPipelineLayoutCreateInfo =
                    vks::initializers::pipelineLayoutCreateInfo(
                        &m_descriptor_set_layouts.textureRepack,
                        1);

                VkPushConstantRange pushConstantRange = vks::initializers::pushConstantRange(VK_SHADER_STAGE_COMPUTE_BIT, 2 * sizeof(int), 0);
                pPipelineLayoutCreateInfo.pushConstantRangeCount = 1;
                pPipelineLayoutCreateInfo.pPushConstantRanges = &pushConstantRange;

                VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pPipelineLayoutCreateInfo, nullptr, &m_pipeline_layouts.textureRepack));
            }
        }
    }
    
    void VkRenderer::SetupDescriptorSet(VkScene const* scene)
    {
        auto& device = m_vulkan_device->logicalDevice;
        auto vk_output = dynamic_cast<VkOutput*>(GetOutput(OutputType::kColor));
        auto const& framebuffers = vk_output->framebuffers;
        int width = framebuffers.deferred->width;
        int height = framebuffers.deferred->height;

        textures.ao.destroy();
        textures.filteredAO.destroy();
        textures.gi.destroy();
        textures.filteredGI.destroy();

        PrepareTextureTarget(&textures.ao, width, height, VK_FORMAT_R32G32B32A32_SFLOAT);
        PrepareTextureTarget(&textures.filteredAO, width, height, VK_FORMAT_R32G32B32A32_SFLOAT);
        PrepareTextureTarget(&textures.gi, width, height, VK_FORMAT_R32G32B32A32_SFLOAT);
        PrepareTextureTarget(&textures.filteredGI, width, height, VK_FORMAT_R32G32B32A32_SFLOAT);

        std::vector<VkWriteDescriptorSet> writeDescriptorSets;

        // Deferred descriptor set
        {
            //recreate
            if (m_descriptor_sets.deferred)
            {
                vkFreeDescriptorSets(device, descriptorPool, 1, &m_descriptor_sets.deferred);
            }
            VkDescriptorSetAllocateInfo allocInfo =
                vks::initializers::descriptorSetAllocateInfo(
                    descriptorPool,
                    &m_descriptor_set_layouts.deferred,
                    1);

            VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &m_descriptor_sets.deferred));

            // Image descriptors for the offscreen color attachments
            VkDescriptorImageInfo texDescriptorData0 =
                vks::initializers::descriptorImageInfo(
                    framebuffers.deferred->sampler,
                    framebuffers.deferred->attachments[0].view,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

            VkDescriptorImageInfo texDescriptorAlbedo =
                vks::initializers::descriptorImageInfo(
                    framebuffers.deferred->sampler,
                    framebuffers.deferred->attachments[1].view,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

            VkDescriptorImageInfo texDescriptorData1 =
                vks::initializers::descriptorImageInfo(
                    framebuffers.deferred->sampler,
                    framebuffers.deferred->attachments[2].view,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

            VkDescriptorImageInfo texDescriptorShadowMap0 =
                vks::initializers::descriptorImageInfo(
                    framebuffers.shadow[0]->sampler,
                    framebuffers.shadow[0]->attachments[0].view,
                    VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);

            VkDescriptorImageInfo texDescriptorShadowMap1 =
                vks::initializers::descriptorImageInfo(
                    framebuffers.shadow[1]->sampler,
                    framebuffers.shadow[1]->attachments[0].view,
                    VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);

            VkDescriptorImageInfo texDescriptorShadowMap2 =
                vks::initializers::descriptorImageInfo(
                    framebuffers.shadow[2]->sampler,
                    framebuffers.shadow[2]->attachments[0].view,
                    VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);


            writeDescriptorSets = {
                // Binding 0: Vertex shader uniform buffer
                vks::initializers::writeDescriptorSet(
                    m_descriptor_sets.deferred,
                    VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                    0,
                    &m_uniform_buffers.vsFullScreen.descriptor),
                // Binding 1: View space normals, depth, mesh id
                vks::initializers::writeDescriptorSet(
                    m_descriptor_sets.deferred,
                    VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                    1,
                    &texDescriptorData0),
                // Binding 2: Albedo texture
                vks::initializers::writeDescriptorSet(
                    m_descriptor_sets.deferred,
                    VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                    2,
                    &texDescriptorAlbedo),
                // Binding 3: Motion vectors, roughness, metallic
                vks::initializers::writeDescriptorSet(
                    m_descriptor_sets.deferred,
                    VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                    3,
                    &texDescriptorData1),
                // Binding 4: Fragment shader uniform buffer
                vks::initializers::writeDescriptorSet(
                    m_descriptor_sets.deferred,
                    VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                    4,
                    &m_uniform_buffers.fsLights.descriptor),
                // Binding 5: Shadow map
                vks::initializers::writeDescriptorSet(
                    m_descriptor_sets.deferred,
                    VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                    5,
                    &texDescriptorShadowMap0),
                // Binding 6: Shadow map
                vks::initializers::writeDescriptorSet(
                    m_descriptor_sets.deferred,
                    VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                    6,
                    &texDescriptorShadowMap1),
                // Binding 7: Shadow map
                vks::initializers::writeDescriptorSet(
                    m_descriptor_sets.deferred,
                    VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                    7,
                    &texDescriptorShadowMap2),
                // Binding 8: GI texture
                vks::initializers::writeDescriptorSet(
                    m_descriptor_sets.deferred,
                    VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                    8,
                    &textures.filteredGI.descriptor),
                // Binding 9: AO texture
                vks::initializers::writeDescriptorSet(
                    m_descriptor_sets.deferred,
                    VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                    9,
                    &textures.filteredAO.descriptor)
            };

            vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, NULL);
        };

        // Shadow map descriptor set
        {

            if (m_descriptor_sets.shadow)
            {
                vkFreeDescriptorSets(device, descriptorPool, 1, &m_descriptor_sets.shadow);
            }
            VkDescriptorSetAllocateInfo allocInfo =
                vks::initializers::descriptorSetAllocateInfo(
                    descriptorPool,
                    &m_descriptor_set_layouts.shadow,
                    1);

            VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &m_descriptor_sets.shadow));

            writeDescriptorSets = {
                vks::initializers::writeDescriptorSet(m_descriptor_sets.shadow, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0, &m_uniform_buffers.fsLights.descriptor)
            };

            vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, NULL);
        }

        // AO descriptor set
        {
            if (m_descriptor_sets.ao)
            {
                vkFreeDescriptorSets(device, descriptorPool, 1, &m_descriptor_sets.ao);
            }
            VkDescriptorSetAllocateInfo allocInfo =
                vks::initializers::descriptorSetAllocateInfo(
                    descriptorPool,
                    &m_descriptor_set_layouts.generateRays,
                    1);

            VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &m_descriptor_sets.ao));

            // Image descriptors for the offscreen color attachments
            VkDescriptorImageInfo texDescriptorData0 =
                vks::initializers::descriptorImageInfo(
                    framebuffers.deferred->sampler,
                    framebuffers.deferred->attachments[0].view,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

            VkDescriptorBufferInfo rngBufferDescriptor = {
                scene->raytrace_RNG_buffer.buffer,
                0,
                scene->raytrace_RNG_buffer.size
            };

            writeDescriptorSets =
            {
                // Binding 0 : Sampled image (normals, depth, mesh id)
                vks::initializers::writeDescriptorSet(
                    m_descriptor_sets.ao,
                    VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                    0,
                    &texDescriptorData0),
                // Binding 2 : Uniform buffer
                vks::initializers::writeDescriptorSet(
                    m_descriptor_sets.ao,
                    VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                    2,
                    &m_uniform_buffers.fsLights.descriptor),
                // Binding 3 : Ray buffer
                vks::initializers::writeDescriptorSet(
                    m_descriptor_sets.ao,
                    VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                    3,
                    &m_buffers.raysLocal.descriptor),
                // Binding 4 : RNG buffer
                vks::initializers::writeDescriptorSet(
                    m_descriptor_sets.ao,
                    VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                    4,
                    &rngBufferDescriptor)
            };

            vkUpdateDescriptorSets(device, (uint32_t)writeDescriptorSets.size(), writeDescriptorSets.data(), 0, NULL);
        }

        // GI descriptor set
        {
            if (m_descriptor_sets.gi)
            {
                vkFreeDescriptorSets(device, descriptorPool, 1, &m_descriptor_sets.gi);
            }
            VkDescriptorSetAllocateInfo allocInfo =
                vks::initializers::descriptorSetAllocateInfo(
                    descriptorPool,
                    &m_descriptor_set_layouts.generateRays,
                    1);

            VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &m_descriptor_sets.gi));

            // Image descriptors for the offscreen color attachments
            VkDescriptorImageInfo texDescriptorData0 =
                vks::initializers::descriptorImageInfo(
                    framebuffers.deferred->sampler,
                    framebuffers.deferred->attachments[0].view,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

            VkDescriptorBufferInfo rngBufferDescriptor = {
                scene->raytrace_RNG_buffer.buffer,
                0,
                scene->raytrace_RNG_buffer.size
            };

            writeDescriptorSets =
            {
                // Binding 0 : Sampled image (normals, depth, mesh id)
                vks::initializers::writeDescriptorSet(
                    m_descriptor_sets.gi,
                    VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                    0,
                    &texDescriptorData0),
                // Binding 2 : Uniform buffer
                vks::initializers::writeDescriptorSet(
                    m_descriptor_sets.gi,
                    VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                    2,
                    &m_uniform_buffers.fsLights.descriptor),
                // Binding 3 : Ray buffer
                vks::initializers::writeDescriptorSet(
                    m_descriptor_sets.gi,
                    VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                    3,
                    &m_buffers.raysLocal.descriptor),
                // Binding 4 : RNG buffer
                vks::initializers::writeDescriptorSet(
                    m_descriptor_sets.gi,
                    VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                    4,
                    &rngBufferDescriptor)
            };

            vkUpdateDescriptorSets(device, (uint32_t)writeDescriptorSets.size(), writeDescriptorSets.data(), 0, NULL);
        }

        // AO resolve descriptor set
        {
            if (m_descriptor_sets.aoResolve)
            {
                vkFreeDescriptorSets(device, descriptorPool, 1, &m_descriptor_sets.aoResolve);
            }
            VkDescriptorSetAllocateInfo allocInfo =
                vks::initializers::descriptorSetAllocateInfo(
                    descriptorPool,
                    &m_descriptor_set_layouts.aoResolve,
                    1);

            VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &m_descriptor_sets.aoResolve));

            writeDescriptorSets =
            {
                // Binding 0 : Storage AO
                vks::initializers::writeDescriptorSet(
                    m_descriptor_sets.aoResolve,
                    VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                    0,
                    &textures.ao.descriptor),
                // Binding 1 : Uniform buffer
                vks::initializers::writeDescriptorSet(
                    m_descriptor_sets.aoResolve,
                    VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                    1,
                    &m_uniform_buffers.fsLights.descriptor),
                // Binding 2 : Hit buffer
                vks::initializers::writeDescriptorSet(
                    m_descriptor_sets.aoResolve,
                    VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                    2,
                    &m_buffers.hitsLocal.descriptor)
            };

            vkUpdateDescriptorSets(device, (uint32_t)writeDescriptorSets.size(), writeDescriptorSets.data(), 0, NULL);
        }

        // GI resolve descriptor set
        {
            if (m_descriptor_sets.giResolve)
            {
                vkFreeDescriptorSets(device, descriptorPool, 1, &m_descriptor_sets.giResolve);
            }
            VkDescriptorSetAllocateInfo allocInfo =
                vks::initializers::descriptorSetAllocateInfo(
                    descriptorPool,
                    &m_descriptor_set_layouts.giResolve,
                    1);

            VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &m_descriptor_sets.giResolve));

            VkDescriptorBufferInfo shapeBufferDescriptor = {
                scene->raytrace_shape_buffer.buffer,
                0,
                scene->raytrace_shape_buffer.size
            };

            VkDescriptorBufferInfo materialBufferDescriptor = {
                scene->raytrace_material_buffer.buffer,
                0,
                scene->raytrace_material_buffer.size
            };

            VkDescriptorBufferInfo indexBufferDescriptor = {
                scene->index_buffer.buffer,
                0,
                scene->index_buffer.size
            };

            VkDescriptorBufferInfo vertexBufferDescriptor = {
                scene->raytrace_vertex_buffer.buffer,
                0,
                scene->raytrace_vertex_buffer.size
            };

            VkDescriptorImageInfo texDescriptorData0 =
                vks::initializers::descriptorImageInfo(
                    framebuffers.deferred->sampler,
                    framebuffers.deferred->attachments[0].view,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

            VkDescriptorImageInfo texDescriptorAlbedo =
                vks::initializers::descriptorImageInfo(
                    framebuffers.deferred->sampler,
                    framebuffers.deferred->attachments[1].view,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

            VkDescriptorImageInfo texDescriptorData1 =
                vks::initializers::descriptorImageInfo(
                    framebuffers.deferred->sampler,
                    framebuffers.deferred->attachments[2].view,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

            VkDescriptorImageInfo texDescriptorShadowMap0 =
                vks::initializers::descriptorImageInfo(
                    framebuffers.shadow[0]->sampler,
                    framebuffers.shadow[0]->attachments[0].view,
                    VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);

            VkDescriptorImageInfo texDescriptorShadowMap1 =
                vks::initializers::descriptorImageInfo(
                    framebuffers.shadow[1]->sampler,
                    framebuffers.shadow[1]->attachments[0].view,
                    VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);

            VkDescriptorImageInfo texDescriptorShadowMap2 =
                vks::initializers::descriptorImageInfo(
                    framebuffers.shadow[2]->sampler,
                    framebuffers.shadow[2]->attachments[0].view,
                    VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);


            writeDescriptorSets =
            {
                // Binding 0 : Storage AO
                vks::initializers::writeDescriptorSet(
                    m_descriptor_sets.giResolve,
                    VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                    0,
                    &textures.gi.descriptor),
                // Binding 1 : Uniform buffer
                vks::initializers::writeDescriptorSet(
                    m_descriptor_sets.giResolve,
                    VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                    1,
                    &m_uniform_buffers.fsLights.descriptor),
                // Binding 2 : Hit buffer
                vks::initializers::writeDescriptorSet(
                    m_descriptor_sets.giResolve,
                    VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                    2,
                    &m_buffers.hitsLocal.descriptor),
                // Binding 3 : Shapes buffer
                vks::initializers::writeDescriptorSet(
                    m_descriptor_sets.giResolve,
                    VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                    3,
                    &shapeBufferDescriptor),
                // Binding 4 : Material buffer
                vks::initializers::writeDescriptorSet(
                    m_descriptor_sets.giResolve,
                    VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                    4,
                    &materialBufferDescriptor),
                // Binding 5 : Index buffer
                vks::initializers::writeDescriptorSet(
                    m_descriptor_sets.giResolve,
                    VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                    5,
                    &indexBufferDescriptor),
                // Binding 6 : Vertex buffer
                vks::initializers::writeDescriptorSet(
                    m_descriptor_sets.giResolve,
                    VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                    6,
                    &vertexBufferDescriptor),
                // Binding 7 : Ray buffer
                vks::initializers::writeDescriptorSet(
                    m_descriptor_sets.giResolve,
                    VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                    7,
                    &m_buffers.raysLocal.descriptor),
                // Binding 8 : Texture descriptors buffer
                vks::initializers::writeDescriptorSet(
                    m_descriptor_sets.giResolve,
                    VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                    8,
                    &m_buffers.textures.descriptor),
                // Binding 9 : Texture descriptors buffer
                vks::initializers::writeDescriptorSet(
                    m_descriptor_sets.giResolve,
                    VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                    9,
                    &m_buffers.textureData.descriptor),
                vks::initializers::writeDescriptorSet(
                    m_descriptor_sets.giResolve,
                    VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                    10,
                    &texDescriptorData0),
                vks::initializers::writeDescriptorSet(
                    m_descriptor_sets.giResolve,
                    VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                    11,
                    &texDescriptorData1),
                vks::initializers::writeDescriptorSet(
                    m_descriptor_sets.giResolve,
                    VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                    12,
                    &texDescriptorAlbedo),
                vks::initializers::writeDescriptorSet(
                    m_descriptor_sets.giResolve,
                    VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                    13,
                    &texDescriptorShadowMap0),
                vks::initializers::writeDescriptorSet(
                    m_descriptor_sets.giResolve,
                    VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                    14,
                    &texDescriptorShadowMap1),
                vks::initializers::writeDescriptorSet(
                    m_descriptor_sets.giResolve,
                    VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                    15,
                    &texDescriptorShadowMap2),
            };

            vkUpdateDescriptorSets(device, (uint32_t)writeDescriptorSets.size(), writeDescriptorSets.data(), 0, NULL);
        }

        // Bilateral filter descriptor set
        {
            if (m_descriptor_sets.bilateralFilter)
            {
                vkFreeDescriptorSets(device, descriptorPool, 1, &m_descriptor_sets.bilateralFilter);
            }

            VkDescriptorImageInfo texDescriptorData0 =
                vks::initializers::descriptorImageInfo(
                    framebuffers.deferred->sampler,
                    framebuffers.deferred->attachments[0].view,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

            VkDescriptorSetAllocateInfo allocInfo =
                vks::initializers::descriptorSetAllocateInfo(
                    descriptorPool,
                    &m_descriptor_set_layouts.bilateralFilter,
                    1);

            VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &m_descriptor_sets.bilateralFilter));

            writeDescriptorSets =
            {
                // Binding 0 : GI
                vks::initializers::writeDescriptorSet(
                    m_descriptor_sets.bilateralFilter,
                    VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                    0,
                    &textures.gi.descriptor),
                // Binding 1 : Depth buffer, normals and mesh id
                vks::initializers::writeDescriptorSet(
                    m_descriptor_sets.bilateralFilter,
                    VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                    1,
                    &texDescriptorData0),
                // Binding 2 : Uniform buffer
                vks::initializers::writeDescriptorSet(
                    m_descriptor_sets.bilateralFilter,
                    VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                    2,
                    &m_uniform_buffers.fsLights.descriptor),
                // Binding 3 : Filtered buffer
                vks::initializers::writeDescriptorSet(
                    m_descriptor_sets.bilateralFilter,
                    VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                    3,
                    &textures.filteredGI.descriptor)
            };

            vkUpdateDescriptorSets(device, (uint32_t)writeDescriptorSets.size(), writeDescriptorSets.data(), 0, NULL);
        }

        // Bilateral filter AO descriptor set
        {
            if (m_descriptor_sets.bilateralFilterAO)
            {
                vkFreeDescriptorSets(device, descriptorPool, 1, &m_descriptor_sets.bilateralFilterAO);
            }
            VkDescriptorImageInfo texDescriptorData0 =
                vks::initializers::descriptorImageInfo(
                    framebuffers.deferred->sampler,
                    framebuffers.deferred->attachments[0].view,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

            VkDescriptorSetAllocateInfo allocInfo =
                vks::initializers::descriptorSetAllocateInfo(
                    descriptorPool,
                    &m_descriptor_set_layouts.bilateralFilter,
                    1);

            VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &m_descriptor_sets.bilateralFilterAO));

            writeDescriptorSets =
            {
                // Binding 0 : GI
                vks::initializers::writeDescriptorSet(
                    m_descriptor_sets.bilateralFilterAO,
                    VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                    0,
                    &textures.ao.descriptor),
                // Binding 1 : Depth buffer, normals and mesh id
                vks::initializers::writeDescriptorSet(
                    m_descriptor_sets.bilateralFilterAO,
                    VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                    1,
                    &texDescriptorData0),
                // Binding 2 : Uniform buffer
                vks::initializers::writeDescriptorSet(
                    m_descriptor_sets.bilateralFilterAO,
                    VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                    2,
                    &m_uniform_buffers.fsLights.descriptor),
                // Binding 3 : Filtered buffer
                vks::initializers::writeDescriptorSet(
                    m_descriptor_sets.bilateralFilterAO,
                    VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                    3,
                    &textures.filteredAO.descriptor)
            };

            vkUpdateDescriptorSets(device, (uint32_t)writeDescriptorSets.size(), writeDescriptorSets.data(), 0, NULL);
        }

        // Texture repack filter descriptor set
        {
            if (m_descriptor_sets.textureRepack)
            {
                vkFreeDescriptorSets(device, descriptorPool, 1, &m_descriptor_sets.textureRepack);
            }
            VkDescriptorSetAllocateInfo allocInfo =
                vks::initializers::descriptorSetAllocateInfo(
                    descriptorPool,
                    &m_descriptor_set_layouts.textureRepack,
                    1);

            VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &m_descriptor_sets.textureRepack));

            writeDescriptorSets =
            {
                // Binding 1 :Texture descriptors buffer
                vks::initializers::writeDescriptorSet(
                    m_descriptor_sets.textureRepack,
                    VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                    1,
                    &m_buffers.textures.descriptor),
                // Binding 2 : Texture data
                vks::initializers::writeDescriptorSet(
                    m_descriptor_sets.textureRepack,
                    VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                    2,
                    &m_buffers.textureData.descriptor)
            };

            vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, NULL);
        }

        // Debug view descriptor set
        {
            if (m_descriptor_sets.debug)
            {
                vkFreeDescriptorSets(device, descriptorPool, 1, &m_descriptor_sets.debug);
            }
            VkDescriptorSetAllocateInfo allocInfo =
                vks::initializers::descriptorSetAllocateInfo(
                    descriptorPool,
                    &m_descriptor_set_layouts.deferred,
                    1);

            VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &m_descriptor_sets.debug));

            writeDescriptorSets =
            {
                // Binding 0: Vertex shader uniform buffer
                vks::initializers::writeDescriptorSet(
                    m_descriptor_sets.debug,
                    VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                    0,
                    &m_uniform_buffers.vsFullScreen.descriptor)
            };

            vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, NULL);
        }
    }
    
    void VkRenderer::PrepareUniformBuffers()
    {
        auto vulkanDevice = m_vulkan_device;

        // Fullscreen vertex shader
        VK_CHECK_RESULT(vulkanDevice->createBuffer(
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            &m_uniform_buffers.vsFullScreen,
            sizeof(uboVS)));

        // Deferred vertex shader
        VK_CHECK_RESULT(vulkanDevice->createBuffer(
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            &m_uniform_buffers.vsOffscreen,
            sizeof(uboOffscreenVS)));

        // Deferred fragment shader
        VK_CHECK_RESULT(vulkanDevice->createBuffer(
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            &m_uniform_buffers.fsLights,
            sizeof(uboFragmentLights)));

        // Map persistent
        VK_CHECK_RESULT(m_uniform_buffers.vsFullScreen.map());
        VK_CHECK_RESULT(m_uniform_buffers.vsOffscreen.map());
        VK_CHECK_RESULT(m_uniform_buffers.fsLights.map());
    }
    void VkRenderer::UpdateUniformBuffers(VkScene const* scene)
    {
        // Update
        UpdateUniformBufferDeferredMatrices(scene);
        UpdateUniformBuffersScreen();
        UpdateUniformBufferDeferredLights(scene);
    }


    void VkRenderer::SetupDescriptorPool()
    {
        auto& device = m_vulkan_device->logicalDevice;

        std::vector<VkDescriptorPoolSize> poolSizes =
        {
            vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 32), //todo: separate set layouts
            vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 64),
            vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 32),
            vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 32)
        };

        VkDescriptorPoolCreateInfo descriptorPoolInfo =
            vks::initializers::descriptorPoolCreateInfo(
                static_cast<uint32_t>(poolSizes.size()),
                poolSizes.data(),
                32);
        descriptorPoolInfo.flags |= VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        VK_CHECK_RESULT(vkCreateDescriptorPool(device, &descriptorPoolInfo, nullptr, &descriptorPool));
    }

    void VkRenderer::UpdateUniformBuffersScreen()
    {
        uboVS.projection = glm::ortho(0.0f, 1.0f, 0.0f, 1.0f, -1.0f, 1.0f);
        memcpy(m_uniform_buffers.vsFullScreen.mapped, &uboVS, sizeof(uboVS));
    }

    void VkRenderer::UpdateUniformBufferDeferredMatrices(VkScene const* scene)
    {
        auto vk_output = dynamic_cast<VkOutput*>(GetOutput(OutputType::kColor));
        auto const& framebuffers = vk_output->framebuffers;
        auto& camera = scene->camera;
        int width = framebuffers.deferred->width;
        int height = framebuffers.deferred->height;
        float cameraFOV = 60.0f;

        uboOffscreenVS.projection = camera.matrices.perspective;
        uboOffscreenVS.view = camera.matrices.view;
        uboOffscreenVS.prevViewProjection = camera.matrices.prevViewPerspective;
        uboOffscreenVS.params = glm::vec4(float(width), float(height), 0.0f, glm::tan(glm::radians(cameraFOV / 2.0f)));
        uboOffscreenVS.cameraPosition = glm::vec4(camera.position, m_frame_counter);
        memcpy(m_uniform_buffers.vsOffscreen.mapped, &uboOffscreenVS, sizeof(uboOffscreenVS));
    }

    void VkRenderer::UpdateUniformBufferDeferredLights(VkScene const* scene)
    {
        auto& camera = scene->camera;

        float zNear = 0.2f;
        float zFar = 500.0f;
        float lightFOV = 100.0f;
        
        glm::vec4 cameraPosition = glm::vec4(camera.position, 0.0f) * glm::vec4(-1.0f, -1.0f, -1.0f, 1.0f);

        uboFragmentLights.view = camera.matrices.view;
        uboFragmentLights.params = uboOffscreenVS.params;
        uboFragmentLights.invView = glm::inverse(camera.matrices.view);
        uboFragmentLights.invProj = glm::inverse(camera.matrices.perspective);

        for (uint32_t i = 0; i < LIGHT_COUNT, i < scene->lights.size(); i++)
        {
            // mvp from light's pov (for shadows)
            glm::mat4 shadowProj = glm::perspective(glm::radians(lightFOV), 1.0f, zNear, zFar);
            glm::mat4 shadowView = glm::lookAt(glm::vec3(uboFragmentLights.lights[i].position), glm::vec3(uboFragmentLights.lights[i].target), glm::vec3(0.0f, 1.0f, 0.0f));

            glm::mat4 mvp = shadowProj * shadowView;
            uboFragmentLights.lights[i].viewMatrix = mvp;
            uboFragmentLights.lights[i] = scene->lights[i];
        }

        if (m_view_updated)
            uboFragmentLights.viewPos.w = 0;

        float frameCount = uboFragmentLights.viewPos.w;
        uboFragmentLights.viewPos = glm::vec4(camera.position, frameCount) * glm::vec4(-1.0f, 1.0f, -1.0f, 1.0f);
        uboFragmentLights.viewPos.w += 1.0f;

        memcpy(m_uniform_buffers.fsLights.mapped, &uboFragmentLights, sizeof(uboFragmentLights));
    }

    // Prepare a texture target that is used to store compute shader calculations
    void VkRenderer::PrepareTextureTarget(vks::Texture *tex, uint32_t width, uint32_t height, VkFormat format)
    {
        auto& physicalDevice = m_vulkan_device->physicalDevice;
        auto& device = m_vulkan_device->logicalDevice;
        VkFormatProperties formatProperties;

        // Get device properties for the requested texture format
        vkGetPhysicalDeviceFormatProperties(physicalDevice, format, &formatProperties);
        // Check if requested image format supports image storage operations
        assert(formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT);

        // Prepare blit target texture
        tex->width = width;
        tex->height = height;

        VkImageCreateInfo imageCreateInfo = vks::initializers::imageCreateInfo();
        imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
        imageCreateInfo.format = format;
        imageCreateInfo.extent = { width, height, 1 };
        imageCreateInfo.mipLevels = 1;
        imageCreateInfo.arrayLayers = 1;
        imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        // Image will be sampled in the fragment shader and used as storage target in the compute shader
        imageCreateInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
        imageCreateInfo.flags = 0;
        // Sharing mode exclusive means that ownership of the image does not need to be explicitly transferred between the compute and graphics queue
        imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VkMemoryAllocateInfo memAllocInfo = vks::initializers::memoryAllocateInfo();
        VkMemoryRequirements memReqs;

        VK_CHECK_RESULT(vkCreateImage(device, &imageCreateInfo, nullptr, &tex->image));

        vkGetImageMemoryRequirements(device, tex->image, &memReqs);
        memAllocInfo.allocationSize = memReqs.size;
        memAllocInfo.memoryTypeIndex = m_vulkan_device->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        VK_CHECK_RESULT(vkAllocateMemory(device, &memAllocInfo, nullptr, &tex->deviceMemory));
        VK_CHECK_RESULT(vkBindImageMemory(device, tex->image, tex->deviceMemory, 0));

        VkCommandBuffer layoutCmd = m_vulkan_device->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);

        tex->imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        vks::tools::setImageLayout(
            layoutCmd, tex->image,
            VK_IMAGE_ASPECT_COLOR_BIT,
            VK_IMAGE_LAYOUT_UNDEFINED,
            tex->imageLayout);

        VkQueue queue;
        vkGetDeviceQueue(device, m_vulkan_device->queueFamilyIndices.graphics, 0, &queue);
        m_vulkan_device->flushCommandBuffer(layoutCmd, queue, true);

        // Create sampler
        VkSamplerCreateInfo sampler = vks::initializers::samplerCreateInfo();
        sampler.magFilter = VK_FILTER_LINEAR;
        sampler.minFilter = VK_FILTER_LINEAR;
        sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        sampler.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        sampler.addressModeV = sampler.addressModeU;
        sampler.addressModeW = sampler.addressModeU;
        sampler.mipLodBias = 0.0f;
        sampler.maxAnisotropy = 1.0f;
        sampler.compareOp = VK_COMPARE_OP_NEVER;
        sampler.minLod = 0.0f;
        sampler.maxLod = 0.0f;
        sampler.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
        VK_CHECK_RESULT(vkCreateSampler(device, &sampler, nullptr, &tex->sampler));

        // Create image view
        VkImageViewCreateInfo view = vks::initializers::imageViewCreateInfo();
        view.image = VK_NULL_HANDLE;
        view.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view.format = format;
        view.components = { VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_A };
        view.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        view.image = tex->image;
        VK_CHECK_RESULT(vkCreateImageView(device, &view, nullptr, &tex->view));

        // Initialize a descriptor for later use
        tex->descriptor.imageLayout = tex->imageLayout;
        tex->descriptor.imageView = tex->view;
        tex->descriptor.sampler = tex->sampler;
        tex->device = m_vulkan_device;
    }

    void VkRenderer::PrepareQuadBuffers()
    {
        auto& device = m_vulkan_device->logicalDevice;

        struct Vertex {
            float pos[3];
            float uv[2];
            float col[3];
            float normal[3];
            float tangent[3];
        };

        std::vector<Vertex> vertexBuffer;

        vertexBuffer.push_back({ { 1.0f, 1.0f, 0.0f },{ 1.0f, 1.0f },{ 1.0f, 1.0f, 1.0f },{ 0.0f, 0.0f, 0.0f } });
        vertexBuffer.push_back({ { 0.0f, 1.0f, 0.0f },{ 0.0f, 1.0f },{ 1.0f, 1.0f, 1.0f },{ 0.0f, 0.0f, 0.0f } });
        vertexBuffer.push_back({ { 0.0f, 0.0f, 0.0f },{ 0.0f, 0.0f },{ 1.0f, 1.0f, 1.0f },{ 0.0f, 0.0f, 0.0f } });
        vertexBuffer.push_back({ { 1.0f, 0.0f, 0.0f },{ 1.0f, 0.0f },{ 1.0f, 1.0f, 1.0f },{ 0.0f, 0.0f, 0.0f } });

        VK_CHECK_RESULT(m_vulkan_device->createBuffer(
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            vertexBuffer.size() * sizeof(Vertex),
            &models.quad.vertices.buffer,
            &models.quad.vertices.memory,
            vertexBuffer.data()));

        // Setup indices
        std::vector<uint32_t> indexBuffer = { 0,1,2, 2,3,0 };
        for (uint32_t i = 0; i < 3; ++i)
        {
            uint32_t indices[6] = { 0,1,2, 2,3,0 };
            for (auto index : indices)
            {
                indexBuffer.push_back(i * 4 + index);
            }
        }
        models.quad.indexCount = static_cast<uint32_t>(indexBuffer.size());

        VK_CHECK_RESULT(m_vulkan_device->createBuffer(
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            indexBuffer.size() * sizeof(uint32_t),
            &models.quad.indices.buffer,
            &models.quad.indices.memory,
            indexBuffer.data()));

        models.quad.device = device;
    }

    void VkRenderer::PrepareTextureBuffers(VkScene const* scene)
    {
        m_buffers.textures.device = m_vulkan_device->logicalDevice;
        m_buffers.textureData.device = m_vulkan_device->logicalDevice;
        
        m_buffers.textures.destroy();
        m_buffers.textureData.destroy();

        const uint32_t numTextures = (uint32_t)scene->resources.textures->resources.size();
        const uint32_t texDescBufferSize = sizeof(Texture) * numTextures;

        VK_CHECK_RESULT(m_vulkan_device->createBuffer(
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            &m_buffers.textures,
            texDescBufferSize));

        uint32_t texDataBufferSize = 0;
        const int numMipsToSkip = 0;

        for (size_t i = 0; i < scene->materials.size(); i++)
        {
            vks::Texture& tex = *scene->materials[i].diffuse;

            // All texture must have power of 2 size: check
            assert(tex.width && (!(tex.width & (tex.width - 1))));
            assert(tex.height && (!(tex.height & (tex.height - 1))));

            uint32_t width = tex.width >> numMipsToSkip;
            uint32_t height = tex.height >> numMipsToSkip;

            texDataBufferSize += width * height * sizeof(std::uint32_t);
        }

        VK_CHECK_RESULT(m_vulkan_device->createBuffer(
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            &m_buffers.textureData,
            texDataBufferSize));
    }

    void VkRenderer::PrepareRayBuffers()
    {
        auto& device = m_vulkan_device->logicalDevice;
        auto vk_output = dynamic_cast<VkOutput*>(GetOutput(OutputType::kColor));
        auto const& framebuffers = vk_output->framebuffers;
        int width = vk_output->width();
        int height = vk_output->height();

        const uint32_t numRays = width * height;
        const uint32_t rayBufferSize = numRays * sizeof(Ray);
        const uint32_t hitBufferSize = numRays * sizeof(Hit);

        m_buffers.raysStaging.device = device;
        m_buffers.raysLocal.device = device;
        m_buffers.hitsStaging.device = device;
        m_buffers.hitsLocal.device = device;
        
        m_buffers.raysStaging.destroy();
        m_buffers.raysLocal.destroy();
        m_buffers.hitsStaging.destroy();
        m_buffers.hitsLocal.destroy();
        
        VK_CHECK_RESULT(m_vulkan_device->createBuffer(
            VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
            &m_buffers.raysStaging,
            rayBufferSize));

        VK_CHECK_RESULT(m_vulkan_device->createBuffer(
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            &m_buffers.raysLocal,
            rayBufferSize));

        VK_CHECK_RESULT(m_vulkan_device->createBuffer(
            VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
            &m_buffers.hitsStaging,
            hitBufferSize));

        VK_CHECK_RESULT(m_vulkan_device->createBuffer(
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            &m_buffers.hitsLocal,
            hitBufferSize));
    }
    
    void VkRenderer::SetupVertexDescriptions()
    {
        // Vertex layout for the models
        vks::VertexLayout vertexLayout = vks::VertexLayout({
            vks::VERTEX_COMPONENT_POSITION,
            vks::VERTEX_COMPONENT_UV,
            vks::VERTEX_COMPONENT_COLOR,
            vks::VERTEX_COMPONENT_NORMAL,
            vks::VERTEX_COMPONENT_TANGENT,
        });

        // Binding description
        vertices.bindingDescriptions.resize(1);
        vertices.bindingDescriptions[0] =
            vks::initializers::vertexInputBindingDescription(
                VERTEX_BUFFER_BIND_ID,
                vertexLayout.stride(),
                VK_VERTEX_INPUT_RATE_VERTEX);

        // Attribute descriptions
        vertices.attributeDescriptions.resize(5);
        // Location 0: Position
        vertices.attributeDescriptions[0] =
            vks::initializers::vertexInputAttributeDescription(
                VERTEX_BUFFER_BIND_ID,
                0,
                VK_FORMAT_R32G32B32_SFLOAT,
                0);
        // Location 1: Texture coordinates
        vertices.attributeDescriptions[1] =
            vks::initializers::vertexInputAttributeDescription(
                VERTEX_BUFFER_BIND_ID,
                1,
                VK_FORMAT_R32G32_SFLOAT,
                sizeof(float) * 3);
        // Location 2: Color
        vertices.attributeDescriptions[2] =
            vks::initializers::vertexInputAttributeDescription(
                VERTEX_BUFFER_BIND_ID,
                2,
                VK_FORMAT_R32G32B32_SFLOAT,
                sizeof(float) * 5);
        // Location 3: Normal
        vertices.attributeDescriptions[3] =
            vks::initializers::vertexInputAttributeDescription(
                VERTEX_BUFFER_BIND_ID,
                3,
                VK_FORMAT_R32G32B32_SFLOAT,
                sizeof(float) * 8);
        // Location 4: Tangent
        vertices.attributeDescriptions[4] =
            vks::initializers::vertexInputAttributeDescription(
                VERTEX_BUFFER_BIND_ID,
                4,
                VK_FORMAT_R32G32B32_SFLOAT,
                sizeof(float) * 11);

        vertices.inputState = vks::initializers::pipelineVertexInputStateCreateInfo();
        vertices.inputState.vertexBindingDescriptionCount = static_cast<uint32_t>(vertices.bindingDescriptions.size());
        vertices.inputState.pVertexBindingDescriptions = vertices.bindingDescriptions.data();
        vertices.inputState.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertices.attributeDescriptions.size());
        vertices.inputState.pVertexAttributeDescriptions = vertices.attributeDescriptions.data();
    }

    void VkRenderer::BuildDrawCommandBuffers()
    {
        auto& device = m_vulkan_device->logicalDevice;
        auto vk_output = dynamic_cast<VkOutput*>(GetOutput(OutputType::kColor));
        auto const& framebuffers = vk_output->framebuffers;
        int width = vk_output->width();
        int height = vk_output->height();

        if (!m_command_buffers.drawCmdBuffers)
        {
            VkCommandBufferAllocateInfo cmdBufAllocateInfo =
                vks::initializers::commandBufferAllocateInfo(
                    m_vulkan_device->commandPool,
                    VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                    1);

            VK_CHECK_RESULT(vkAllocateCommandBuffers(device, &cmdBufAllocateInfo, &m_command_buffers.drawCmdBuffers));
        }

        VkCommandBufferBeginInfo cmdBufInfo = vks::initializers::commandBufferBeginInfo();

        VkClearValue clearValues[2];
        clearValues[0].color = { { 0.0f, 0.0f, 0.0f, 0.0f } };
        clearValues[1].depthStencil = { 1.0f, 0 };

        VkRenderPassBeginInfo renderPassBeginInfo = vks::initializers::renderPassBeginInfo();
        renderPassBeginInfo.renderPass = framebuffers.draw_render_pass;
        renderPassBeginInfo.renderArea.offset.x = 0;
        renderPassBeginInfo.renderArea.offset.y = 0;
        renderPassBeginInfo.renderArea.extent.width = width;
        renderPassBeginInfo.renderArea.extent.height = height;
        renderPassBeginInfo.clearValueCount = 2;
        renderPassBeginInfo.pClearValues = clearValues;

        auto& drawCmdBuffers = m_command_buffers.drawCmdBuffers;
        bool aoEnabled = true;
        bool giEnabled = true;

        {
            // Set target frame buffer
            renderPassBeginInfo.framebuffer = framebuffers.draw_fb;

            VK_CHECK_RESULT(vkBeginCommandBuffer(m_command_buffers.drawCmdBuffers, &cmdBufInfo));

            vkCmdBeginRenderPass(drawCmdBuffers, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

            VkViewport viewport = vks::initializers::viewport((float)width, (float)height, 0.0f, 1.0f);
            vkCmdSetViewport(drawCmdBuffers, 0, 1, &viewport);

            VkRect2D scissor = vks::initializers::rect2D(width, height, 0, 0);
            vkCmdSetScissor(drawCmdBuffers, 0, 1, &scissor);

            VkDeviceSize offsets[1] = { 0 };
            vkCmdBindDescriptorSets(drawCmdBuffers, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline_layouts.deferred, 0, 1, &m_descriptor_sets.deferred, 0, NULL);

            int drawMode[] = { giEnabled ? 1 : 0, aoEnabled ? 1 : 0 };
            vkCmdPushConstants(drawCmdBuffers, m_pipeline_layouts.deferred, VK_SHADER_STAGE_FRAGMENT_BIT, 0, 2 * sizeof(int), drawMode);

            // Final composition as full screen quad
            vkCmdBindPipeline(drawCmdBuffers, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelines.deferred);
            vkCmdBindVertexBuffers(drawCmdBuffers, VERTEX_BUFFER_BIND_ID, 1, &models.quad.vertices.buffer, offsets);
            vkCmdBindIndexBuffer(drawCmdBuffers, models.quad.indices.buffer, 0, VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexed(drawCmdBuffers, 6, 1, 0, 0, 0);

            //if (debugViewEnabled)
            //{
            //    vkCmdBindDescriptorSets(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayouts.deferred, 0, 1, &descriptorSets.debug, 0, NULL);
            //    vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.debug);
            //    vkCmdDrawIndexed(drawCmdBuffers[i], 6, numDebugImages, 0, 0, 0);
            //}

            vkCmdEndRenderPass(drawCmdBuffers);

            VK_CHECK_RESULT(vkEndCommandBuffer(drawCmdBuffers));
        }
    }

}
