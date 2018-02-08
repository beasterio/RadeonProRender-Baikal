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
    
    VkRenderer::VkRenderer(vks::VulkanDevice* device, rr_instance instance)
        : m_vulkan_device(device)
        , m_view_updated(true)
        , m_depth_bias_constant(25.0f)
        , m_depth_bias_slope(25.0f)
        , m_profiler(nullptr)
        , m_rr_instance(instance)
    {
        PreparePipelines();
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
        if (!m_command_buffers.deferred)
        {
            BuildDeferredCommandBuffer(&scene);
        }

        Draw();
        m_view_updated = false;
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

    void VkRenderer::Draw()
    {
        auto& device = m_vulkan_device->logicalDevice;
        
        VkQueue graphics_queue;
        vkGetDeviceQueue(device, m_vulkan_device->queueFamilyIndices.graphics, 0, &graphics_queue);

        VkQueue compute_queue;
        vkGetDeviceQueue(device, m_vulkan_device->queueFamilyIndices.compute, 0, &compute_queue);

        VkSubmitInfo submit_info;
        // Gbuffer rendering
        submit_info.pWaitSemaphores = VK_NULL_HANDLE;
        submit_info.waitSemaphoreCount = 0;
        VkPipelineStageFlags stageFlags = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        submit_info.pWaitDstStageMask = &stageFlags;
        submit_info.pSignalSemaphores = &m_offscreen_semaphore;
        submit_info.signalSemaphoreCount = 1;
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
            VK_CHECK_RESULT(vkMapMemory(device, buffers.raysStaging.memory, 0, VK_WHOLE_SIZE, 0, &data));
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

            vkUnmapMemory(device, buffers.raysStaging.memory);
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
            // Scene rendering
            submit_info.pWaitSemaphores = waitSemaphores;
            submit_info.waitSemaphoreCount = 3;
            VkPipelineStageFlags stageFlags[] = {
                VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT
            };

            submit_info.pWaitDstStageMask = stageFlags;
            submit_info.pSignalSemaphores = &m_render_complete;
            submit_info.signalSemaphoreCount = 1;
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

        VkCommandBufferUsageFlags flags = 0;
        VkRenderPassBeginInfo renderPassBeginInfo = vks::initializers::renderPassBeginInfo();
        std::array<VkClearValue, 4> clearValues = {};
        VkViewport viewport;
        VkRect2D scissor;

        // Deferred calculations
        // -------------------------------------------------------------------------------------------------------

        vkBeginCommandBuffer(m_command_buffers.deferred, flags);

        // Clear values for all attachments written in the fragment shader
        clearValues[0].color = { { 0.0f, 0.0f, 0.0f, 0.0f } };
        clearValues[1].color = { { 0.0f, 0.0f, 0.0f, 0.0f } };
        clearValues[2].color = { { 0.0f, 0.0f, 0.0f, 0.0f } };
        clearValues[3].depthStencil = { 1.0f, 0 };

        renderPassBeginInfo.framebuffer = framebuffers.deferred->framebuffer;

        vkCmdBeginRenderPass(m_command_buffers.deferred, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

        viewport = vks::initializers::viewport((float)framebuffers.deferred->width, (float)framebuffers.deferred->height, 0.0f, 1.0f);
        vkCmdSetViewport(m_command_buffers.deferred, 0, 1, &viewport);

        scissor = vks::initializers::rect2D(framebuffers.deferred->width, framebuffers.deferred->height, 0, 0);
        vkCmdSetScissor(m_command_buffers.deferred, 0, 1, &scissor);

        vkCmdBindPipeline(m_command_buffers.deferred, m_pipelines.offscreen);
        RenderScene(m_command_buffers.deferred, false);
        vkCmdEndRenderPass(m_command_buffers.deferred);

        VK_CHECK_RESULT(vkEndCommandBuffer(m_command_buffers.deferred));

        for (int i = 0; i < LIGHT_COUNT; i++)
        {
            m_command_buffers.shadow[i] = m_vulkan_device->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, graphics_queue, false);

            VkClearValue clearValues[1];
            clearValues[0].depthStencil = { 1.0f, 0 };

            VkRenderPassBeginInfo renderPassBeginInfo = vks::initializers::renderPassBeginInfo();
            renderPassBeginInfo.framebuffer = framebuffers.shadow[i]->framebuffer;

            VK_CHECK_RESULT(vkBeginCommandBuffer(m_command_buffers.shadow[i], flags));

            VkViewport viewport = vks::initializers::viewport((float)framebuffers.shadow[i]->width, (float)framebuffers.shadow[i]->height, 0.0f, 1.0f);
            vkCmdSetViewport(m_command_buffers.shadow[i], 0, 1, &viewport);

            VkRect2D scissor = vks::initializers::rect2D(framebuffers.shadow[i]->width, framebuffers.shadow[i]->height, 0, 0);
            vkCmdSetScissor(m_command_buffers.shadow[i], 0, 1, &scissor);

            // Set depth bias (aka "Polygon offset")
            // Required to avoid shadow mapping artifacts 
            vkCmdSetDepthBias(
                m_command_buffers.shadow[i],
                m_depth_bias_constant,
                0.0f,
                m_depth_bias_slope);

            vkCmdBeginRenderPass(m_command_buffers.shadow[i], &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

            vkCmdBindPipeline(m_command_buffers.shadow[i], m_pipelines.shadow);
            vkCmdPushConstants(m_command_buffers.shadow[i], 0, sizeof(int), &i);

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
                vkCmdDrawIndexed(m_command_buffers.shadow[i], mesh.indexCount, 1, 0, mesh.indexBase, 0);
            }

            vkCmdEndRenderPass(m_command_buffers.shadow[i]);

            VK_CHECK_RESULT(vkEndCommandBuffer(m_command_buffers.shadow[i]));
        }

        // Build transfer to host cmd buffer
        {

            if (m_command_buffers.transferToHost == VK_NULL_HANDLE)
            {
                VkCommandBufferAllocateInfo cmdBufAllocateInfo = vks::initializers::commandBufferAllocateInfo(compute_queue, VK_COMMAND_BUFFER_LEVEL_PRIMARY, 1);
                VK_CHECK_RESULT(vkAllocateCommandBuffers(device, &cmdBufAllocateInfo, &m_command_buffers.transferToHost));
            }

            vkBeginCommandBuffer(m_command_buffers.transferToHost, flags);

            const uint32_t numRays = width * height;
            const uint32_t hitBufferSize = numRays * sizeof(Hit);

            VkBufferCopy cmdCopy = { 0, 0, hitBufferSize };
            vkCmdCopyBuffer(m_command_buffers.transferToHost, buffers.hitsLocal.buffer, buffers.hitsStaging.buffer, 1, &cmdCopy);

            VK_CHECK_RESULT(vkEndCommandBuffer(m_command_buffers.transferToHost));
        }

        // Build AO cmd buffer
        {
            VkCommandBufferAllocateInfo cmdBufAllocateInfo =
                vks::initializers::commandBufferAllocateInfo(
                    compute_queue,
                    VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                    1);

            VK_CHECK_RESULT(vkAllocateCommandBuffers(device, &cmdBufAllocateInfo, &m_command_buffers.ao));

            // Build command buffer
            //VkCommandBufferBeginInfo cmdBufInfo = vks::initializers::commandBufferBeginInfo();
            VK_CHECK_RESULT(vkBeginCommandBuffer(m_command_buffers.ao, flags));

            m_profiler->WriteTimestamp(m_command_buffers.ao, startGenerateAOQuery);

            vkCmdBindPipeline(m_command_buffers.ao, m_pipelines.ao);
            //vkCmdBindDescriptorSets(m_command_buffers.ao, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayouts.generateRays, 0, 1, &descriptorSets.ao, 0, 0);

            uint32_t groupSize = 16;
            uint32_t groupCountX = (width + groupSize - 1) / groupSize;
            uint32_t groupCountY = (height + groupSize - 1) / groupSize;

            vkCmdDispatch(m_command_buffers.ao, groupCountX, groupCountY, 1);

            m_profiler->WriteTimestamp(m_command_buffers.ao, endGenerateAOQuery);

            vkEndCommandBuffer(m_command_buffers.ao);
        }

        // Build GI cmd buffer
        {
            //VK_CHECK_RESULT(vkCreateSemaphore(device, &semaphoreCreateInfo, nullptr, &giComplete));

            VkCommandBufferAllocateInfo cmdBufAllocateInfo =
                vks::initializers::commandBufferAllocateInfo(
                    compute_queue,
                    VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                    1);

            VK_CHECK_RESULT(vkAllocateCommandBuffers(device, &cmdBufAllocateInfo, &m_command_buffers.gi));

            // Build command buffer
            //VkCommandBufferBeginInfo cmdBufInfo = vks::initializers::commandBufferBeginInfo();
            VK_CHECK_RESULT(vkBeginCommandBuffer(m_command_buffers.gi, flags));

            m_profiler->WriteTimestamp(m_command_buffers.gi, startGenerateGIRays);

            vkCmdBindPipeline(m_command_buffers.gi, m_pipelines.gi);
            //vkCmdBindDescriptorSets(m_command_buffers.gi, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayouts.generateRays, 0, 1, &descriptorSets.gi, 0, 0);

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

            //VK_CHECK_RESULT(vkCreateSemaphore(device, &semaphoreCreateInfo, nullptr, &aoResolveComplete));

            VkCommandBufferAllocateInfo cmdBufAllocateInfo =
                vks::initializers::commandBufferAllocateInfo(
                    compute_queue,
                    VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                    1);

            VK_CHECK_RESULT(vkAllocateCommandBuffers(device, &cmdBufAllocateInfo, &m_command_buffers.aoResolve));

            // Build command buffer for ao resolve
            //VkCommandBufferBeginInfo cmdBufInfo = vks::initializers::commandBufferBeginInfo();
            VK_CHECK_RESULT(vkBeginCommandBuffer(m_command_buffers.aoResolve, flags));

            m_profiler->WriteTimestamp(m_command_buffers.aoResolve, startAOResolve);

            vkCmdBindPipeline(m_command_buffers.aoResolve, m_pipelines.aoResolve);
            //vkCmdBindDescriptorSets(m_command_buffers.aoResolve, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayouts.aoResolve, 0, 1, &descriptorSets.aoResolve, 0, 0);

            vkCmdPushConstants(m_command_buffers.aoResolve, 0, sizeof(int), &clearBuffer);

            uint32_t groupSize = 16;
            uint32_t groupCountX = (width + groupSize - 1) / groupSize;
            uint32_t groupCountY = (height + groupSize - 1) / groupSize;

            vkCmdDispatch(m_command_buffers.aoResolve, groupCountX, groupCountY, 1);

            m_profiler->WriteTimestamp(m_command_buffers.aoResolve, endAOResolve);

            vkEndCommandBuffer(m_command_buffers.aoResolve);

            VK_CHECK_RESULT(vkAllocateCommandBuffers(device, &cmdBufAllocateInfo, &m_command_buffers.aoResolveAndClear));
            // Build command buffer for ao resolve and clear
            VK_CHECK_RESULT(vkBeginCommandBuffer(m_command_buffers.aoResolveAndClear, flags));

            vkCmdBindPipeline(m_command_buffers.aoResolveAndClear, m_pipelines.aoResolve);
            //vkCmdBindDescriptorSets(m_command_buffers.aoResolveAndClear, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayouts.aoResolve, 0, 1, &descriptorSets.aoResolve, 0, 0);

            vkCmdPushConstants(m_command_buffers.aoResolveAndClear,  0, sizeof(int), &preserveBuffer);

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

            //VK_CHECK_RESULT(vkCreateSemaphore(device, &semaphoreCreateInfo, nullptr, &giResolveComplete));

            VkCommandBufferAllocateInfo cmdBufAllocateInfo =
                vks::initializers::commandBufferAllocateInfo(
                    compute_queue,
                    VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                    1);

            VK_CHECK_RESULT(vkAllocateCommandBuffers(device, &cmdBufAllocateInfo, &m_command_buffers.giResolve));

            // Build command buffer for ao resolve
            //VkCommandBufferBeginInfo cmdBufInfo = vks::initializers::commandBufferBeginInfo();
            VK_CHECK_RESULT(vkBeginCommandBuffer(m_command_buffers.giResolve, flags));

            m_profiler->WriteTimestamp(m_command_buffers.giResolve, startGIResolve);

            vkCmdBindPipeline(m_command_buffers.giResolve, m_pipelines.giResolve);
            //vkCmdBindDescriptorSets(m_command_buffers.giResolve, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayouts.giResolve, 0, 1, &descriptorSets.giResolve, 0, 0);

            vkCmdPushConstants(m_command_buffers.giResolve, 0, sizeof(int), &clearBuffer);

            vkCmdDispatch(m_command_buffers.giResolve, groupCountX, groupCountY, 1);

            m_profiler->WriteTimestamp(m_command_buffers.giResolve, endGIResolve);

            vkEndCommandBuffer(m_command_buffers.giResolve);

            VK_CHECK_RESULT(vkAllocateCommandBuffers(device, &cmdBufAllocateInfo, &m_command_buffers.giResolveAndClear));
            // Build command buffer for ao resolve and clear
            VK_CHECK_RESULT(vkBeginCommandBuffer(m_command_buffers.giResolveAndClear, flags));

            vkCmdBindPipeline(m_command_buffers.giResolveAndClear, m_pipelines.giResolve);
            //vkCmdBindDescriptorSets(m_command_buffers.giResolveAndClear, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayouts.giResolve, 0, 1, &descriptorSets.giResolve, 0, 0);

            vkCmdPushConstants(m_command_buffers.giResolveAndClear, 0, sizeof(int), &preserveBuffer);

            vkCmdDispatch(m_command_buffers.giResolveAndClear, groupCountX, groupCountY, 1);

            vkEndCommandBuffer(m_command_buffers.giResolveAndClear);
        }

        // Build GI filter cmd buffer
        {
            //VK_CHECK_RESULT(vkCreateSemaphore(device, &semaphoreCreateInfo, nullptr, &bilateralFilterComplete));

            VkCommandBufferAllocateInfo cmdBufAllocateInfo =
                vks::initializers::commandBufferAllocateInfo(
                    compute_queue,
                    VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                    1);

            VK_CHECK_RESULT(vkAllocateCommandBuffers(device, &cmdBufAllocateInfo, &m_command_buffers.bilateralFilter));

            uint32_t groupSize = 16;
            uint32_t groupCountX = (width + groupSize - 1) / groupSize;
            uint32_t groupCountY = (height + groupSize - 1) / groupSize;

            // Build command buffer for ao resolve
            //VkCommandBufferBeginInfo cmdBufInfo = vks::initializers::commandBufferBeginInfo();
            VK_CHECK_RESULT(vkBeginCommandBuffer(m_command_buffers.bilateralFilter, flags));

            m_profiler->WriteTimestamp(m_command_buffers.bilateralFilter, startGIBilateralFilter);

            vkCmdBindPipeline(m_command_buffers.bilateralFilter, m_pipelines.bilateralFilter);
            //vkCmdBindDescriptorSets(m_command_buffers.bilateralFilter, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayouts.bilateralFilter, 0, 1, &descriptorSets.bilateralFilter, 0, 0);

            vkCmdDispatch(m_command_buffers.bilateralFilter, groupCountX, groupCountY, 1);

            m_profiler->WriteTimestamp(m_command_buffers.bilateralFilter, endGIBilateralFilter);

            vkEndCommandBuffer(m_command_buffers.bilateralFilter);
        }

        // Build AO filter cmd buffer
        {
            //VK_CHECK_RESULT(vkCreateSemaphore(device, &semaphoreCreateInfo, nullptr, &bilateralFilterAOComplete));

            VkCommandBufferAllocateInfo cmdBufAllocateInfo =
                vks::initializers::commandBufferAllocateInfo(
                    compute_queue,
                    VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                    1);

            VK_CHECK_RESULT(vkAllocateCommandBuffers(device, &cmdBufAllocateInfo, &m_command_buffers.bilateralFilterAO));

            // Build command buffer for ao resolve
            //VkCommandBufferBeginInfo cmdBufInfo = vks::initializers::commandBufferBeginInfo();
            VK_CHECK_RESULT(vkBeginCommandBuffer(m_command_buffers.bilateralFilterAO, flags));

            m_profiler->WriteTimestamp(m_command_buffers.bilateralFilterAO, startAOBilateralFilter);

            vkCmdBindPipeline(m_command_buffers.bilateralFilterAO, m_pipelines.bilateralFilter);
            //vkCmdBindDescriptorSets(m_command_buffers.bilateralFilterAO, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayouts.bilateralFilter, 0, 1, &descriptorSets.bilateralFilterAO, 0, 0);

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
                VkCommandBufferAllocateInfo cmdBufAllocateInfo = vks::initializers::commandBufferAllocateInfo(compute_queue, VK_COMMAND_BUFFER_LEVEL_PRIMARY, 1);
                VK_CHECK_RESULT(vkAllocateCommandBuffers(device, &cmdBufAllocateInfo, &m_command_buffers.dbg_transferRaysToHost));
            }

            //VkCommandBufferBeginInfo cmdBufInfo = vks::initializers::commandBufferBeginInfo();
            vkBeginCommandBuffer(m_command_buffers.dbg_transferRaysToHost, flags);

            //VkBufferMemoryBarrier memBarries = vks::initializers::bufferMemoryBarrier();
            //memBarries.buffer = buffers.raysLocal.buffer;
            //memBarries.offset = 0;
            //memBarries.size = buffers.raysLocal.size;
            //memBarries.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            //memBarries.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

            //vkCmdPipelineBarrier(m_command_buffers.dbg_transferRaysToHost, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 1, &memBarries, 0, nullptr);

            const uint32_t numRays = width * height;
            const uint32_t rayBufferSize = numRays * sizeof(Ray);

            VkBufferCopy cmdCopy = { 0, 0, rayBufferSize };
            vkCmdCopyBuffer(m_command_buffers.dbg_transferRaysToHost, buffers.raysLocal.buffer, buffers.raysStaging.buffer, 1, &cmdCopy);

            VK_CHECK_RESULT(vkEndCommandBuffer(m_command_buffers.dbg_transferRaysToHost));
        }

        // Build texture repack cmd buffer
        {
            VkCommandBufferAllocateInfo cmdBufAllocateInfo =
                vks::initializers::commandBufferAllocateInfo(
                    compute_queue,
                    VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                    1);

            VK_CHECK_RESULT(vkAllocateCommandBuffers(device, &cmdBufAllocateInfo, &m_command_buffers.textureRepack));
        }

        uint32_t num_rays = width * height;
        //TODO:
        //rrBindBuffers(m_rr_instance, buffers.raysLocal.buffer, buffers.hitsLocal.buffer, num_rays);
        //auto status = rrTraceRays(m_rr_instance, RR_QUERY_INTERSECT, num_rays, &m_command_buffers.traceRays);
    }

    void VkRenderer::RenderScene(VkCommandBuffer cmdBuffer, bool shadow)
    {
        //VkDeviceSize offsets[1] = { 0 };

        //// Render from global buffer using index offsets
        //vkCmdBindVertexBuffers(cmdBuffer, VERTEX_BUFFER_BIND_ID, 1, &scene->vertexBuffer.buffer, offsets);
        //vkCmdBindIndexBuffer(cmdBuffer, scene->indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);

        //for (auto mesh : scene->meshes)
        //{
        //    pushConsts.baseDiffuse = mesh.material->baseDiffuse;
        //    pushConsts.baseRoughness = mesh.material->baseRoughness;
        //    pushConsts.baseMetallic = mesh.material->baseMetallic;

        //    vkCmdBindDescriptorSets(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, scene->pipelineLayout, 0, 1, &mesh.descriptorSet, 0, NULL);
        //    vkCmdPushConstants(cmdBuffer, scene->pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConsts), &pushConsts);
        //    //vkCmdDrawIndexed(cmdBuffer, mesh.indexCount, 1, 0, mesh.indexBase, 0);
        //    vkCmdDrawIndexed(cmdBuffer, mesh.indexCount, 1, mesh.indexBase, 0, 0);

        //    pushConsts.meshID[0]++;
        //}
    }
    void VkRenderer::PreparePipelines()
    {
        auto& device = m_vulkan_device->logicalDevice;
        //VkPipelineInputAssemblyStateCreateInfo inputAssemblyState =
        //    vks::initializers::pipelineInputAssemblyStateCreateInfo(
        //        VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        //        0,
        //        VK_FALSE);

        //VkPipelineRasterizationStateCreateInfo rasterizationState =
        //    vks::initializers::pipelineRasterizationStateCreateInfo(
        //        VK_POLYGON_MODE_FILL,
        //        VK_CULL_MODE_BACK_BIT,
        //        VK_FRONT_FACE_CLOCKWISE,
        //        0);

        //VkPipelineColorBlendAttachmentState blendAttachmentState =
        //    vks::initializers::pipelineColorBlendAttachmentState(
        //        0xf,
        //        VK_FALSE);

        //VkPipelineColorBlendStateCreateInfo colorBlendState =
        //    vks::initializers::pipelineColorBlendStateCreateInfo(
        //        1,
        //        &blendAttachmentState);

        //VkPipelineDepthStencilStateCreateInfo depthStencilState =
        //    vks::initializers::pipelineDepthStencilStateCreateInfo(
        //        VK_TRUE,
        //        VK_TRUE,
        //        VK_COMPARE_OP_LESS_OR_EQUAL);

        //VkPipelineViewportStateCreateInfo viewportState =
        //    vks::initializers::pipelineViewportStateCreateInfo(1, 1, 0);

        VkPipelineMultisampleStateCreateInfo multisampleState =
            vks::initializers::pipelineMultisampleStateCreateInfo(
                VK_SAMPLE_COUNT_1_BIT);

        //std::vector<VkDynamicState> dynamicStateEnables = {
        //    VK_DYNAMIC_STATE_VIEWPORT,
        //    VK_DYNAMIC_STATE_SCISSOR
        //};
        //VkPipelineDynamicStateCreateInfo dynamicState =
        //    vks::initializers::pipelineDynamicStateCreateInfo(
        //        dynamicStateEnables.data(),
        //        static_cast<uint32_t>(dynamicStateEnables.size()),
        //        0);

        // Final fullscreen pass pipeline
        std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages;

        shaderStages[0] = LoadShader(GetAssetPath() + "shaders/deferred.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
        shaderStages[1] = LoadShader(GetAssetPath() + "shaders/deferred.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);

        VkGraphicsPipelineCreateInfo pipelineCreateInfo;

        //pipelineCreateInfo.pVertexInputState = &vertices.inputState;
        //pipelineCreateInfo.pInputAssemblyState = &inputAssemblyState;
        //pipelineCreateInfo.pRasterizationState = &rasterizationState;
        //pipelineCreateInfo.pColorBlendState = &colorBlendState;
        //pipelineCreateInfo.pMultisampleState = &multisampleState;
        //pipelineCreateInfo.pViewportState = &viewportState;
        //pipelineCreateInfo.pDepthStencilState = &depthStencilState;
        //pipelineCreateInfo.pDynamicState = &dynamicState;
        pipelineCreateInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
        pipelineCreateInfo.pStages = shaderStages.data();

        VK_CHECK_RESULT(vkCreateGraphicsPipeline(device, &pipelineCreateInfo, &m_pipelines.deferred));

        // Debug display pipeline
        shaderStages[0] = LoadShader(GetAssetPath() + "shaders/debug.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
        shaderStages[1] = LoadShader(GetAssetPath() + "shaders/debug.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
        VK_CHECK_RESULT(vkCreateGraphicsPipeline(device, &pipelineCreateInfo, &m_pipelines.debug));

        // Offscreen pipeline
        shaderStages[0] = LoadShader(GetAssetPath() + "shaders/mrt.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
        shaderStages[1] = LoadShader(GetAssetPath() + "shaders/mrt.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);

        // Separate render pass
        //pipelineCreateInfo.renderPass = frameBuffers.deferred->renderPass;

        // Separate layout
        //pipelineCreateInfo.layout = scene->pipelineLayout;// pipelineLayouts.offscreen;

                                                          // Blend attachment states required for all color attachments
                                                          // This is important, as color write mask will otherwise be 0x0 and you
                                                          // won't see anything rendered to the attachment
        std::array<VkPipelineColorBlendAttachmentState, 3> blendAttachmentStates =
        {
            vks::initializers::pipelineColorBlendAttachmentState(0xf, VK_FALSE),
            vks::initializers::pipelineColorBlendAttachmentState(0xf, VK_FALSE),
            vks::initializers::pipelineColorBlendAttachmentState(0xf, VK_FALSE)
        };

        //colorBlendState.attachmentCount = static_cast<uint32_t>(blendAttachmentStates.size());
        //colorBlendState.pAttachments = blendAttachmentStates.data();

        VK_CHECK_RESULT(vkCreateGraphicsPipeline(device, &pipelineCreateInfo, &m_pipelines.offscreen));

        shaderStages[0] = LoadShader(GetAssetPath() + "shaders/shadow.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
        shaderStages[1] = LoadShader(GetAssetPath() + "shaders/shadow.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
        //// No blend attachment states (no color attachments used)
        //colorBlendState.attachmentCount = 0;
        //// Cull front faces
        //depthStencilState.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
        //// Enable depth bias
        //rasterizationState.depthBiasEnable = VK_TRUE;
        //// Add depth bias to dynamic state, so we can change it at runtime
        //dynamicStateEnables.push_back(VK_DYNAMIC_STATE_DEPTH_BIAS);
        //dynamicState = vks::initializers::pipelineDynamicStateCreateInfo(dynamicStateEnables.data(), static_cast<uint32_t>(dynamicStateEnables.size()), 0);

        //pipelineCreateInfo.layout = pipelineLayouts.shadow;
        //pipelineCreateInfo.renderPass = frameBuffers.shadow[0]->renderPass;

        VK_CHECK_RESULT(vkCreateGraphicsPipeline(device, &pipelineCreateInfo, &m_pipelines.shadow));
        auto comp_shader_stage = LoadShader(GetAssetPath() + "shaders/ao.comp.spv", VK_SHADER_STAGE_COMPUTE_BIT);

        // Pipeline for AO
        {
            //VkComputePipelineCreateInfo computePipelineCreateInfo = vks::initializers::computePipelineCreateInfo(pipelineLayouts.generateRays, 0);
            VkComputePipelineCreateInfo computePipelineCreateInfo;
            computePipelineCreateInfo.stage = &comp_shader_stage;

            VK_CHECK_RESULT(vkCreateComputePipeline(device, &computePipelineCreateInfo, &m_pipelines.ao));
        }

        // Pipeline for GI
        {
            comp_shader_stage = LoadShader(GetAssetPath() + "shaders/gi.comp.spv", VK_SHADER_STAGE_COMPUTE_BIT);
            //VkComputePipelineCreateInfo computePipelineCreateInfo = vks::initializers::computePipelineCreateInfo(pipelineLayouts.generateRays, 0);
            VkComputePipelineCreateInfo computePipelineCreateInfo;

            computePipelineCreateInfo.stage = &comp_shader_stage;

            VK_CHECK_RESULT(vkCreateComputePipeline(device, &computePipelineCreateInfo, &m_pipelines.gi));
        }

        // Pipeline for AO resolve
        {
            //VkComputePipelineCreateInfo computePipelineCreateInfo = vks::initializers::computePipelineCreateInfo(pipelineLayouts.aoResolve, 0);
            VkComputePipelineCreateInfo computePipelineCreateInfo;
            comp_shader_stage = LoadShader(GetAssetPath() + "shaders/ao_resolve.comp.spv", VK_SHADER_STAGE_COMPUTE_BIT);
            computePipelineCreateInfo.stage = &comp_shader_stage;

            VK_CHECK_RESULT(vkCreateComputePipeline(device, &computePipelineCreateInfo, &m_pipelines.aoResolve));
        }

        // Pipeline for GI resolve
        {
            //VkComputePipelineCreateInfo computePipelineCreateInfo = vks::initializers::computePipelineCreateInfo(pipelineLayouts.giResolve, 0);
            VkComputePipelineCreateInfo computePipelineCreateInfo;
            comp_shader_stage = LoadShader(GetAssetPath() + "shaders/gi_resolve.comp.spv", VK_SHADER_STAGE_COMPUTE_BIT);
            computePipelineCreateInfo.stage = &comp_shader_stage;

            VK_CHECK_RESULT(vkCreateComputePipeline(device, &computePipelineCreateInfo, &m_pipelines.giResolve));
        }

        // Pipeline for filter
        {
            //VkComputePipelineCreateInfo computePipelineCreateInfo = vks::initializers::computePipelineCreateInfo(pipelineLayouts.bilateralFilter, 0);
            VkComputePipelineCreateInfo computePipelineCreateInfo;
            comp_shader_stage = LoadShader(GetAssetPath() + "shaders/biltateral_filter.comp.spv", VK_SHADER_STAGE_COMPUTE_BIT);
            computePipelineCreateInfo.stage = &comp_shader_stage;

            VK_CHECK_RESULT(vkCreateComputePipeline(device, &computePipelineCreateInfo, &m_pipelines.bilateralFilter));
        }

        // Pipeline for texture repack
        {
            //VkComputePipelineCreateInfo computePipelineCreateInfo = vks::initializers::computePipelineCreateInfo(pipelineLayouts.textureRepack, 0);
            VkComputePipelineCreateInfo computePipelineCreateInfo;
            comp_shader_stage = LoadShader(GetAssetPath() + "shaders/texture_repack.comp.spv", VK_SHADER_STAGE_COMPUTE_BIT);
            computePipelineCreateInfo.stage = &comp_shader_stage;

            VK_CHECK_RESULT(vkCreateComputePipeline(device, &computePipelineCreateInfo, &m_pipelines.textureRepack));
        }
    }

    VkPipelineShaderStageCreateInfo VkRenderer::LoadShader(std::string fileName, VkShaderStageFlagBits stage)
    {
        VkPipelineShaderStageCreateInfo shader_stage = {};
        //shaderStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        //shaderStage.stage = stage;
#if defined(VK_USE_PLATFORM_ANDROID_KHR)
        shader_stage.module = vks::tools::LoadShader(androidApp->activity->assetManager, fileName.c_str(), device);
#else
        shader_stage.module = vks::tools::loadShader(fileName.c_str(), m_vulkan_device->logicalDevice);
#endif
        shader_stage.pEntryPoint = "main"; // todo : make param
        assert(shader_stage.module != VK_NULL_HANDLE);
        m_shader_modules.push_back(shader_stage.module);
        return shader_stage;
    }
}
