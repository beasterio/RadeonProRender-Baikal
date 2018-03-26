#pragma once

#include "VulkanTexture.hpp"
#include "VulkanModel.hpp"
#include "VulkanFrameBuffer.hpp"
#include "VulkanInitializers.hpp"

#include "../resources/resource_manager.h"
#include "SceneGraph/vkscene.h"

#include <vector>

class CubemapRender
{
public:
    CubemapRender(vks::VulkanDevice* device, VkQueue queue, Baikal::VkScene& scene)
        : _device(device)
        , _queue(queue) {
        using namespace vks::initializers;

        VertexDescriptionList& vertex_desc_list = scene.resources->GetVertexDescriptions();
        PipelineList& pipeline_list = scene.resources->GetPipepineList();
        PipelineLayoutList& pipeline_layout_list = scene.resources->GetPipepineLayoutList();
        DescriptionSetList& desc_set_list = scene.resources->GetDescriptionSetList();
        DescriptionSetLayoutList& desc_set_layout_list = scene.resources->GetDescriptionSetLayoutList();
        TextureList& texture_list = scene.resources->GetTextures();
        BuffersList& buffers_list = scene.resources->GetBuffersList();
        ShaderList& shader_list = scene.resources->GetShaderList();

        for (int i = 0; i < 6; i++)
        {
            _render_to_cubemap_cmd_buf[i] = _device->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY);

            _framebuffer[i] = new vks::Framebuffer(_device);

            _framebuffer[i]->width = _cubemap_face_size;
            _framebuffer[i]->height = _cubemap_face_size;

            // Attachments (1 color, 1 depth)
            vks::AttachmentCreateInfo attachment_info = {};
            attachment_info.width = _cubemap_face_size;
            attachment_info.height = _cubemap_face_size;
            attachment_info.layerCount = 1;
            attachment_info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

            attachment_info.format = VK_FORMAT_R16G16B16A16_SFLOAT;
            _framebuffer[i]->addAttachment(attachment_info);
        
            VkFormat attDepthFormat;
            VkBool32 validDepthFormat = vks::tools::getSupportedDepthFormat(_device->physicalDevice, &attDepthFormat);
            assert(validDepthFormat);

            attachment_info.format = attDepthFormat;
            attachment_info.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
            _framebuffer[i]->addAttachment(attachment_info);

            VK_CHECK_RESULT(_framebuffer[i]->createRenderPass());
            VK_CHECK_RESULT(_framebuffer[i]->createSampler(VK_FILTER_LINEAR, VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE));
        }

        std::array<VkPipelineShaderStageCreateInfo, 2> shader_stages = {
            shader_list.Load("shaders/cubemap_render.vert.spv", VK_SHADER_STAGE_VERTEX_BIT),
            shader_list.Load("shaders/cubemap_render.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT) };

        VkPipelineInputAssemblyStateCreateInfo input_assembly_state = pipelineInputAssemblyStateCreateInfo(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 0, VK_FALSE);
        VkPipelineRasterizationStateCreateInfo rasterization_state = pipelineRasterizationStateCreateInfo(VK_POLYGON_MODE_FILL, VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_CLOCKWISE, 0);
        VkPipelineColorBlendStateCreateInfo color_blend_state = pipelineColorBlendStateCreateInfo(0, nullptr);
        VkPipelineDepthStencilStateCreateInfo depth_stencil_state = pipelineDepthStencilStateCreateInfo(VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS_OR_EQUAL);
        VkPipelineViewportStateCreateInfo viewport_state = pipelineViewportStateCreateInfo(1, 1, 0);
        VkPipelineMultisampleStateCreateInfo multisample_state = pipelineMultisampleStateCreateInfo(VK_SAMPLE_COUNT_1_BIT, 0);

        std::array<VkDynamicState, 2> dynamic_state_enabled = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };

        std::array<VkPipelineColorBlendAttachmentState, 1> blend_attachments_states = {
            vks::initializers::pipelineColorBlendAttachmentState(0xf, VK_FALSE)
        };

        color_blend_state.attachmentCount = static_cast<uint32_t>(blend_attachments_states.size());
        color_blend_state.pAttachments = blend_attachments_states.data();

        VkPipelineLayout pipeline_layout = scene.cubemapPipelineLayout;

        VkPipelineDynamicStateCreateInfo dynamic_state = pipelineDynamicStateCreateInfo(dynamic_state_enabled.data(), static_cast<uint32_t>(dynamic_state_enabled.size()), 0);
        VkGraphicsPipelineCreateInfo pipeline_create_info = pipelineCreateInfo(pipeline_layout, _framebuffer[0]->renderPass, 0);
        VkPipelineVertexInputStateCreateInfo vertex_desc = vertex_desc_list.Get(STATIC_CRC32("Vertex"));

        pipeline_create_info.stageCount = static_cast<uint32_t>(shader_stages.size());
        pipeline_create_info.pStages = shader_stages.data();
        pipeline_create_info.pVertexInputState = &vertex_desc;
        pipeline_create_info.pInputAssemblyState = &input_assembly_state;
        pipeline_create_info.pRasterizationState = &rasterization_state;
        pipeline_create_info.pColorBlendState = &color_blend_state;
        pipeline_create_info.pMultisampleState = &multisample_state;
        pipeline_create_info.pViewportState = &viewport_state;
        pipeline_create_info.pDepthStencilState = &depth_stencil_state;
        pipeline_create_info.pDynamicState = &dynamic_state;

        vkCreateGraphicsPipelines(_device->logicalDevice, pipeline_list.GetPipelineCache(), 1, &pipeline_create_info, nullptr, &_pipeline);

        pipeline_list.Set(_scene_forward_render_name, _pipeline);

        VkSemaphoreCreateInfo semaphore_create_info = semaphoreCreateInfo();
        
        _semaphores.resize(6);
        for (int i = 0 ; i < 6; i++)
            VK_CHECK_RESULT(vkCreateSemaphore(_device->logicalDevice, &semaphore_create_info, nullptr, &_semaphores[i]));

        // Sky rendering resources
        {
            std::vector<VkDescriptorSetLayoutBinding> set_layout_bindings = {
                vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT, 0),
                vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT, 1),
                vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 2)
            };

            VkDescriptorSetLayoutCreateInfo descriptor_layout = vks::initializers::descriptorSetLayoutCreateInfo(
                set_layout_bindings.data(),
                static_cast<uint32_t>(set_layout_bindings.size()));

            VK_CHECK_RESULT(vkCreateDescriptorSetLayout(_device->logicalDevice, &descriptor_layout, nullptr, &_sky_descriptor_set_layout));

            desc_set_layout_list.Set(_sky_forward_render_name, _sky_descriptor_set_layout);

            VkPipelineLayoutCreateInfo pipeline_layout_create_info = vks::initializers::pipelineLayoutCreateInfo(&_sky_descriptor_set_layout, 1);

            VkPushConstantRange pushConstantRange = vks::initializers::pushConstantRange(VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(int), 0);
            pipeline_layout_create_info.pushConstantRangeCount = 1;
            pipeline_layout_create_info.pPushConstantRanges = &pushConstantRange;

            VK_CHECK_RESULT(vkCreatePipelineLayout(_device->logicalDevice, &pipeline_layout_create_info, nullptr, &_sky_pipeline_layout));
            pipeline_layout_list.Set(_sky_forward_render_name, _sky_pipeline_layout);

            std::array<VkPipelineShaderStageCreateInfo, 2> sky_shader_stages = {
                shader_list.Load("shaders/cubemap_sky.vert.spv", VK_SHADER_STAGE_VERTEX_BIT),
                shader_list.Load("shaders/cubemap_sky.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT) };

            VkPipelineInputAssemblyStateCreateInfo input_assembly_state = pipelineInputAssemblyStateCreateInfo(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 0, VK_FALSE);
            VkPipelineRasterizationStateCreateInfo rasterization_state = pipelineRasterizationStateCreateInfo(VK_POLYGON_MODE_FILL, VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_CLOCKWISE, 0);
            VkPipelineColorBlendAttachmentState blend_attachment_state = vks::initializers::pipelineColorBlendAttachmentState(0xf, VK_FALSE);
            VkPipelineColorBlendStateCreateInfo color_blend_state = pipelineColorBlendStateCreateInfo(1, &blend_attachment_state);
            VkPipelineDepthStencilStateCreateInfo depth_stencil_state = pipelineDepthStencilStateCreateInfo(VK_FALSE, VK_FALSE, VK_COMPARE_OP_ALWAYS);
            VkPipelineViewportStateCreateInfo viewport_state = pipelineViewportStateCreateInfo(1, 1, 0);
            VkPipelineMultisampleStateCreateInfo multisample_state = pipelineMultisampleStateCreateInfo(VK_SAMPLE_COUNT_1_BIT, 0);

            std::vector<VkDynamicState> dynamic_state_enabled = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };

            VkPipelineDynamicStateCreateInfo dynamic_state = pipelineDynamicStateCreateInfo(dynamic_state_enabled.data(), static_cast<uint32_t>(dynamic_state_enabled.size()), 0);
            VkGraphicsPipelineCreateInfo pipeline_create_info = pipelineCreateInfo(_sky_pipeline_layout, _framebuffer[0]->renderPass, 0);

            VkPipelineVertexInputStateCreateInfo vertex_create_info = vertex_desc_list.Get(STATIC_CRC32("Vertex"));

            pipeline_create_info.stageCount = static_cast<uint32_t>(sky_shader_stages.size());
            pipeline_create_info.pStages = sky_shader_stages.data();
            pipeline_create_info.pVertexInputState = &vertex_create_info;
            pipeline_create_info.pInputAssemblyState = &input_assembly_state;
            pipeline_create_info.pRasterizationState = &rasterization_state;
            pipeline_create_info.pColorBlendState = &color_blend_state;
            pipeline_create_info.pMultisampleState = &multisample_state;
            pipeline_create_info.pViewportState = &viewport_state;
            pipeline_create_info.pDepthStencilState = &depth_stencil_state;
            pipeline_create_info.pDynamicState = &dynamic_state;

            vkCreateGraphicsPipelines(_device->logicalDevice, pipeline_list.GetPipelineCache(), 1, &pipeline_create_info, nullptr, &_sky_pipeline);
            pipeline_list.Set(_sky_forward_render_name, _sky_pipeline);

            VkDescriptorSetAllocateInfo alloc_info = vks::initializers::descriptorSetAllocateInfo(desc_set_list.GetDescriptorPool(), &_sky_descriptor_set_layout, 1);
            VK_CHECK_RESULT(vkAllocateDescriptorSets(_device->logicalDevice, &alloc_info, &_sky_descriptor_set));
            
            vks::Buffer scene_to_cube_buffer = buffers_list.Get(STATIC_CRC32("SceneToCube"));
            vks::Buffer fsq_ubo_buffer = buffers_list.Get(STATIC_CRC32("FullscreenQuad"));
            vks::Texture env_map_texture = texture_list.Get(STATIC_CRC32("EnvMap"));

            VkWriteDescriptorSet writeDescriptorSets[3] = {
                vks::initializers::writeDescriptorSet(_sky_descriptor_set, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0, &fsq_ubo_buffer.descriptor),
                vks::initializers::writeDescriptorSet(_sky_descriptor_set, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, &scene_to_cube_buffer.descriptor),
                vks::initializers::writeDescriptorSet(_sky_descriptor_set, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2, &env_map_texture.descriptor)
            };

            vkUpdateDescriptorSets(_device->logicalDevice, sizeof(writeDescriptorSets) / sizeof(VkWriteDescriptorSet), writeDescriptorSets, 0, NULL);
        }

        std::vector<glm::mat4> view_matrices = {
            glm::rotate(glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(0.0f, 1.0f, 0.0f)), glm::radians(180.0f), glm::vec3(1.0f, 0.0f, 0.0f)),
            glm::rotate(glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f), glm::vec3(0.0f, 1.0f, 0.0f)), glm::radians(180.0f), glm::vec3(1.0f, 0.0f, 0.0f)),
            glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f), glm::vec3(1.0f, 0.0f, 0.0f)),
            glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f)),
            glm::rotate(glm::mat4(1.0f), glm::radians(180.0f), glm::vec3(1.0f, 0.0f, 0.0f)),
            glm::rotate(glm::mat4(1.0f), glm::radians(180.0f), glm::vec3(0.0f, 0.0f, 1.0f)),
        };
      
        _ubo.projection = glm::perspective((float)(M_PI / 2.0), 1.0f, 0.1f, 512.0f);
        for (int i = 0; i < 6; i++) _ubo.view[i] = view_matrices[i];
   }

    ~CubemapRender() {
        for (int i = 0; i < 6; i++) {
            vkDestroySemaphore(_device->logicalDevice, _semaphores[i], nullptr);
            delete _framebuffer[i];
        }

        vkFreeCommandBuffers(_device->logicalDevice, _device->commandPool, 6, &_render_to_cubemap_cmd_buf[0]);
    }

    vks::TextureCubeMap CreateCubemap(uint32_t size, VkFormat format, bool hasMips = false) {
        vks::TextureCubeMap cubemap;

        const uint32_t num_mips = hasMips ? static_cast<uint32_t>(floor(log2(size))) + 1 : 1;

        VkImageCreateInfo image_create_info = vks::initializers::imageCreateInfo();
        image_create_info.imageType = VK_IMAGE_TYPE_2D;
        image_create_info.format = format;
        image_create_info.extent.width = size;
        image_create_info.extent.height = size;
        image_create_info.extent.depth = 1;
        image_create_info.mipLevels = num_mips;
        image_create_info.arrayLayers = 6;
        image_create_info.samples = VK_SAMPLE_COUNT_1_BIT;
        image_create_info.tiling = VK_IMAGE_TILING_OPTIMAL;
        image_create_info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
        image_create_info.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
        VK_CHECK_RESULT(vkCreateImage(_device->logicalDevice, &image_create_info, nullptr, &cubemap.image));

        VkMemoryAllocateInfo memAlloc = vks::initializers::memoryAllocateInfo();
        VkMemoryRequirements memReqs;
        vkGetImageMemoryRequirements(_device->logicalDevice, cubemap.image, &memReqs);
        memAlloc.allocationSize = memReqs.size;
        memAlloc.memoryTypeIndex = _device->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        VK_CHECK_RESULT(vkAllocateMemory(_device->logicalDevice, &memAlloc, nullptr, &cubemap.deviceMemory));
        VK_CHECK_RESULT(vkBindImageMemory(_device->logicalDevice, cubemap.image, cubemap.deviceMemory, 0));

        VkImageViewCreateInfo view_create_info = vks::initializers::imageViewCreateInfo();
        view_create_info.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
        view_create_info.format = format;
        view_create_info.subresourceRange = {};
        view_create_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        view_create_info.subresourceRange.levelCount = num_mips;
        view_create_info.subresourceRange.layerCount = 6;
        view_create_info.image = cubemap.image;
        VK_CHECK_RESULT(vkCreateImageView(_device->logicalDevice, &view_create_info, nullptr, &cubemap.view));

        VkSamplerCreateInfo sampler_create_info = vks::initializers::samplerCreateInfo();
        sampler_create_info.magFilter = VK_FILTER_LINEAR;
        sampler_create_info.minFilter = VK_FILTER_LINEAR;
        sampler_create_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        sampler_create_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler_create_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler_create_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler_create_info.minLod = 0.0f;
        sampler_create_info.maxLod = static_cast<float>(num_mips);
        sampler_create_info.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
        VK_CHECK_RESULT(vkCreateSampler(_device->logicalDevice, &sampler_create_info, nullptr, &cubemap.sampler));

        VkImageSubresourceRange subresource_range = {};
        subresource_range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        subresource_range.baseMipLevel = 0;
        subresource_range.levelCount = num_mips;
        subresource_range.layerCount = 6;

        VkCommandBuffer change_layout_cmd = _device->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);

        vks::tools::setImageLayout(change_layout_cmd, cubemap.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, subresource_range);

        _device->flushCommandBuffer(change_layout_cmd, _queue);

        cubemap.descriptor.imageView = cubemap.view;
        cubemap.descriptor.sampler = cubemap.sampler;
        cubemap.descriptor.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        cubemap.device = _device;
        cubemap.width = size;
        cubemap.height = size;
        cubemap.mipLevels = num_mips;

        return cubemap;
    }

    void RenderSceneToCubemap(Baikal::VkScene& scene, glm::vec3 camera_pos, vks::TextureCubeMap& tex, std::vector<VkSemaphore>& semaphores) {
        if (!_cmd_buffer_write_initialized) {
            VkCommandBufferBeginInfo cmd_buf_info = vks::initializers::commandBufferBeginInfo();

            VkClearValue clear_values[4] = {
                { 0.0f, 0.0f, 0.0f, 0.0f }, // color
                { 1.0f, 0 }                 // depth stencil
            };

            VkRenderPassBeginInfo render_pass_begin_info = vks::initializers::renderPassBeginInfo();
            render_pass_begin_info.renderArea.extent.width = _framebuffer[0]->width;
            render_pass_begin_info.renderArea.extent.height = _framebuffer[0]->height;
            render_pass_begin_info.clearValueCount = sizeof(clear_values) / sizeof(VkClearValue);
            render_pass_begin_info.pClearValues = clear_values;

            BuffersList& buffers_list = scene.resources->GetBuffersList();
            vks::Buffer fsq_vertex_buffer = buffers_list.Get(STATIC_CRC32("FullScreenQuadVB"));
            vks::Buffer fsq_index_buffer = buffers_list.Get(STATIC_CRC32("FullScreenQuadIB"));
            VkDeviceSize offsets[1] = { 0 };

            VkImageSubresourceRange cubemap_subresource_range = {};
            cubemap_subresource_range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            cubemap_subresource_range.baseMipLevel = 0;
            cubemap_subresource_range.levelCount = tex.mipLevels;
            cubemap_subresource_range.layerCount = 6;

            for (int face = 0; face < 6; face++) {
                render_pass_begin_info.renderPass = _framebuffer[face]->renderPass;
                render_pass_begin_info.framebuffer = _framebuffer[face]->framebuffer;

                vkBeginCommandBuffer(_render_to_cubemap_cmd_buf[face], &cmd_buf_info);

                vkCmdBeginRenderPass(_render_to_cubemap_cmd_buf[face], &render_pass_begin_info, VK_SUBPASS_CONTENTS_INLINE);

                VkViewport viewport = vks::initializers::viewport((float)_framebuffer[0]->width, (float)_framebuffer[0]->height, 0.0f, 1.0f);
                vkCmdSetViewport(_render_to_cubemap_cmd_buf[face], 0, 1, &viewport);

                VkRect2D scissor = vks::initializers::rect2D(_framebuffer[0]->width, _framebuffer[0]->height, 0, 0);
                vkCmdSetScissor(_render_to_cubemap_cmd_buf[face], 0, 1, &scissor);

                // First - render sky (TODO: render sky after scene with DEPTH_EQUAL function and DEPTH_WRITES disabled)
                vkCmdBindPipeline(_render_to_cubemap_cmd_buf[face], VK_PIPELINE_BIND_POINT_GRAPHICS, _sky_pipeline);
                vkCmdBindDescriptorSets(_render_to_cubemap_cmd_buf[face], VK_PIPELINE_BIND_POINT_GRAPHICS, _sky_pipeline_layout, 0, 1, &_sky_descriptor_set, 0, NULL);
                vkCmdBindVertexBuffers(_render_to_cubemap_cmd_buf[face], 0, 1, &fsq_vertex_buffer.buffer, offsets);
                vkCmdBindIndexBuffer(_render_to_cubemap_cmd_buf[face], fsq_index_buffer.buffer, 0, VK_INDEX_TYPE_UINT32);
                vkCmdPushConstants(_render_to_cubemap_cmd_buf[face], _sky_pipeline_layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(int), &face);

                vkCmdDrawIndexed(_render_to_cubemap_cmd_buf[face], 6, 1, 0, 0, 0);

                // Afterwards - scene
                vkCmdBindPipeline(_render_to_cubemap_cmd_buf[face], VK_PIPELINE_BIND_POINT_GRAPHICS, _pipeline);
                vkCmdPushConstants(_render_to_cubemap_cmd_buf[face], scene.cubemapPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(int), &face);

                scene.WriteToCmdBuffer(_render_to_cubemap_cmd_buf[face], Baikal::VkScene::SCENE_FORWARD_PASS);

                vkCmdEndRenderPass(_render_to_cubemap_cmd_buf[face]);

                vks::tools::setImageLayout(
                    _render_to_cubemap_cmd_buf[face],
                    _framebuffer[face]->attachments[0].image,
                    VK_IMAGE_ASPECT_COLOR_BIT,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

                vks::tools::setImageLayout(_render_to_cubemap_cmd_buf[face], tex.image, VK_IMAGE_LAYOUT_GENERAL,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, cubemap_subresource_range);

                // Copy region for transfer from framebuffer to cube face
                VkImageCopy copy_region = {};

                copy_region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                copy_region.srcSubresource.baseArrayLayer = 0;
                copy_region.srcSubresource.mipLevel = 0;
                copy_region.srcSubresource.layerCount = 1;
                copy_region.srcOffset = { 0, 0, 0 };

                copy_region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                copy_region.dstSubresource.baseArrayLayer = face;
                copy_region.dstSubresource.mipLevel = 0;
                copy_region.dstSubresource.layerCount = 1;
                copy_region.dstOffset = { 0, 0, 0 };

                copy_region.extent.width = static_cast<uint32_t>(viewport.width);
                copy_region.extent.height = static_cast<uint32_t>(viewport.height);
                copy_region.extent.depth = 1;

                vkCmdCopyImage(_render_to_cubemap_cmd_buf[face], _framebuffer[face]->attachments[0].image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, tex.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy_region);

                vks::tools::setImageLayout(_render_to_cubemap_cmd_buf[face], _framebuffer[face]->attachments[0].image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
                vks::tools::setImageLayout(_render_to_cubemap_cmd_buf[face], tex.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL, cubemap_subresource_range);

                VK_CHECK_RESULT(vkEndCommandBuffer(_render_to_cubemap_cmd_buf[face]));
            }

            _cmd_buffer_write_initialized = true;
        }

        _ubo.cam_position = glm::vec4(camera_pos, 1.0f);

        BuffersList& buffers_list = scene.resources->GetBuffersList();
        vks::Buffer scene_to_cube_buffer = buffers_list.Get(STATIC_CRC32("SceneToCube"));
        memcpy(scene_to_cube_buffer.mapped, &_ubo, sizeof(Baikal::VkScene::CubeViewInfo));

        for (int face = 0; face < 6; face++) {
            VkSubmitInfo submit_info = {};

            submit_info.pWaitDstStageMask = nullptr;
            submit_info.pWaitSemaphores = nullptr;
            submit_info.waitSemaphoreCount = 0;
            submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            submit_info.pSignalSemaphores = &_semaphores[face];
            submit_info.signalSemaphoreCount = 1;
            submit_info.commandBufferCount = 1;
            submit_info.pCommandBuffers = &_render_to_cubemap_cmd_buf[face];

            VK_CHECK_RESULT(vkQueueSubmit(_queue, 1, &submit_info, VK_NULL_HANDLE));
        }

        semaphores = _semaphores;
    }

    void GetSyncPrimitives(std::vector<VkSemaphore>& semaphores) {
        semaphores = _semaphores;
    }
public:
    const size_t _cubemap_face_size = 32;
protected:
    vks::Model _cube;
    vks::VulkanDevice* _device;
    VkQueue _queue;

    const uint32_t      _scene_forward_render_name = STATIC_CRC32("SceneForwardRender");
    const uint32_t      _sky_forward_render_name = STATIC_CRC32("SkyForwardRender");

    vks::Framebuffer*   _framebuffer[6];
    VkCommandBuffer     _render_to_cubemap_cmd_buf[6];
    VkPipeline          _pipeline;
    std::vector<VkSemaphore> _semaphores;

    // Sky rendering in forward pass
    VkPipeline            _sky_pipeline;
    VkPipelineLayout      _sky_pipeline_layout;
    VkDescriptorSetLayout _sky_descriptor_set_layout;
    VkDescriptorSet       _sky_descriptor_set;

    Baikal::VkScene::CubeViewInfo   _ubo;

    bool                  _cmd_buffer_write_initialized = false;
};