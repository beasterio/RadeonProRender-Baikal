#pragma once

#include <vector>

#include "vulkan/vulkan.h"

#include "VulkanFrameBuffer.hpp"

#include "render_pass.h"

#include "SceneGraph/vkscene.h"
#include "../resources/resource_manager.h"

#include "../utils/static_hash.h"

class GBufferRenderPass : public RenderPass
{
public:
    GBufferRenderPass(vks::VulkanDevice* device, Baikal::VkScene const& scene, uint32_t width, uint32_t height) : RenderPass(device)
        , _buffer_width(width)
        , _buffer_height(height) {
        AllocateResources(scene);

        _pipeline = scene.resources->GetPipepineList().Get(_pass_name_hash);
        
        BuildCommandBuffer(scene);
    }

    ~GBufferRenderPass() {
        vkDestroySemaphore(_device->logicalDevice, _signal_finished, nullptr);
        _signal_finished = VK_NULL_HANDLE;

        delete _gbuffer;
        vkFreeCommandBuffers(_device->logicalDevice, _device->commandPool, 1, &_cmd_buffer);
    }

    void BuildCommandBuffer(Baikal::VkScene const& scene) {
        VkCommandBufferBeginInfo cmd_buf_info = vks::initializers::commandBufferBeginInfo();

        vkBeginCommandBuffer(_cmd_buffer, &cmd_buf_info);

        VkClearValue clear_values[4] = {
            { 0.0f, 0.0f, 0.0f, 0.0f }, // color
            { 0.0f, 0.0f, 0.0f, 0.0f }, // color
            { 0.0f, 0.0f, 0.0f, 0.0f }, // color
            { 1.0f, 0 }                 // depth stencil
        };

        VkRenderPassBeginInfo render_pass_begin_info = vks::initializers::renderPassBeginInfo();
        render_pass_begin_info.renderPass = _gbuffer->renderPass;
        render_pass_begin_info.framebuffer = _gbuffer->framebuffer;
        render_pass_begin_info.renderArea.extent.width = _gbuffer->width;
        render_pass_begin_info.renderArea.extent.height = _gbuffer->height;
        render_pass_begin_info.clearValueCount = sizeof(clear_values) / sizeof(VkClearValue);
        render_pass_begin_info.pClearValues = clear_values;

        vkCmdBeginRenderPass(_cmd_buffer, &render_pass_begin_info, VK_SUBPASS_CONTENTS_INLINE);
        
        VkViewport viewport = vks::initializers::viewport((float)_gbuffer->width, (float)_gbuffer->height, 0.0f, 1.0f);
        vkCmdSetViewport(_cmd_buffer, 0, 1, &viewport);

        VkRect2D scissor = vks::initializers::rect2D(_gbuffer->width, _gbuffer->height, 0, 0);
        vkCmdSetScissor(_cmd_buffer, 0, 1, &scissor);

        vkCmdBindPipeline(_cmd_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, _pipeline);
        scene.WriteToCmdBuffer(_cmd_buffer);
        vkCmdEndRenderPass(_cmd_buffer);

        VK_CHECK_RESULT(vkEndCommandBuffer(_cmd_buffer));
    }

    void Execute(VkQueue queue) {
        VkSubmitInfo submit_info = {};

        submit_info.pWaitDstStageMask = &_stage_wait_flags[0];
        submit_info.pWaitSemaphores = &_dependencies[0];
        submit_info.waitSemaphoreCount = _dependencies.size();
        submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit_info.pSignalSemaphores = &_signal_finished;
        submit_info.signalSemaphoreCount = 1;
        submit_info.commandBufferCount = 1;
        submit_info.pCommandBuffers = &_cmd_buffer;

        VK_CHECK_RESULT(vkQueueSubmit(queue, 1, &submit_info, VK_NULL_HANDLE));
    }

    inline vks::Framebuffer* GetFrameBuffer() { 
        return _gbuffer;
    }

    const char* GetName() { return _pass_name; };

    inline uint32_t GetWidth() { return _buffer_width; }
    inline uint32_t GetHeight() { return _buffer_height; }

private:
    void AllocateResources(Baikal::VkScene const& scene) {
        using namespace vks::initializers;

        assert(scene.resources);

        VkSemaphoreCreateInfo semaphore_create_info = semaphoreCreateInfo();
        VK_CHECK_RESULT(vkCreateSemaphore(_device->logicalDevice, &semaphore_create_info, nullptr, &_signal_finished));

        ShaderList& shader_list = scene.resources->GetShaderList();
        PipelineList& pipeline_list = scene.resources->GetPipepineList();
        PipelineLayoutList& pipelinelayout_list = scene.resources->GetPipepineLayoutList();
        DescriptionSetLayoutList& desc_set_layout_list = scene.resources->GetDescriptionSetLayoutList();
        BuffersList& uniform_buffers_list = scene.resources->GetBuffersList();
        DescriptionSetList& desc_set_list = scene.resources->GetDescriptionSetList();
        VertexDescriptionList& vertex_desc_list = scene.resources->GetVertexDescriptions();

        _gbuffer = new vks::Framebuffer(_device);

        _gbuffer->width = _buffer_width;
        _gbuffer->height = _buffer_height;

        // Attachments (3 color, 1 depth)
        vks::AttachmentCreateInfo attachment_info = {};
        attachment_info.width = _buffer_width;
        attachment_info.height = _buffer_height;
        attachment_info.layerCount = 1;
        attachment_info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

        // Color attachments
        // Attachment 0: Packed view space normals (RG16F), depth - bit z/w, mesh id - last 8 bits
        attachment_info.format = VK_FORMAT_R16G16B16A16_UINT;
        _gbuffer->addAttachment(attachment_info);

        // Attachment 1: Albedo
        attachment_info.format = VK_FORMAT_R8G8B8A8_UNORM;
        _gbuffer->addAttachment(attachment_info);

        // Attachment 2: Motion, roughness, metaliness
        attachment_info.format = VK_FORMAT_R8G8B8A8_UNORM;
        _gbuffer->addAttachment(attachment_info);

        // Depth attachment
        // Find a suitable depth format
        VkFormat attDepthFormat;
        VkBool32 validDepthFormat = vks::tools::getSupportedDepthFormat(_device->physicalDevice, &attDepthFormat);
        assert(validDepthFormat);

        attachment_info.format = attDepthFormat;
        attachment_info.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        _gbuffer->addAttachment(attachment_info);

        // Create sampler to sample from the color attachments
        VK_CHECK_RESULT(_gbuffer->createSampler(VK_FILTER_NEAREST, VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE));

        // Create default renderpass for the framebuffer
        VK_CHECK_RESULT(_gbuffer->createRenderPass());

        _cmd_buffer = _device->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY);

        if (!pipeline_list.Present(_pass_name_hash)) {
            std::array<VkPipelineShaderStageCreateInfo, 2> shader_stages = {
                shader_list.Load("shaders/mrt.vert.spv", VK_SHADER_STAGE_VERTEX_BIT),
                shader_list.Load("shaders/mrt.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT) };

            VkPipelineInputAssemblyStateCreateInfo input_assembly_state = pipelineInputAssemblyStateCreateInfo(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 0, VK_FALSE);
            VkPipelineRasterizationStateCreateInfo rasterization_state  = pipelineRasterizationStateCreateInfo(VK_POLYGON_MODE_FILL, VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_CLOCKWISE, 0);
            VkPipelineColorBlendStateCreateInfo color_blend_state       = pipelineColorBlendStateCreateInfo(0, nullptr);
            VkPipelineDepthStencilStateCreateInfo depth_stencil_state   = pipelineDepthStencilStateCreateInfo(VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS_OR_EQUAL);
            VkPipelineViewportStateCreateInfo viewport_state            = pipelineViewportStateCreateInfo(1, 1, 0);
            VkPipelineMultisampleStateCreateInfo multisample_state      = pipelineMultisampleStateCreateInfo(VK_SAMPLE_COUNT_1_BIT, 0);

            std::array<VkDynamicState, 2> dynamic_state_enabled = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };

            std::array<VkPipelineColorBlendAttachmentState, 3> blend_attachments_states = {
                vks::initializers::pipelineColorBlendAttachmentState(0xf, VK_FALSE),
                vks::initializers::pipelineColorBlendAttachmentState(0xf, VK_FALSE),
                vks::initializers::pipelineColorBlendAttachmentState(0xf, VK_FALSE)
            };

            color_blend_state.attachmentCount = static_cast<uint32_t>(blend_attachments_states.size());
            color_blend_state.pAttachments = blend_attachments_states.data();

            VkPipelineLayout pipeline_layout = scene.pipelineLayout;

            VkPipelineDynamicStateCreateInfo dynamic_state = pipelineDynamicStateCreateInfo(dynamic_state_enabled.data(), static_cast<uint32_t>(dynamic_state_enabled.size()), 0);
            VkGraphicsPipelineCreateInfo pipeline_create_info = pipelineCreateInfo(pipeline_layout, _gbuffer->renderPass, 0);
            
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
        }
    }
protected:
    vks::Framebuffer*       _gbuffer;
    VkCommandBuffer         _cmd_buffer;

    VkPipeline              _pipeline;

    const char*             _pass_name = "GBuffer Pass";
    const uint32_t          _pass_name_hash = STATIC_CRC32(_pass_name);

    uint32_t                _buffer_width;
    uint32_t                _buffer_height;
};