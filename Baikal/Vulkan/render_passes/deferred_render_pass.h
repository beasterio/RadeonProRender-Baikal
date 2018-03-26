#pragma once

#include <vector>

#include "vulkan/vulkan.h"

#include "VulkanFrameBuffer.hpp"

#include "render_pass.h"

#include "../resources/resource_manager.h"

#include "../utils/static_hash.h"
#include "gbuffer_render_pass.h"
#include "shadow_render_pass.h"

//#include "imgui_impl_win32_vulkan.h"
#include "../utils/irradiance_grid.h"

class DeferredRenderPass : public RenderPass
{
public:
    struct PushConst {
        glm::vec4 giAoEnabled;
        glm::vec4 bbox_scene_min;
        glm::vec4 probe_dist;
        glm::vec4 probe_count;
    };

    struct BuildCommandBufferInfo {
        GBufferRenderPass& g_buffer_pass;
        VkRenderPass render_pass;
        VkFramebuffer frame_buffer;
        VkCommandBuffer cmd_buffer;
        bool ao_enabled;
        bool gi_enabled;
        bool debug_view_enabled;
    };
public:
    DeferredRenderPass(vks::VulkanDevice* device, GBufferRenderPass& g_buffer_pass, ShadowRenderPass& shadow_pass, IrradianceGrid& irradiance_grid, VkRenderPass render_pass, VkDescriptorImageInfo gi_buffer, VkSemaphore semaphore, ResourceManager* resources) :
        RenderPass(device)
        , _num_debug_images(0)
        , _resources(resources)
    {
        AllocateResources(g_buffer_pass, shadow_pass, render_pass, gi_buffer);
        AllocateMLAAResources(g_buffer_pass, render_pass);

        _signal_finished = semaphore;
        _pipeline = _resources->GetPipepineList().Get(_pass_name_hash);
        _pipeline_debug_view = _resources->GetPipepineList().Get(_debug_view_hash);
        _irradiance_grid_properties = irradiance_grid.GetProperties();

        _descriptor_set = _resources->GetDescriptionSetList().Get(_pass_name_hash);
        _descriptor_set_debug_view = _resources->GetDescriptionSetList().Get(_debug_view_hash);

        VkSemaphoreCreateInfo semaphoreCreateInfo = vks::initializers::semaphoreCreateInfo();

        VK_CHECK_RESULT(vkCreateSemaphore(device->logicalDevice, &semaphoreCreateInfo, nullptr, &_edge_detect_finished));
        VK_CHECK_RESULT(vkCreateSemaphore(device->logicalDevice, &semaphoreCreateInfo, nullptr, &_weight_calc_finished));
        VK_CHECK_RESULT(vkCreateSemaphore(device->logicalDevice, &semaphoreCreateInfo, nullptr, &_deferred_finished));
    }

    ~DeferredRenderPass() {
        delete _edge_detect_buffer;
        delete _weight_buffer;
        delete _deferred_buffer;

        vkFreeCommandBuffers(_device->logicalDevice, _device->commandPool, 1, &_edge_detect_cmd_buf);
        vkFreeCommandBuffers(_device->logicalDevice, _device->commandPool, 1, &_deferred_cmd_buf);
        vkFreeCommandBuffers(_device->logicalDevice, _device->commandPool, 1, &_weight_cmd_buf);
        
        vkDestroyBuffer(_device->logicalDevice, _fsq_vertex_buffer.buffer, nullptr);
        vkFreeMemory(_device->logicalDevice, _fsq_vertex_buffer.memory, nullptr);
        vkDestroyBuffer(_device->logicalDevice, _fsq_index_buffer.buffer, nullptr);
        vkFreeMemory(_device->logicalDevice, _fsq_index_buffer.memory, nullptr);

        vkDestroySemaphore(_device->logicalDevice, _edge_detect_finished, nullptr);
        vkDestroySemaphore(_device->logicalDevice, _weight_calc_finished, nullptr);
        vkDestroySemaphore(_device->logicalDevice, _deferred_finished, nullptr);

        _signal_finished = VK_NULL_HANDLE;
    }

    void BuildCommandBuffer(BuildCommandBufferInfo &build_cmd_buf_info) {
        GBufferRenderPass& g_buffer_pass = build_cmd_buf_info.g_buffer_pass;
        VkRenderPass render_pass = build_cmd_buf_info.render_pass;
        VkFramebuffer frame_buffer = build_cmd_buf_info.frame_buffer;
        VkCommandBuffer cmd_buffer = build_cmd_buf_info.cmd_buffer;

        {
            VkClearValue clear_values[1] = {
                { 0.f, 0.f, 0.f, 0.f }
            };

            const uint32_t width = g_buffer_pass.GetWidth();
            const uint32_t height = g_buffer_pass.GetHeight();

            VkRenderPassBeginInfo render_pass_begin_info = vks::initializers::renderPassBeginInfo();
            render_pass_begin_info.renderPass = _edge_detect_buffer->renderPass;
            render_pass_begin_info.renderArea.offset.x = 0;
            render_pass_begin_info.renderArea.offset.y = 0;
            render_pass_begin_info.renderArea.extent.width = width;
            render_pass_begin_info.renderArea.extent.height = height;
            render_pass_begin_info.clearValueCount = 1;
            render_pass_begin_info.pClearValues = clear_values;
            render_pass_begin_info.framebuffer = _edge_detect_buffer->framebuffer;

            VkCommandBufferBeginInfo cmd_buf_info = vks::initializers::commandBufferBeginInfo();
            VK_CHECK_RESULT(vkBeginCommandBuffer(_edge_detect_cmd_buf, &cmd_buf_info));
            vkCmdBeginRenderPass(_edge_detect_cmd_buf, &render_pass_begin_info, VK_SUBPASS_CONTENTS_INLINE);

            VkViewport viewport = vks::initializers::viewport((float)width, (float)height, 0.0f, 1.0f);
            vkCmdSetViewport(_edge_detect_cmd_buf, 0, 1, &viewport);

            VkRect2D scissor = vks::initializers::rect2D(width, height, 0, 0);
            vkCmdSetScissor(_edge_detect_cmd_buf, 0, 1, &scissor);

            VkDeviceSize offsets[1] = { 0 };
            vkCmdBindDescriptorSets(_edge_detect_cmd_buf, VK_PIPELINE_BIND_POINT_GRAPHICS, _edge_detect_pipeline_layout, 0, 1, &_edge_detect_descriptor_set, 0, NULL);

            vkCmdBindPipeline(_edge_detect_cmd_buf, VK_PIPELINE_BIND_POINT_GRAPHICS, _edge_detect_pipeline);
            vkCmdBindVertexBuffers(_edge_detect_cmd_buf, 0, 1, &_fsq_vertex_buffer.buffer, offsets);
            vkCmdBindIndexBuffer(_edge_detect_cmd_buf, _fsq_index_buffer.buffer, 0, VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexed(_edge_detect_cmd_buf, _fsq_index_count, 1, 0, 0, 0);

            vkCmdEndRenderPass(_edge_detect_cmd_buf);

            VK_CHECK_RESULT(vkEndCommandBuffer(_edge_detect_cmd_buf));
        }

        {
            VkClearValue clear_values[1] = {
                { 0.f, 0.f, 0.f, 0.f },
             };

            const uint32_t width = g_buffer_pass.GetWidth();
            const uint32_t height = g_buffer_pass.GetHeight();

            VkRenderPassBeginInfo render_pass_begin_info = vks::initializers::renderPassBeginInfo();
            render_pass_begin_info.renderPass = _deferred_buffer->renderPass;
            render_pass_begin_info.renderArea.offset.x = 0;
            render_pass_begin_info.renderArea.offset.y = 0;
            render_pass_begin_info.renderArea.extent.width = width;
            render_pass_begin_info.renderArea.extent.height = height;
            render_pass_begin_info.clearValueCount = 1;
            render_pass_begin_info.pClearValues = clear_values;
            render_pass_begin_info.framebuffer = _deferred_buffer->framebuffer;

            VkCommandBufferBeginInfo cmd_buf_info = vks::initializers::commandBufferBeginInfo();
            VK_CHECK_RESULT(vkBeginCommandBuffer(_deferred_cmd_buf, &cmd_buf_info));

            vkCmdBeginRenderPass(_deferred_cmd_buf, &render_pass_begin_info, VK_SUBPASS_CONTENTS_INLINE);

            VkViewport viewport = vks::initializers::viewport((float)width, (float)height, 0.0f, 1.0f);
            vkCmdSetViewport(_deferred_cmd_buf, 0, 1, &viewport);

            VkRect2D scissor = vks::initializers::rect2D(width, height, 0, 0);
            vkCmdSetScissor(_deferred_cmd_buf, 0, 1, &scissor);

            VkDeviceSize offsets[1] = { 0 };
            vkCmdBindDescriptorSets(_deferred_cmd_buf, VK_PIPELINE_BIND_POINT_GRAPHICS, _pipeline_layout, 0, 1, &_descriptor_set, 0, NULL);

            PushConst push_consts = {
                glm::vec4(build_cmd_buf_info.gi_enabled ? 1.0f : 0.0f, build_cmd_buf_info.ao_enabled ? 1.0f : 0.0f, 0.0f, 1.0f),
                glm::vec4(_irradiance_grid_properties.scene_min, 1.0f),
                glm::vec4(_irradiance_grid_properties.probes_count, 1.0f),
                glm::vec4(_irradiance_grid_properties.probe_dist, 1.0f)
            };

            vkCmdPushConstants(_deferred_cmd_buf, _pipeline_layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConst), &push_consts);

            vkCmdBindPipeline(_deferred_cmd_buf, VK_PIPELINE_BIND_POINT_GRAPHICS, _pipeline);
            vkCmdBindVertexBuffers(_deferred_cmd_buf, 0, 1, &_fsq_vertex_buffer.buffer, offsets);
            vkCmdBindIndexBuffer(_deferred_cmd_buf, _fsq_index_buffer.buffer, 0, VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexed(_deferred_cmd_buf, _fsq_index_count, 1, 0, 0, 0);

            vkCmdEndRenderPass(_deferred_cmd_buf);

            VK_CHECK_RESULT(vkEndCommandBuffer(_deferred_cmd_buf));
        }

        {
            VkClearValue clear_values[1] = {
                { 0.f, 0.f, 0.f, 0.f }
            };

            const uint32_t width = g_buffer_pass.GetWidth();
            const uint32_t height = g_buffer_pass.GetHeight();

            VkRenderPassBeginInfo render_pass_begin_info = vks::initializers::renderPassBeginInfo();
            render_pass_begin_info.renderPass = _weight_buffer->renderPass;
            render_pass_begin_info.renderArea.offset.x = 0;
            render_pass_begin_info.renderArea.offset.y = 0;
            render_pass_begin_info.renderArea.extent.width = width;
            render_pass_begin_info.renderArea.extent.height = height;
            render_pass_begin_info.clearValueCount = 1;
            render_pass_begin_info.pClearValues = clear_values;
            render_pass_begin_info.framebuffer = _weight_buffer->framebuffer;

            VkCommandBufferBeginInfo cmd_buf_info = vks::initializers::commandBufferBeginInfo();
            VK_CHECK_RESULT(vkBeginCommandBuffer(_weight_cmd_buf, &cmd_buf_info));

            vkCmdBeginRenderPass(_weight_cmd_buf, &render_pass_begin_info, VK_SUBPASS_CONTENTS_INLINE);

            VkViewport viewport = vks::initializers::viewport((float)width, (float)height, 0.0f, 1.0f);
            vkCmdSetViewport(_weight_cmd_buf, 0, 1, &viewport);

            VkRect2D scissor = vks::initializers::rect2D(width, height, 0, 0);
            vkCmdSetScissor(_weight_cmd_buf, 0, 1, &scissor);

            VkDeviceSize offsets[1] = { 0 };
            vkCmdBindDescriptorSets(_weight_cmd_buf, VK_PIPELINE_BIND_POINT_GRAPHICS, _weight_pipeline_layout, 0, 1, &_weight_descriptor_set, 0, NULL);

            vkCmdBindPipeline(_weight_cmd_buf, VK_PIPELINE_BIND_POINT_GRAPHICS, _weight_pipeline);
            vkCmdBindVertexBuffers(_weight_cmd_buf, 0, 1, &_fsq_vertex_buffer.buffer, offsets);
            vkCmdBindIndexBuffer(_weight_cmd_buf, _fsq_index_buffer.buffer, 0, VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexed(_weight_cmd_buf, _fsq_index_count, 1, 0, 0, 0);

            vkCmdEndRenderPass(_weight_cmd_buf);

            VK_CHECK_RESULT(vkEndCommandBuffer(_weight_cmd_buf));
        }

        {
            VkClearValue clear_values[2] = {
                { 0.f, 0.f, 0.f, 0.f },
                { 1.0f, 0.f }
            };

            const uint32_t width = g_buffer_pass.GetWidth();
            const uint32_t height = g_buffer_pass.GetHeight();

            VkRenderPassBeginInfo render_pass_begin_info = vks::initializers::renderPassBeginInfo();
            render_pass_begin_info.renderPass = render_pass;
            render_pass_begin_info.renderArea.offset.x = 0;
            render_pass_begin_info.renderArea.offset.y = 0;
            render_pass_begin_info.renderArea.extent.width = width;
            render_pass_begin_info.renderArea.extent.height = height;
            render_pass_begin_info.clearValueCount = 2;
            render_pass_begin_info.pClearValues = clear_values;
            render_pass_begin_info.framebuffer = frame_buffer;

            VkCommandBufferBeginInfo cmd_buf_info = vks::initializers::commandBufferBeginInfo();
            VK_CHECK_RESULT(vkBeginCommandBuffer(cmd_buffer, &cmd_buf_info));

            vkCmdBeginRenderPass(cmd_buffer, &render_pass_begin_info, VK_SUBPASS_CONTENTS_INLINE);

            VkViewport viewport = vks::initializers::viewport((float)width, (float)height, 0.0f, 1.0f);
            vkCmdSetViewport(cmd_buffer, 0, 1, &viewport);

            VkRect2D scissor = vks::initializers::rect2D(width, height, 0, 0);
            vkCmdSetScissor(cmd_buffer, 0, 1, &scissor);

            VkDeviceSize offsets[1] = { 0 };
            vkCmdBindDescriptorSets(cmd_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, _final_blend_pipeline_layout, 0, 1, &_final_blend_descriptor_set, 0, NULL);

            vkCmdBindPipeline(cmd_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, _final_blend_pipeline);
            vkCmdBindVertexBuffers(cmd_buffer, 0, 1, &_fsq_vertex_buffer.buffer, offsets);
            vkCmdBindIndexBuffer(cmd_buffer, _fsq_index_buffer.buffer, 0, VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexed(cmd_buffer, _fsq_index_count, 1, 0, 0, 0);

#if ENABLE_PROFILER
            ImGui_ImplWin32Vulkan_Render(cmd_buffer);
#endif

            vkCmdEndRenderPass(cmd_buffer);

            VK_CHECK_RESULT(vkEndCommandBuffer(cmd_buffer));
        }
    }

    void Execute(VkQueue queue, VkCommandBuffer cmd_buffer) {
        VkSubmitInfo submit_info = {};

        if (!_dependencies.empty()) {
            submit_info.pWaitSemaphores = &_dependencies[0];
            submit_info.waitSemaphoreCount = _dependencies.size();
        }

        if (!_stage_wait_flags.empty()) {
            submit_info.pWaitDstStageMask = &_stage_wait_flags[0];
        }

        // Deferred pass
        submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit_info.pSignalSemaphores = &_deferred_finished;
        submit_info.signalSemaphoreCount = 1;
        submit_info.commandBufferCount = 1;
        submit_info.pCommandBuffers = &_deferred_cmd_buf;
        VK_CHECK_RESULT(vkQueueSubmit(queue, 1, &submit_info, VK_NULL_HANDLE));

        // Edge detect pass
        submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit_info.pSignalSemaphores = &_edge_detect_finished;
        submit_info.pWaitSemaphores = &_deferred_finished;
        submit_info.waitSemaphoreCount = 1;
        submit_info.signalSemaphoreCount = 1;
        submit_info.commandBufferCount = 1;
        submit_info.pCommandBuffers = &_edge_detect_cmd_buf;
        VK_CHECK_RESULT(vkQueueSubmit(queue, 1, &submit_info, VK_NULL_HANDLE));

        // Weight calc pass
        submit_info.pWaitSemaphores = &_edge_detect_finished;
        submit_info.waitSemaphoreCount = 1;
        submit_info.pSignalSemaphores = &_weight_calc_finished;
        submit_info.signalSemaphoreCount = 1;
        submit_info.commandBufferCount = 1;
        submit_info.pCommandBuffers = &_weight_cmd_buf;
        VK_CHECK_RESULT(vkQueueSubmit(queue, 1, &submit_info, VK_NULL_HANDLE));

        submit_info.pWaitSemaphores = &_weight_calc_finished;
        submit_info.waitSemaphoreCount = 1;
        submit_info.pSignalSemaphores = &_signal_finished;
        submit_info.signalSemaphoreCount = 1;
        submit_info.commandBufferCount = 1;
        submit_info.pCommandBuffers = &cmd_buffer;

        VK_CHECK_RESULT(vkQueueSubmit(queue, 1, &submit_info, VK_NULL_HANDLE));
    }

    void Execute(VkQueue queue) {}

    const char* GetName() { return _pass_name; };

    void SetDebugImagesOutput(std::vector<VkImageView> const& imageView, GBufferRenderPass& g_buffer_pass) {
        assert(imageView.size() < _max_debug_images + 1);

        _num_debug_images = imageView.size();

        std::vector<VkWriteDescriptorSet> writeDescriptorSets;
        std::vector<VkDescriptorImageInfo> imageInfo;

        imageInfo.resize(imageView.size());

        VkSampler sampler = g_buffer_pass.GetFrameBuffer()->sampler;

        for (size_t i = 0; i < imageView.size(); i++)
        {
            imageInfo[i] = vks::initializers::descriptorImageInfo(
                sampler,
                imageView[i],
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

            writeDescriptorSets.push_back(vks::initializers::writeDescriptorSet(
                _descriptor_set_debug_view,
                VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                i + 1,
                &imageInfo[i]));
        };

        vkUpdateDescriptorSets(_device->logicalDevice, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, NULL);
    }

private:
    void AllocateResources(GBufferRenderPass& g_buffer_pass, ShadowRenderPass& shadow_pass, VkRenderPass render_pass, VkDescriptorImageInfo gi_buffer) {
        _deferred_cmd_buf = _device->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY);

        using namespace vks::initializers;

        _deferred_buffer = new vks::Framebuffer(_device);
        vks::AttachmentCreateInfo attachment_info = {};
        attachment_info.width = g_buffer_pass.GetWidth();
        attachment_info.height = g_buffer_pass.GetHeight();
        attachment_info.layerCount = 1;
        attachment_info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        attachment_info.format = VK_FORMAT_R16G16B16A16_SFLOAT;

        _deferred_buffer->width = g_buffer_pass.GetWidth();
        _deferred_buffer->height = g_buffer_pass.GetHeight();

        _deferred_buffer->addAttachment(attachment_info);
        VK_CHECK_RESULT(_deferred_buffer->createSampler(VK_FILTER_LINEAR, VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE));
        VK_CHECK_RESULT(_deferred_buffer->createRenderPass());

        assert(_resources);

        VkSemaphoreCreateInfo semaphore_create_info = semaphoreCreateInfo();
        VK_CHECK_RESULT(vkCreateSemaphore(_device->logicalDevice, &semaphore_create_info, nullptr, &_signal_finished));

        ShaderList& shader_list = _resources->GetShaderList();
        PipelineList& pipeline_list = _resources->GetPipepineList();
        PipelineLayoutList& pipelinelayout_list = _resources->GetPipepineLayoutList();
        DescriptionSetLayoutList& desc_set_layout_list = _resources->GetDescriptionSetLayoutList();
        BuffersList& buffers_list = _resources->GetBuffersList();
        DescriptionSetList& desc_set_list = _resources->GetDescriptionSetList();
        VertexDescriptionList& vertex_desc_list = _resources->GetVertexDescriptions();
        TextureList& texture_list = _resources->GetTextures();

        if (!desc_set_layout_list.Present(_pass_name_hash)) {
            std::vector<VkDescriptorSetLayoutBinding> set_layout_bindings = {
                // Binding 0: Vertex shader uniform buffer
                vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_GEOMETRY_BIT, 0),
                // Binding 1: Packed normals, roughness, metaliness texture
                vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                VK_SHADER_STAGE_FRAGMENT_BIT, 1),
                // Binding 2: Albedo texture
                vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                VK_SHADER_STAGE_FRAGMENT_BIT, 2),
                // Binding 3: Motion, roughness, metallic texture
                vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                VK_SHADER_STAGE_FRAGMENT_BIT, 3),
                // Binding 4: Fragment shader uniform buffer
                vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                VK_SHADER_STAGE_FRAGMENT_BIT, 4),
                // Binding 5: Shadow map 0
                vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                VK_SHADER_STAGE_FRAGMENT_BIT, 5),
                // Binding 6: Shadow map 1
                vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                VK_SHADER_STAGE_FRAGMENT_BIT, 6),
                // Binding 7: Shadow map 2
                vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                VK_SHADER_STAGE_FRAGMENT_BIT, 7),
                // Binding 8: GI texture
                vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                VK_SHADER_STAGE_FRAGMENT_BIT, 8),
                // Binding 9: Envcubemap texture
                vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                VK_SHADER_STAGE_FRAGMENT_BIT, 9),
                // Binding 10: BRDF lut texture
                vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                VK_SHADER_STAGE_FRAGMENT_BIT, 10),
                // Binding 11: Irradiance texture
                vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                VK_SHADER_STAGE_FRAGMENT_BIT, 11),
                // Binding 12: Prefiltered reflections texture
                vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                VK_SHADER_STAGE_FRAGMENT_BIT, 12),
                // Binding 13: Irradiance grid buffer
                vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                VK_SHADER_STAGE_FRAGMENT_BIT, 13)
            };

            VkDescriptorSetLayoutCreateInfo descriptor_layout = vks::initializers::descriptorSetLayoutCreateInfo(
                set_layout_bindings.data(),
                static_cast<uint32_t>(set_layout_bindings.size()));

            VkDescriptorSetLayout descriptor_set_layout;
            VK_CHECK_RESULT(vkCreateDescriptorSetLayout(_device->logicalDevice, &descriptor_layout, nullptr, &_descriptor_set_layout));

            VkPipelineLayoutCreateInfo pipeline_create_info = vks::initializers::pipelineLayoutCreateInfo(&_descriptor_set_layout, 1);

            VkPushConstantRange pushConstantRange = vks::initializers::pushConstantRange(VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(PushConst), 0);
            pipeline_create_info.pushConstantRangeCount = 1;
            pipeline_create_info.pPushConstantRanges = &pushConstantRange;

            VK_CHECK_RESULT(vkCreatePipelineLayout(_device->logicalDevice, &pipeline_create_info, nullptr, &_pipeline_layout));

            desc_set_layout_list.Set(_pass_name_hash, _descriptor_set_layout);
            pipelinelayout_list.Set(_pass_name_hash, _pipeline_layout);
        }

        if (!pipeline_list.Present(_pass_name_hash)) {
            std::array<VkPipelineShaderStageCreateInfo, 2> shader_stages = {
                shader_list.Load("shaders/deferred.vert.spv", VK_SHADER_STAGE_VERTEX_BIT),
                shader_list.Load("shaders/deferred.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT) };

            VkPipelineInputAssemblyStateCreateInfo input_assembly_state = pipelineInputAssemblyStateCreateInfo(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 0, VK_FALSE);
            VkPipelineRasterizationStateCreateInfo rasterization_state = pipelineRasterizationStateCreateInfo(VK_POLYGON_MODE_FILL, VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_CLOCKWISE, 0);
            VkPipelineColorBlendAttachmentState blend_attachment_state = vks::initializers::pipelineColorBlendAttachmentState(0xf, VK_FALSE);
            VkPipelineColorBlendStateCreateInfo color_blend_state = pipelineColorBlendStateCreateInfo(1, &blend_attachment_state);
            VkPipelineDepthStencilStateCreateInfo depth_stencil_state = pipelineDepthStencilStateCreateInfo(VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS_OR_EQUAL);
            VkPipelineViewportStateCreateInfo viewport_state = pipelineViewportStateCreateInfo(1, 1, 0);
            VkPipelineMultisampleStateCreateInfo multisample_state = pipelineMultisampleStateCreateInfo(VK_SAMPLE_COUNT_1_BIT, 0);

            std::vector<VkDynamicState> dynamic_state_enabled = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };

            VkPipelineLayout pipeline_layout = pipelinelayout_list.Get(_pass_name_hash);

            VkPipelineDynamicStateCreateInfo dynamic_state = pipelineDynamicStateCreateInfo(dynamic_state_enabled.data(), static_cast<uint32_t>(dynamic_state_enabled.size()), 0);
            VkGraphicsPipelineCreateInfo pipeline_create_info = pipelineCreateInfo(pipeline_layout, _deferred_buffer->renderPass, 0);

            VkPipelineVertexInputStateCreateInfo vertex_create_info = vertex_desc_list.Get(STATIC_CRC32("Vertex"));

            pipeline_create_info.stageCount = static_cast<uint32_t>(shader_stages.size());
            pipeline_create_info.pStages = shader_stages.data();
            pipeline_create_info.pVertexInputState = &vertex_create_info;
            pipeline_create_info.pInputAssemblyState = &input_assembly_state;
            pipeline_create_info.pRasterizationState = &rasterization_state;
            pipeline_create_info.pColorBlendState = &color_blend_state;
            pipeline_create_info.pMultisampleState = &multisample_state;
            pipeline_create_info.pViewportState = &viewport_state;
            pipeline_create_info.pDepthStencilState = &depth_stencil_state;
            pipeline_create_info.pDynamicState = &dynamic_state;

            VkPipeline pipeline;
            vkCreateGraphicsPipelines(_device->logicalDevice, pipeline_list.GetPipelineCache(), 1, &pipeline_create_info, nullptr, &pipeline);

            pipeline_list.Set(_pass_name_hash, pipeline);

            shader_stages[0] = shader_list.Load("shaders/debug.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
            shader_stages[1] = shader_list.Load("shaders/debug.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);

            VkPipeline debug_pipeline;
            vkCreateGraphicsPipelines(_device->logicalDevice, pipeline_list.GetPipelineCache(), 1, &pipeline_create_info, nullptr, &debug_pipeline);

            pipeline_list.Set(_debug_view_hash, debug_pipeline);
        }

        if (!desc_set_list.Present(_pass_name_hash)) {
            VkDescriptorSetLayout descriptor_set_layout = desc_set_layout_list.Get(_pass_name_hash);
            VkDescriptorSetAllocateInfo alloc_info = vks::initializers::descriptorSetAllocateInfo(desc_set_list.GetDescriptorPool(), &descriptor_set_layout, 1);

            VK_CHECK_RESULT(vkAllocateDescriptorSets(_device->logicalDevice, &alloc_info, &_descriptor_set));

            vks::Buffer lights_buffer = buffers_list.Get(STATIC_CRC32("Lights"));
            vks::Buffer fsq_buffer = buffers_list.Get(STATIC_CRC32("FullscreenQuad"));

            vks::Texture env_map_texture = texture_list.Get(STATIC_CRC32("EnvMap"));
            vks::Texture irradiance_map_texture = texture_list.Get(STATIC_CRC32("IrradianceCube"));
            vks::Texture prefiltered_map_texture = texture_list.Get(STATIC_CRC32("PrefilteredCube"));
            vks::Texture brdflut_map_texture = texture_list.Get(STATIC_CRC32("brdfLUT"));

            vks::Framebuffer* g_buffer = g_buffer_pass.GetFrameBuffer();

            VkSampler g_buffer_sampler = g_buffer->sampler;

            VkDescriptorImageInfo g_buffer_attachments[3] = {
                vks::initializers::descriptorImageInfo(g_buffer_sampler, g_buffer->attachments[0].view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
                vks::initializers::descriptorImageInfo(g_buffer_sampler, g_buffer->attachments[1].view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
                vks::initializers::descriptorImageInfo(g_buffer_sampler, g_buffer->attachments[2].view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) };


            VkDescriptorImageInfo shadow_buffer_attachments[3] = {
                vks::initializers::descriptorImageInfo(shadow_pass.GetFrameBuffer(0)->sampler, shadow_pass.GetFrameBuffer(0)->attachments[0].view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
                vks::initializers::descriptorImageInfo(shadow_pass.GetFrameBuffer(1)->sampler, shadow_pass.GetFrameBuffer(1)->attachments[0].view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
                vks::initializers::descriptorImageInfo(shadow_pass.GetFrameBuffer(2)->sampler, shadow_pass.GetFrameBuffer(2)->attachments[0].view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) };

            vks::Buffer sh9_buffer = buffers_list.Get(g_irradiance_grid_buffer_name);

            VkWriteDescriptorSet writeDescriptorSets[14] = {
                vks::initializers::writeDescriptorSet(_descriptor_set, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0, &fsq_buffer.descriptor),
                vks::initializers::writeDescriptorSet(_descriptor_set, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, &g_buffer_attachments[0]),
                vks::initializers::writeDescriptorSet(_descriptor_set, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2, &g_buffer_attachments[1]),
                vks::initializers::writeDescriptorSet(_descriptor_set, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 3, &g_buffer_attachments[2]),
                vks::initializers::writeDescriptorSet(_descriptor_set, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 4, &lights_buffer.descriptor),
                vks::initializers::writeDescriptorSet(_descriptor_set, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 5, &shadow_buffer_attachments[0]),
                vks::initializers::writeDescriptorSet(_descriptor_set, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 6, &shadow_buffer_attachments[1]),
                vks::initializers::writeDescriptorSet(_descriptor_set, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 7, &shadow_buffer_attachments[2]),
                vks::initializers::writeDescriptorSet(_descriptor_set, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 8, &gi_buffer),
                vks::initializers::writeDescriptorSet(_descriptor_set, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 9, &env_map_texture.descriptor),
                vks::initializers::writeDescriptorSet(_descriptor_set, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 10, &brdflut_map_texture.descriptor),
                vks::initializers::writeDescriptorSet(_descriptor_set, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 11, &irradiance_map_texture.descriptor),
                vks::initializers::writeDescriptorSet(_descriptor_set, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 12, &prefiltered_map_texture.descriptor),
                vks::initializers::writeDescriptorSet(_descriptor_set, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 13, &sh9_buffer.descriptor)
            };

            vkUpdateDescriptorSets(_device->logicalDevice, sizeof(writeDescriptorSets) / sizeof(VkWriteDescriptorSet), writeDescriptorSets, 0, NULL);

            desc_set_list.Set(_pass_name_hash, _descriptor_set);

            VkDescriptorSet descriptor_debug_view;
            VK_CHECK_RESULT(vkAllocateDescriptorSets(_device->logicalDevice, &alloc_info, &descriptor_debug_view));

            writeDescriptorSets[0] = {
                vks::initializers::writeDescriptorSet(descriptor_debug_view, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0, &fsq_buffer.descriptor)
            };

            vkUpdateDescriptorSets(_device->logicalDevice, 1, writeDescriptorSets, 0, NULL);

            desc_set_list.Set(_debug_view_hash, descriptor_debug_view);
        }

        VertexDescriptionList::Vertex vertices[4] = {
            { { 1.0f, 1.0f, 0.0f, 1.0f },{ 1.0f, 1.0f, 1.0f, 1.0f },{ 0.0f, 0.0f, 0.0f, 0.0f } },
        { { 0.0f, 1.0f, 0.0f, 0.0f },{ 1.0f, 1.0f, 1.0f, 1.0f },{ 0.0f, 0.0f, 0.0f, 0.0f } },
        { { 0.0f, 0.0f, 0.0f, 0.0f },{ 1.0f, 1.0f, 1.0f, 0.0f },{ 0.0f, 0.0f, 0.0f, 0.0f } },
        { { 1.0f, 0.0f, 0.0f, 1.0f },{ 1.0f, 1.0f, 1.0f, 0.0f },{ 0.0f, 0.0f, 0.0f, 0.0f } }
        };

        uint32_t indices[6] = { 0, 1, 2, 2, 3, 0 };

        VK_CHECK_RESULT(_device->createBuffer(VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            _fsq_index_count * sizeof(uint32_t), &_fsq_index_buffer.buffer, &_fsq_index_buffer.memory, indices));
        VK_CHECK_RESULT(_device->createBuffer(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            _fsq_vertex_count * sizeof(VertexDescriptionList::Vertex), &_fsq_vertex_buffer.buffer, &_fsq_vertex_buffer.memory, vertices));

        buffers_list.Set(STATIC_CRC32("FullScreenQuadVB"), _fsq_vertex_buffer);
        buffers_list.Set(STATIC_CRC32("FullScreenQuadIB"), _fsq_index_buffer);
    };

    void AllocateMLAAResources(GBufferRenderPass& g_buffer_pass, VkRenderPass render_pass) {
        using namespace vks::initializers;

        ShaderList& shader_list = _resources->GetShaderList();
        PipelineList& pipeline_list = _resources->GetPipepineList();
        PipelineLayoutList& pipelinelayout_list = _resources->GetPipepineLayoutList();
        DescriptionSetLayoutList& desc_set_layout_list = _resources->GetDescriptionSetLayoutList();
        VertexDescriptionList& vertex_desc_list = _resources->GetVertexDescriptions();
        BuffersList& buffers_list = _resources->GetBuffersList();
        DescriptionSetList& desc_set_list = _resources->GetDescriptionSetList();
        TextureList& texture_list = _resources->GetTextures();

        _edge_detect_cmd_buf = _device->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY);
        
        _edge_detect_buffer = new vks::Framebuffer(_device);

        _edge_detect_buffer->width = g_buffer_pass.GetWidth();
        _edge_detect_buffer->height = g_buffer_pass.GetHeight();

        vks::AttachmentCreateInfo attachment_info = {};
        attachment_info.width = g_buffer_pass.GetWidth();
        attachment_info.height = g_buffer_pass.GetHeight();
        attachment_info.layerCount = 1;
        attachment_info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        attachment_info.format = VK_FORMAT_R8G8_SNORM;

        _edge_detect_buffer->addAttachment(attachment_info);
        VK_CHECK_RESULT(_edge_detect_buffer->createSampler(VK_FILTER_LINEAR, VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE));
        VK_CHECK_RESULT(_edge_detect_buffer->createRenderPass());

        // Edge detection resources
        if (!desc_set_layout_list.Present(_edge_detect_pass_name_hash)) {
            std::vector<VkDescriptorSetLayoutBinding> set_layout_bindings = {
                // Binding 0: Vertex shader uniform buffer
                vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_GEOMETRY_BIT, 0),
                // Binding 1: Packed normals, roughness, metaliness texture
                vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                VK_SHADER_STAGE_FRAGMENT_BIT, 1),
                // Binding 4: Fragment shader uniform buffer
                vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                VK_SHADER_STAGE_FRAGMENT_BIT, 2)
            };

            VkDescriptorSetLayoutCreateInfo descriptor_layout = vks::initializers::descriptorSetLayoutCreateInfo(
                set_layout_bindings.data(),
                static_cast<uint32_t>(set_layout_bindings.size()));

            VkDescriptorSetLayout descriptor_set_layout;
            VK_CHECK_RESULT(vkCreateDescriptorSetLayout(_device->logicalDevice, &descriptor_layout, nullptr, &_edge_detect_descriptor_set_layout));

            VkPipelineLayoutCreateInfo pipeline_create_info = vks::initializers::pipelineLayoutCreateInfo(&_edge_detect_descriptor_set_layout, 1);
            VK_CHECK_RESULT(vkCreatePipelineLayout(_device->logicalDevice, &pipeline_create_info, nullptr, &_edge_detect_pipeline_layout));

            desc_set_layout_list.Set(_edge_detect_pass_name_hash, _edge_detect_descriptor_set_layout);
            pipelinelayout_list.Set(_edge_detect_pass_name_hash, _edge_detect_pipeline_layout);
        }

        if (!pipeline_list.Present(_edge_detect_pass_name_hash)) {
            std::array<VkPipelineShaderStageCreateInfo, 2> shader_stages = {
                shader_list.Load("shaders/edge_detection.vert.spv", VK_SHADER_STAGE_VERTEX_BIT),
                shader_list.Load("shaders/edge_detection.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT) };

            VkPipelineInputAssemblyStateCreateInfo input_assembly_state = pipelineInputAssemblyStateCreateInfo(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 0, VK_FALSE);
            VkPipelineRasterizationStateCreateInfo rasterization_state = pipelineRasterizationStateCreateInfo(VK_POLYGON_MODE_FILL, VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_CLOCKWISE, 0);
            VkPipelineColorBlendAttachmentState blend_attachment_state = vks::initializers::pipelineColorBlendAttachmentState(0xf, VK_FALSE);
            VkPipelineColorBlendStateCreateInfo color_blend_state = pipelineColorBlendStateCreateInfo(1, &blend_attachment_state);
            VkPipelineDepthStencilStateCreateInfo depth_stencil_state = pipelineDepthStencilStateCreateInfo(VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS_OR_EQUAL);
            VkPipelineViewportStateCreateInfo viewport_state = pipelineViewportStateCreateInfo(1, 1, 0);
            VkPipelineMultisampleStateCreateInfo multisample_state = pipelineMultisampleStateCreateInfo(VK_SAMPLE_COUNT_1_BIT, 0);

            std::vector<VkDynamicState> dynamic_state_enabled = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };

            VkPipelineLayout pipeline_layout = pipelinelayout_list.Get(_edge_detect_pass_name_hash);

            VkPipelineDynamicStateCreateInfo dynamic_state = pipelineDynamicStateCreateInfo(dynamic_state_enabled.data(), static_cast<uint32_t>(dynamic_state_enabled.size()), 0);
            
            VkGraphicsPipelineCreateInfo pipeline_create_info = pipelineCreateInfo(pipeline_layout, _edge_detect_buffer->renderPass, 0);

            VkPipelineVertexInputStateCreateInfo vertex_create_info = vertex_desc_list.Get(STATIC_CRC32("Vertex"));

            pipeline_create_info.stageCount = static_cast<uint32_t>(shader_stages.size());
            pipeline_create_info.pStages = shader_stages.data();
            pipeline_create_info.pVertexInputState = &vertex_create_info;
            pipeline_create_info.pInputAssemblyState = &input_assembly_state;
            pipeline_create_info.pRasterizationState = &rasterization_state;
            pipeline_create_info.pColorBlendState = &color_blend_state;
            pipeline_create_info.pMultisampleState = &multisample_state;
            pipeline_create_info.pViewportState = &viewport_state;
            pipeline_create_info.pDepthStencilState = &depth_stencil_state;
            pipeline_create_info.pDynamicState = &dynamic_state;

            vkCreateGraphicsPipelines(_device->logicalDevice, pipeline_list.GetPipelineCache(), 1, &pipeline_create_info, nullptr, &_edge_detect_pipeline);

            pipeline_list.Set(_edge_detect_pass_name_hash, _edge_detect_pipeline);
        } 

        if (!desc_set_list.Present(_edge_detect_pass_name_hash)) {
            VkDescriptorSetLayout descriptor_set_layout = desc_set_layout_list.Get(_edge_detect_pass_name_hash);
            VkDescriptorSetAllocateInfo alloc_info = vks::initializers::descriptorSetAllocateInfo(desc_set_list.GetDescriptorPool(), &descriptor_set_layout, 1);

            VK_CHECK_RESULT(vkAllocateDescriptorSets(_device->logicalDevice, &alloc_info, &_edge_detect_descriptor_set));

            vks::Buffer lights_buffer = buffers_list.Get(STATIC_CRC32("Lights"));
            vks::Buffer fsq_buffer = buffers_list.Get(STATIC_CRC32("FullscreenQuad"));

            vks::Framebuffer* g_buffer = g_buffer_pass.GetFrameBuffer();
            VkSampler g_buffer_sampler = g_buffer->sampler;

            VkDescriptorImageInfo g_buffer_attachments = descriptorImageInfo(g_buffer_sampler, g_buffer->attachments[0].view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

            VkWriteDescriptorSet writeDescriptorSets[3] = {
                vks::initializers::writeDescriptorSet(_edge_detect_descriptor_set, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0, &fsq_buffer.descriptor),
                vks::initializers::writeDescriptorSet(_edge_detect_descriptor_set, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, &g_buffer_attachments),
                vks::initializers::writeDescriptorSet(_edge_detect_descriptor_set, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 2, &lights_buffer.descriptor)
            };

            vkUpdateDescriptorSets(_device->logicalDevice, sizeof(writeDescriptorSets) / sizeof(VkWriteDescriptorSet), writeDescriptorSets, 0, NULL);

            desc_set_list.Set(_edge_detect_pass_name_hash, _edge_detect_descriptor_set);
        }

        // Weight calc resources
        _weight_buffer = new vks::Framebuffer(_device);

        _weight_buffer->width = g_buffer_pass.GetWidth();
        _weight_buffer->height = g_buffer_pass.GetHeight();

        attachment_info = {};
        attachment_info.width = g_buffer_pass.GetWidth();
        attachment_info.height = g_buffer_pass.GetHeight();
        attachment_info.layerCount = 1;
        attachment_info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        attachment_info.format = VK_FORMAT_R8G8B8A8_SNORM;

        _weight_buffer->addAttachment(attachment_info);
        VK_CHECK_RESULT(_weight_buffer->createSampler(VK_FILTER_NEAREST, VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE));
        VK_CHECK_RESULT(_weight_buffer->createRenderPass());

        _weight_cmd_buf = _device->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY);

        if (!desc_set_layout_list.Present(_weight_pass_name_hash)) {
            std::vector<VkDescriptorSetLayoutBinding> set_layout_bindings = {
                // Binding 0: Vertex shader uniform buffer
                vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_GEOMETRY_BIT, 0),
                // Binding 1: Edges texture
                vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                VK_SHADER_STAGE_FRAGMENT_BIT, 1),
                // Binding 2: Area map
                vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                VK_SHADER_STAGE_FRAGMENT_BIT, 2),
                // Binding 3: Fragment shader uniform buffer
                vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                VK_SHADER_STAGE_FRAGMENT_BIT, 3)
            };

            VkDescriptorSetLayoutCreateInfo descriptor_layout = vks::initializers::descriptorSetLayoutCreateInfo(
                set_layout_bindings.data(),
                static_cast<uint32_t>(set_layout_bindings.size()));

            VkDescriptorSetLayout descriptor_set_layout;
            VK_CHECK_RESULT(vkCreateDescriptorSetLayout(_device->logicalDevice, &descriptor_layout, nullptr, &_weight_descriptor_set_layout));

            VkPipelineLayoutCreateInfo pipeline_create_info = vks::initializers::pipelineLayoutCreateInfo(&_weight_descriptor_set_layout, 1);
            VK_CHECK_RESULT(vkCreatePipelineLayout(_device->logicalDevice, &pipeline_create_info, nullptr, &_weight_pipeline_layout));

            desc_set_layout_list.Set(_weight_pass_name_hash, _weight_descriptor_set_layout);
            pipelinelayout_list.Set(_weight_pass_name_hash, _weight_pipeline_layout);
        }

        if (!pipeline_list.Present(_weight_pass_name_hash)) {
            std::array<VkPipelineShaderStageCreateInfo, 2> shader_stages = {
                shader_list.Load("shaders/weight_calc.vert.spv", VK_SHADER_STAGE_VERTEX_BIT),
                shader_list.Load("shaders/weight_calc.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT) };

            VkPipelineInputAssemblyStateCreateInfo input_assembly_state = pipelineInputAssemblyStateCreateInfo(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 0, VK_FALSE);
            VkPipelineRasterizationStateCreateInfo rasterization_state = pipelineRasterizationStateCreateInfo(VK_POLYGON_MODE_FILL, VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_CLOCKWISE, 0);
            VkPipelineColorBlendAttachmentState blend_attachment_state = vks::initializers::pipelineColorBlendAttachmentState(0xf, VK_FALSE);
            VkPipelineColorBlendStateCreateInfo color_blend_state = pipelineColorBlendStateCreateInfo(1, &blend_attachment_state);
            VkPipelineDepthStencilStateCreateInfo depth_stencil_state = pipelineDepthStencilStateCreateInfo(VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS_OR_EQUAL);
            VkPipelineViewportStateCreateInfo viewport_state = pipelineViewportStateCreateInfo(1, 1, 0);
            VkPipelineMultisampleStateCreateInfo multisample_state = pipelineMultisampleStateCreateInfo(VK_SAMPLE_COUNT_1_BIT, 0);

            std::vector<VkDynamicState> dynamic_state_enabled = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };

            VkPipelineLayout pipeline_layout = pipelinelayout_list.Get(_weight_pass_name_hash);

            VkPipelineDynamicStateCreateInfo dynamic_state = pipelineDynamicStateCreateInfo(dynamic_state_enabled.data(), static_cast<uint32_t>(dynamic_state_enabled.size()), 0);
            // $tmp
            VkGraphicsPipelineCreateInfo pipeline_create_info = pipelineCreateInfo(pipeline_layout, _weight_buffer->renderPass, 0);

            VkPipelineVertexInputStateCreateInfo vertex_create_info = vertex_desc_list.Get(STATIC_CRC32("Vertex"));

            pipeline_create_info.stageCount = static_cast<uint32_t>(shader_stages.size());
            pipeline_create_info.pStages = shader_stages.data();
            pipeline_create_info.pVertexInputState = &vertex_create_info;
            pipeline_create_info.pInputAssemblyState = &input_assembly_state;
            pipeline_create_info.pRasterizationState = &rasterization_state;
            pipeline_create_info.pColorBlendState = &color_blend_state;
            pipeline_create_info.pMultisampleState = &multisample_state;
            pipeline_create_info.pViewportState = &viewport_state;
            pipeline_create_info.pDepthStencilState = &depth_stencil_state;
            pipeline_create_info.pDynamicState = &dynamic_state;

            vkCreateGraphicsPipelines(_device->logicalDevice, pipeline_list.GetPipelineCache(), 1, &pipeline_create_info, nullptr, &_weight_pipeline);

            pipeline_list.Set(_weight_pass_name_hash, _weight_pipeline);
        }

        if (!desc_set_list.Present(_weight_pass_name_hash)) {
            VkDescriptorSetLayout descriptor_set_layout = desc_set_layout_list.Get(_weight_pass_name_hash);
            VkDescriptorSetAllocateInfo alloc_info = vks::initializers::descriptorSetAllocateInfo(desc_set_list.GetDescriptorPool(), &descriptor_set_layout, 1);

            VK_CHECK_RESULT(vkAllocateDescriptorSets(_device->logicalDevice, &alloc_info, &_weight_descriptor_set));

            vks::Buffer lights_buffer = buffers_list.Get(STATIC_CRC32("Lights"));
            vks::Buffer fsq_buffer = buffers_list.Get(STATIC_CRC32("FullscreenQuad"));

            vks::Framebuffer* g_buffer = g_buffer_pass.GetFrameBuffer();
            VkSampler g_buffer_sampler = g_buffer->sampler;

            VkDescriptorImageInfo g_buffer_attachments = descriptorImageInfo(g_buffer_sampler, g_buffer->attachments[0].view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

            char area_map_path[MAX_PATH];
            sprintf_s(area_map_path, "%s/textures/%s", g_asset_path, "AreaMap33.dds");

            _area_map = texture_list.addTexture2D(STATIC_CRC32("AreaMap33"), area_map_path, VK_FORMAT_R8G8B8A8_SNORM);
            
            VkSamplerCreateInfo samplerCreateInfo = {};
            samplerCreateInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
            samplerCreateInfo.magFilter = VK_FILTER_NEAREST;
            samplerCreateInfo.minFilter = VK_FILTER_NEAREST;
            samplerCreateInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
            samplerCreateInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            samplerCreateInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            samplerCreateInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            samplerCreateInfo.mipLodBias = 0.0f;
            samplerCreateInfo.compareOp = VK_COMPARE_OP_NEVER;
            samplerCreateInfo.minLod = 0.0f;
            samplerCreateInfo.maxLod = 0.0f;
            // Only enable anisotropic filtering if enabled on the devicec
            samplerCreateInfo.maxAnisotropy = 1.0f;
            samplerCreateInfo.anisotropyEnable = VK_TRUE;
            samplerCreateInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
            VK_CHECK_RESULT(vkCreateSampler(_device->logicalDevice, &samplerCreateInfo, nullptr, &_area_map_sampler));

            vkDestroySampler(_device->logicalDevice, _area_map.sampler, nullptr);

            _area_map.sampler = _area_map_sampler;
            _area_map.updateDescriptor();

            VkDescriptorImageInfo edge_detect_attachment = descriptorImageInfo(_edge_detect_buffer->sampler, _edge_detect_buffer->attachments[0].view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

            VkWriteDescriptorSet writeDescriptorSets[4] = {
                vks::initializers::writeDescriptorSet(_weight_descriptor_set, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0, &fsq_buffer.descriptor),
                vks::initializers::writeDescriptorSet(_weight_descriptor_set, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, &edge_detect_attachment),
                vks::initializers::writeDescriptorSet(_weight_descriptor_set, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2, &_area_map.descriptor),
                vks::initializers::writeDescriptorSet(_weight_descriptor_set, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 3, &lights_buffer.descriptor)
            };
            
            vkUpdateDescriptorSets(_device->logicalDevice, sizeof(writeDescriptorSets) / sizeof(VkWriteDescriptorSet), writeDescriptorSets, 0, NULL);

            desc_set_list.Set(_weight_pass_name_hash, _weight_descriptor_set);
        }

        // Final blend resources
        if (!desc_set_layout_list.Present(_final_blend_pass_name_hash)) {
            std::vector<VkDescriptorSetLayoutBinding> set_layout_bindings = {
                // Binding 0: Vertex shader uniform buffer
                vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_GEOMETRY_BIT, 0),
                // Binding 1: Blend texture
                vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                VK_SHADER_STAGE_FRAGMENT_BIT, 1),
                // Binding 2: Color texture
                vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                VK_SHADER_STAGE_FRAGMENT_BIT, 2),
                // Binding 3: Fragment shader uniform buffer
                vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                VK_SHADER_STAGE_FRAGMENT_BIT, 3)
            };

            VkDescriptorSetLayoutCreateInfo descriptor_layout = vks::initializers::descriptorSetLayoutCreateInfo(
                set_layout_bindings.data(),
                static_cast<uint32_t>(set_layout_bindings.size()));

            VkDescriptorSetLayout descriptor_set_layout;
            VK_CHECK_RESULT(vkCreateDescriptorSetLayout(_device->logicalDevice, &descriptor_layout, nullptr, &_final_blend_descriptor_set_layout));

            VkPipelineLayoutCreateInfo pipeline_create_info = vks::initializers::pipelineLayoutCreateInfo(&_final_blend_descriptor_set_layout, 1);
            VK_CHECK_RESULT(vkCreatePipelineLayout(_device->logicalDevice, &pipeline_create_info, nullptr, &_final_blend_pipeline_layout));

            desc_set_layout_list.Set(_final_blend_pass_name_hash, _final_blend_descriptor_set_layout);
            pipelinelayout_list.Set(_final_blend_pass_name_hash, _final_blend_pipeline_layout);
        }

        if (!pipeline_list.Present(_final_blend_pass_name_hash)) {
            std::array<VkPipelineShaderStageCreateInfo, 2> shader_stages = {
                shader_list.Load("shaders/mlaa_final_blend.vert.spv", VK_SHADER_STAGE_VERTEX_BIT),
                shader_list.Load("shaders/mlaa_final_blend.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT) };

            VkPipelineInputAssemblyStateCreateInfo input_assembly_state = pipelineInputAssemblyStateCreateInfo(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 0, VK_FALSE);
            VkPipelineRasterizationStateCreateInfo rasterization_state = pipelineRasterizationStateCreateInfo(VK_POLYGON_MODE_FILL, VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_CLOCKWISE, 0);
            VkPipelineColorBlendAttachmentState blend_attachment_state = vks::initializers::pipelineColorBlendAttachmentState(0xf, VK_FALSE);
            VkPipelineColorBlendStateCreateInfo color_blend_state = pipelineColorBlendStateCreateInfo(1, &blend_attachment_state);
            VkPipelineDepthStencilStateCreateInfo depth_stencil_state = pipelineDepthStencilStateCreateInfo(VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS_OR_EQUAL);
            VkPipelineViewportStateCreateInfo viewport_state = pipelineViewportStateCreateInfo(1, 1, 0);
            VkPipelineMultisampleStateCreateInfo multisample_state = pipelineMultisampleStateCreateInfo(VK_SAMPLE_COUNT_1_BIT, 0);

            std::vector<VkDynamicState> dynamic_state_enabled = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };

            VkPipelineLayout pipeline_layout = pipelinelayout_list.Get(_weight_pass_name_hash);

            VkPipelineDynamicStateCreateInfo dynamic_state = pipelineDynamicStateCreateInfo(dynamic_state_enabled.data(), static_cast<uint32_t>(dynamic_state_enabled.size()), 0);
            VkGraphicsPipelineCreateInfo pipeline_create_info = pipelineCreateInfo(pipeline_layout, render_pass, 0);

            VkPipelineVertexInputStateCreateInfo vertex_create_info = vertex_desc_list.Get(STATIC_CRC32("Vertex"));

            pipeline_create_info.stageCount = static_cast<uint32_t>(shader_stages.size());
            pipeline_create_info.pStages = shader_stages.data();
            pipeline_create_info.pVertexInputState = &vertex_create_info;
            pipeline_create_info.pInputAssemblyState = &input_assembly_state;
            pipeline_create_info.pRasterizationState = &rasterization_state;
            pipeline_create_info.pColorBlendState = &color_blend_state;
            pipeline_create_info.pMultisampleState = &multisample_state;
            pipeline_create_info.pViewportState = &viewport_state;
            pipeline_create_info.pDepthStencilState = &depth_stencil_state;
            pipeline_create_info.pDynamicState = &dynamic_state;

            vkCreateGraphicsPipelines(_device->logicalDevice, pipeline_list.GetPipelineCache(), 1, &pipeline_create_info, nullptr, &_final_blend_pipeline);

            pipeline_list.Set(_final_blend_pass_name_hash, _final_blend_pipeline);
        }

        if (!desc_set_list.Present(_final_blend_pass_name_hash)) {
            VkDescriptorSetLayout descriptor_set_layout = desc_set_layout_list.Get(_final_blend_pass_name_hash);
            VkDescriptorSetAllocateInfo alloc_info = vks::initializers::descriptorSetAllocateInfo(desc_set_list.GetDescriptorPool(), &descriptor_set_layout, 1);

            VK_CHECK_RESULT(vkAllocateDescriptorSets(_device->logicalDevice, &alloc_info, &_final_blend_descriptor_set));

            vks::Buffer lights_buffer = buffers_list.Get(STATIC_CRC32("Lights"));
            vks::Buffer fsq_buffer = buffers_list.Get(STATIC_CRC32("FullscreenQuad"));

            VkDescriptorImageInfo weight_attachment = descriptorImageInfo(_weight_buffer->sampler, _weight_buffer->attachments[0].view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            VkDescriptorImageInfo deferred_attachment = descriptorImageInfo(_deferred_buffer->sampler, _deferred_buffer->attachments[0].view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

            VkWriteDescriptorSet writeDescriptorSets[4] = {
                vks::initializers::writeDescriptorSet(_final_blend_descriptor_set, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0, &fsq_buffer.descriptor),
                vks::initializers::writeDescriptorSet(_final_blend_descriptor_set, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, &weight_attachment),
                vks::initializers::writeDescriptorSet(_final_blend_descriptor_set, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2, &deferred_attachment),
                vks::initializers::writeDescriptorSet(_final_blend_descriptor_set, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 3, &lights_buffer.descriptor)
            };

            vkUpdateDescriptorSets(_device->logicalDevice, sizeof(writeDescriptorSets) / sizeof(VkWriteDescriptorSet), writeDescriptorSets, 0, NULL);

            desc_set_list.Set(_final_blend_pass_name_hash, _final_blend_descriptor_set);
        }
    }
protected:
    IrradianceGrid::Properties  _irradiance_grid_properties;

    VkDescriptorSetLayout       _descriptor_set_layout;
    VkPipelineLayout            _pipeline_layout;

    VkPipeline                  _pipeline;
    VkPipeline                  _pipeline_debug_view;

    VkDescriptorSet             _descriptor_set;
    VkDescriptorSet             _descriptor_set_debug_view;

    vks::Buffer                 _fsq_vertex_buffer;
    vks::Buffer                 _fsq_index_buffer;

    const char*                 _pass_name = "Deferred Pass";
    const uint32_t              _pass_name_hash = STATIC_CRC32(_pass_name);

    const char*                 _debug_view_name = "Debug view";
    const uint32_t              _debug_view_hash = STATIC_CRC32(_debug_view_name);

    const uint32_t              _fsq_vertex_count = 4;
    const uint32_t              _fsq_index_count = 6;
    const uint32_t              _max_debug_images = 3;

    uint32_t                    _buffer_width;
    uint32_t                    _buffer_height;
    uint32_t                    _num_debug_images;

    VkCommandBuffer             _deferred_cmd_buf;
    vks::Framebuffer*           _deferred_buffer;
    VkSemaphore                 _deferred_finished;
    // MLAA
    VkCommandBuffer             _edge_detect_cmd_buf;
    vks::Framebuffer*           _edge_detect_buffer;

    const char*                 _edge_detect_pass_name = "Edge Detect Pass";
    const uint32_t              _edge_detect_pass_name_hash = STATIC_CRC32(_edge_detect_pass_name);

    VkPipelineLayout            _edge_detect_pipeline_layout;
    VkPipeline                  _edge_detect_pipeline;
    VkDescriptorSetLayout       _edge_detect_descriptor_set_layout;
    VkDescriptorSet             _edge_detect_descriptor_set;
    VkSemaphore                 _edge_detect_finished;

    VkCommandBuffer             _weight_cmd_buf;
    vks::Framebuffer*           _weight_buffer;

    const char*                 _weight_pass_name = "Weight Pass";
    const uint32_t              _weight_pass_name_hash = STATIC_CRC32(_weight_pass_name);

    VkPipelineLayout            _weight_pipeline_layout;
    VkPipeline                  _weight_pipeline;
    VkDescriptorSetLayout       _weight_descriptor_set_layout;
    VkDescriptorSet             _weight_descriptor_set;

    vks::Texture                _area_map;
    VkSampler                   _area_map_sampler;

    VkSemaphore                 _weight_calc_finished;

    const char*                 _final_blend_pass_name = "Final Blend Pass";
    const uint32_t              _final_blend_pass_name_hash = STATIC_CRC32(_final_blend_pass_name);

    VkPipelineLayout            _final_blend_pipeline_layout;
    VkPipeline                  _final_blend_pipeline;
    VkDescriptorSetLayout       _final_blend_descriptor_set_layout;
    VkDescriptorSet             _final_blend_descriptor_set;

    ResourceManager*            _resources;
};