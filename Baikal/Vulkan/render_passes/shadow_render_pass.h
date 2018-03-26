#pragma once

#include <vector>

#include "vulkan/vulkan.h"

#include "VulkanFrameBuffer.hpp"

#include "render_pass.h"

#include "SceneGraph/vkscene.h"
#include "../resources/resource_manager.h"

#include "../utils/static_hash.h"

class ShadowRenderPass : public RenderPass
{
public:
    ShadowRenderPass(vks::VulkanDevice* device, Baikal::VkScene& scene) : RenderPass(device), _scene(&scene) {
        AllocateResources();
                
        _descriptor_set_layout = scene.resources->GetDescriptionSetLayoutList().Get(_pass_name_hash);
        _pipeline_layout = scene.resources->GetPipepineLayoutList().Get(_pass_name_hash);
        _pipeline = scene.resources->GetPipepineList().Get(_pass_name_hash);
    }

    ~ShadowRenderPass() {
        vkDestroySemaphore(_device->logicalDevice, _signal_finished, nullptr);
        _signal_finished = VK_NULL_HANDLE;

        for (int i = 0; i < LIGHT_COUNT; i++) {
            delete _framebuffer[i];
        }

        vkFreeCommandBuffers(_device->logicalDevice, _device->commandPool, LIGHT_COUNT, _cmd_buffers);
    }

    void BuildCommandBuffer(Baikal::VkScene const& scene) {
        VkClearValue clear_values[1];
        clear_values[0].depthStencil = { 1.0f, 0 };

        VkViewport viewport = vks::initializers::viewport((float)_framebuffer[0]->width, (float)_framebuffer[0]->height, 0.0f, 1.0f);
        VkRect2D scissor = vks::initializers::rect2D(_framebuffer[0]->width, _framebuffer[0]->height, 0, 0);

        for (int i = 0; i < LIGHT_COUNT; i++) {
            assert(_cmd_buffers[i] != VK_NULL_HANDLE);

            VkRenderPassBeginInfo render_pass_begin_info = vks::initializers::renderPassBeginInfo();
            render_pass_begin_info.renderPass = _framebuffer[i]->renderPass;
            render_pass_begin_info.framebuffer = _framebuffer[i]->framebuffer;
            render_pass_begin_info.renderArea.offset.x = 0;
            render_pass_begin_info.renderArea.offset.y = 0;
            render_pass_begin_info.renderArea.extent.width = _framebuffer[i]->width;
            render_pass_begin_info.renderArea.extent.height = _framebuffer[i]->height;
            render_pass_begin_info.clearValueCount = 2;
            render_pass_begin_info.pClearValues = clear_values;

            VkCommandBufferBeginInfo cmd_buf_info = vks::initializers::commandBufferBeginInfo();

            VK_CHECK_RESULT(vkBeginCommandBuffer(_cmd_buffers[i], &cmd_buf_info));

            //profiler->WriteTimestamp(commandBuffers.shadow[i], shadowPassQuery[i].first);
            
            vkCmdPushConstants(_cmd_buffers[i], _pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(int), &i);

            vkCmdSetViewport(_cmd_buffers[i], 0, 1, &viewport);
            vkCmdSetScissor(_cmd_buffers[i], 0, 1, &scissor);
            vkCmdSetDepthBias(_cmd_buffers[i], _depth_bias_constant, 0.0f, _depth_bias_slope);
            
            vkCmdBeginRenderPass(_cmd_buffers[i], &render_pass_begin_info, VK_SUBPASS_CONTENTS_INLINE);

            vkCmdBindPipeline(_cmd_buffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, _pipeline);
            vkCmdBindDescriptorSets(_cmd_buffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, _pipeline_layout, 0, 1, &_descriptor_set, 0, NULL);

            scene.WriteToCmdBuffer(_cmd_buffers[i], Baikal::VkScene::SCENE_DEPTH_ONLY);

            vkCmdEndRenderPass(_cmd_buffers[i]);
            
            //profiler->WriteTimestamp(commandBuffers.shadow[i], shadowPassQuery[i].second);

            VK_CHECK_RESULT(vkEndCommandBuffer(_cmd_buffers[i]));
        }
    }

    void Execute(VkQueue queue) {
        VkSubmitInfo submit_info = {};

        submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit_info.pSignalSemaphores = &_signal_finished;
        submit_info.signalSemaphoreCount = 1;
        submit_info.commandBufferCount = LIGHT_COUNT;
        submit_info.pCommandBuffers = _cmd_buffers;

        VK_CHECK_RESULT(vkQueueSubmit(queue, 1, &submit_info, VK_NULL_HANDLE));
    }

    inline vks::Framebuffer* GetFrameBuffer(uint32_t idx) { 
        if (idx >= LIGHT_COUNT) return nullptr;
        return _framebuffer[idx];
    }

    const char* GetName() { return _pass_name; };

private:
    void AllocateResources() {
        using namespace vks::initializers;

        assert(_scene->resources);

        ShaderList& shader_list = _scene->resources->GetShaderList();
        PipelineList& pipeline_list = _scene->resources->GetPipepineList();
        PipelineLayoutList& pipelinelayout_list = _scene->resources->GetPipepineLayoutList();
        DescriptionSetLayoutList& desc_set_layout_list = _scene->resources->GetDescriptionSetLayoutList();
        BuffersList& uniform_buffers_list = _scene->resources->GetBuffersList();
        DescriptionSetList& desc_set_list = _scene->resources->GetDescriptionSetList();
        VertexDescriptionList& vertex_desc_list = _scene->resources->GetVertexDescriptions();

        for (int i = 0; i < LIGHT_COUNT; i++) {
            _framebuffer[i] = new vks::Framebuffer(_device);

            _framebuffer[i]->width = _shadow_map_resolution;
            _framebuffer[i]->height = _shadow_map_resolution;

            vks::AttachmentCreateInfo attachment_info = {};
            attachment_info.format = VK_FORMAT_D32_SFLOAT_S8_UINT;
            attachment_info.width = _framebuffer[i]->width;
            attachment_info.height = _framebuffer[i]->height;
            attachment_info.layerCount = 1;
            attachment_info.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
            _framebuffer[i]->addAttachment(attachment_info);

            VK_CHECK_RESULT(_framebuffer[i]->createSampler(VK_FILTER_LINEAR, VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE));
            VK_CHECK_RESULT(_framebuffer[i]->createRenderPass());

            _cmd_buffers[i] = _device->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY);
        }

        VkSemaphoreCreateInfo semaphore_create_info = semaphoreCreateInfo();
        VK_CHECK_RESULT(vkCreateSemaphore(_device->logicalDevice, &semaphore_create_info, nullptr, &_signal_finished));

        if (!desc_set_layout_list.Present(_pass_name_hash)) {
            std::vector<VkDescriptorSetLayoutBinding> set_layout_bindings = {
                descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT, 0)
            };

            VkDescriptorSetLayoutCreateInfo descriptor_layout_info = descriptorSetLayoutCreateInfo(set_layout_bindings.data(),
                    static_cast<uint32_t>(set_layout_bindings.size()));

            VkDescriptorSetLayout descriptor_set_layout;
            VK_CHECK_RESULT(vkCreateDescriptorSetLayout(_device->logicalDevice, &descriptor_layout_info, nullptr, &descriptor_set_layout));

            desc_set_layout_list.Set(_pass_name_hash, descriptor_set_layout);
        }

        if (!pipelinelayout_list.Present(_pass_name_hash)) {
            VkDescriptorSetLayout descriptor_set_layout = desc_set_layout_list.Get(_pass_name_hash);

            VkPipelineLayoutCreateInfo pipeline_create_info = pipelineLayoutCreateInfo(&descriptor_set_layout, 1);

            VkPushConstantRange push_constant_range = pushConstantRange(VK_SHADER_STAGE_VERTEX_BIT, sizeof(int), 0);
            pipeline_create_info.pushConstantRangeCount = 1;
            pipeline_create_info.pPushConstantRanges = &push_constant_range;

            VkPipelineLayout pipeline_layout;
            VK_CHECK_RESULT(vkCreatePipelineLayout(_device->logicalDevice, &pipeline_create_info, nullptr, &pipeline_layout));

            pipelinelayout_list.Set(_pass_name_hash, pipeline_layout);
        }

        if (!pipeline_list.Present(_pass_name_hash)) {
            std::array<VkPipelineShaderStageCreateInfo, 2> shader_stages = {
                shader_list.Load("shaders/shadow.vert.spv", VK_SHADER_STAGE_VERTEX_BIT),
                shader_list.Load("shaders/shadow.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT) };

            VkPipelineInputAssemblyStateCreateInfo input_assembly_state = pipelineInputAssemblyStateCreateInfo(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 0, VK_FALSE);
            VkPipelineRasterizationStateCreateInfo rasterization_state  = pipelineRasterizationStateCreateInfo(VK_POLYGON_MODE_FILL, VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_CLOCKWISE, 0);
            VkPipelineColorBlendStateCreateInfo color_blend_state       = pipelineColorBlendStateCreateInfo(0, nullptr);
            VkPipelineDepthStencilStateCreateInfo depth_stencil_state   = pipelineDepthStencilStateCreateInfo(VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS_OR_EQUAL);
            VkPipelineViewportStateCreateInfo viewport_state            = pipelineViewportStateCreateInfo(1, 1, 0);
            VkPipelineMultisampleStateCreateInfo multisample_state      = pipelineMultisampleStateCreateInfo(VK_SAMPLE_COUNT_1_BIT, 0);
            
            rasterization_state.depthBiasEnable = true;

            std::vector<VkDynamicState> dynamic_state_enabled = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR,  VK_DYNAMIC_STATE_DEPTH_BIAS };
            
            VkPipelineLayout pipeline_layout = pipelinelayout_list.Get(_pass_name_hash);

            VkPipelineDynamicStateCreateInfo dynamic_state = pipelineDynamicStateCreateInfo(dynamic_state_enabled.data(), static_cast<uint32_t>(dynamic_state_enabled.size()), 0);
            VkGraphicsPipelineCreateInfo pipeline_create_info = pipelineCreateInfo(pipeline_layout, _framebuffer[0]->renderPass, 0);
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

        if (!desc_set_list.Present(_pass_name_hash))
        {
            VkDescriptorSetLayout descriptor_set_layout = desc_set_layout_list.Get(_pass_name_hash);
            VkDescriptorSetAllocateInfo alloc_info = vks::initializers::descriptorSetAllocateInfo(desc_set_list.GetDescriptorPool(), &descriptor_set_layout, 1);

            VK_CHECK_RESULT(vkAllocateDescriptorSets(_device->logicalDevice, &alloc_info, &_descriptor_set));

            vks::Buffer buffer = uniform_buffers_list.Get(STATIC_CRC32("Lights"));

            VkWriteDescriptorSet writeDescriptorSets[1] = {
                vks::initializers::writeDescriptorSet(_descriptor_set, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0, &buffer.descriptor)
            };

            vkUpdateDescriptorSets(_device->logicalDevice, 1, writeDescriptorSets, 0, NULL);

            desc_set_list.Set(_pass_name_hash, _descriptor_set);
        }
    }
protected:
    vks::Framebuffer*       _framebuffer[LIGHT_COUNT];
    VkCommandBuffer         _cmd_buffers[LIGHT_COUNT];

    VkPipelineLayout        _pipeline_layout;
    VkDescriptorSetLayout   _descriptor_set_layout;

    VkPipeline              _pipeline;
    VkDescriptorSet         _descriptor_set;

    const uint32_t          _shadow_map_resolution = 1024;
    const float             _depth_bias_constant = 25.0f;
    const float             _depth_bias_slope = 25.0f;

    const char*             _pass_name = "Shadow Pass";
    const uint32_t          _pass_name_hash = STATIC_CRC32(_pass_name);
    Baikal::VkScene*        _scene;
};