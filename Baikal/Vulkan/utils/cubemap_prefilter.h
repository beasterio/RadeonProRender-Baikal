#pragma once

#include "VulkanTexture.hpp"
#include "VulkanModel.hpp"
#include "VulkanTools.h"

#include "../resources/resource_manager.h"

#include "glm/glm.hpp"

#include <vector>
#include <cmath>

class CubemapPrefilter
{
public:
    struct SH9Color
    {
        glm::vec4 r[3];
        glm::vec4 g[3];
        glm::vec4 b[3];
    };
public:
    CubemapPrefilter(const char* asset_path, vks::VulkanDevice* vulkan_device, VkQueue queue, ResourceManager* resources)
        : _vulkan_device(vulkan_device)
        , _queue(queue)
        , _resources(resources)
    {
        char path[MAX_PATH] = {'\0'};

        sprintf(path, "%s/%s", asset_path, "cube.obj");

        _cube.loadFromFile(path, _vertex_layout, 1.0f, vulkan_device, queue);

        VkCommandBufferAllocateInfo cmd_buf_alloc_info = vks::initializers::commandBufferAllocateInfo(_vulkan_device->computeCommandPool, VK_COMMAND_BUFFER_LEVEL_PRIMARY, 1);
        VK_CHECK_RESULT(vkAllocateCommandBuffers(_vulkan_device->logicalDevice, &cmd_buf_alloc_info, &_sh9_cmd_buf));
        VK_CHECK_RESULT(vkAllocateCommandBuffers(_vulkan_device->logicalDevice, &cmd_buf_alloc_info, &_sh9_cmd_buf_project));
        VK_CHECK_RESULT(vkAllocateCommandBuffers(_vulkan_device->logicalDevice, &cmd_buf_alloc_info, &_sh9_cmd_buf_downsample));

        GenerateIrradianceCube();      
        GeneratePrefilteredCube();
        GenerateBRDFLUT();
   }

    ~CubemapPrefilter() {
        _cube.destroy();
        
        for (size_t i = 0; i < _sh9_buffers.size(); i++)
        {
            _sh9_buffers[i].destroy();
             vkDestroySemaphore(_vulkan_device->logicalDevice, _buffers_semaphores[i], nullptr);
        };

        vkFreeCommandBuffers(_vulkan_device->logicalDevice, _vulkan_device->computeCommandPool, 1, &_sh9_cmd_buf);
        vkFreeCommandBuffers(_vulkan_device->logicalDevice, _vulkan_device->computeCommandPool, 1, &_sh9_cmd_buf_project);
        vkFreeCommandBuffers(_vulkan_device->logicalDevice, _vulkan_device->computeCommandPool, 1, &_sh9_cmd_buf_downsample);
    }

    void GenerateIrradianceCube() {
        vks::TextureCubeMap irradiance_cube;

        const VkFormat format = VK_FORMAT_R32G32B32A32_SFLOAT;
        const int32_t dim = 64;
        const uint32_t numMips = static_cast<uint32_t>(floor(log2(dim))) + 1;

        VkImageCreateInfo image_create_info = vks::initializers::imageCreateInfo();
        image_create_info.imageType = VK_IMAGE_TYPE_2D;
        image_create_info.format = format;
        image_create_info.extent.width = dim;
        image_create_info.extent.height = dim;
        image_create_info.extent.depth = 1;
        image_create_info.mipLevels = numMips;
        image_create_info.arrayLayers = 6;
        image_create_info.samples = VK_SAMPLE_COUNT_1_BIT;
        image_create_info.tiling = VK_IMAGE_TILING_OPTIMAL;
        image_create_info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        image_create_info.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
        VK_CHECK_RESULT(vkCreateImage(_vulkan_device->logicalDevice, &image_create_info, nullptr, &irradiance_cube.image));

        VkMemoryAllocateInfo memAlloc = vks::initializers::memoryAllocateInfo();
        VkMemoryRequirements memReqs;
        vkGetImageMemoryRequirements(_vulkan_device->logicalDevice, irradiance_cube.image, &memReqs);
        memAlloc.allocationSize = memReqs.size;
        memAlloc.memoryTypeIndex = _vulkan_device->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        VK_CHECK_RESULT(vkAllocateMemory(_vulkan_device->logicalDevice, &memAlloc, nullptr, &irradiance_cube.deviceMemory));
        VK_CHECK_RESULT(vkBindImageMemory(_vulkan_device->logicalDevice, irradiance_cube.image, irradiance_cube.deviceMemory, 0));
        
        // Image view
        VkImageViewCreateInfo view_create_info = vks::initializers::imageViewCreateInfo();
        view_create_info.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
        view_create_info.format = format;
        view_create_info.subresourceRange = {};
        view_create_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        view_create_info.subresourceRange.levelCount = numMips;
        view_create_info.subresourceRange.layerCount = 6;
        view_create_info.image = irradiance_cube.image;
        VK_CHECK_RESULT(vkCreateImageView(_vulkan_device->logicalDevice, &view_create_info, nullptr, &irradiance_cube.view));

        // Sampler
        VkSamplerCreateInfo sampler_create_info = vks::initializers::samplerCreateInfo();
        sampler_create_info.magFilter = VK_FILTER_LINEAR;
        sampler_create_info.minFilter = VK_FILTER_LINEAR;
        sampler_create_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        sampler_create_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler_create_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler_create_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler_create_info.minLod = 0.0f;
        sampler_create_info.maxLod = static_cast<float>(numMips);
        sampler_create_info.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
        VK_CHECK_RESULT(vkCreateSampler(_vulkan_device->logicalDevice, &sampler_create_info, nullptr, &irradiance_cube.sampler));

        irradiance_cube.descriptor.imageView = irradiance_cube.view;
        irradiance_cube.descriptor.sampler = irradiance_cube.sampler;
        irradiance_cube.descriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        irradiance_cube.device = _vulkan_device;

        VkAttachmentDescription attributes_description = {};

        // Color attachment
        attributes_description.format = format;
        attributes_description.samples = VK_SAMPLE_COUNT_1_BIT;
        attributes_description.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attributes_description.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attributes_description.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attributes_description.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attributes_description.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attributes_description.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        VkAttachmentReference color_reference = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };

        VkSubpassDescription subpassDescription = {};
        subpassDescription.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpassDescription.colorAttachmentCount = 1;
        subpassDescription.pColorAttachments = &color_reference;

        // Use subpass dependencies for layout transitions
        std::array<VkSubpassDependency, 2> dependencies;
        dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[0].dstSubpass = 0;
        dependencies[0].srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
        dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependencies[0].srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
        dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
        dependencies[1].srcSubpass = 0;
        dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependencies[1].dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
        dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        dependencies[1].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
        dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

        // Renderpass
        VkRenderPassCreateInfo renderPassCI = vks::initializers::renderPassCreateInfo();
        renderPassCI.attachmentCount = 1;
        renderPassCI.pAttachments = &attributes_description;
        renderPassCI.subpassCount = 1;
        renderPassCI.pSubpasses = &subpassDescription;
        renderPassCI.dependencyCount = 2;
        renderPassCI.pDependencies = dependencies.data();
        VkRenderPass renderpass;
        VK_CHECK_RESULT(vkCreateRenderPass(_vulkan_device->logicalDevice, &renderPassCI, nullptr, &renderpass));

        struct {
            VkImage image;
            VkImageView view;
            VkDeviceMemory memory;
            VkFramebuffer framebuffer;
        } offscreen;

        // Offfscreen framebuffer
        {
            // Color attachment
            VkImageCreateInfo image_create_info = vks::initializers::imageCreateInfo();
            image_create_info.imageType = VK_IMAGE_TYPE_2D;
            image_create_info.format = format;
            image_create_info.extent.width = dim;
            image_create_info.extent.height = dim;
            image_create_info.extent.depth = 1;
            image_create_info.mipLevels = 1;
            image_create_info.arrayLayers = 1;
            image_create_info.samples = VK_SAMPLE_COUNT_1_BIT;
            image_create_info.tiling = VK_IMAGE_TILING_OPTIMAL;
            image_create_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            image_create_info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
            image_create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            VK_CHECK_RESULT(vkCreateImage(_vulkan_device->logicalDevice, &image_create_info, nullptr, &offscreen.image));

            VkMemoryAllocateInfo memAlloc = vks::initializers::memoryAllocateInfo();
            VkMemoryRequirements memReqs;
            vkGetImageMemoryRequirements(_vulkan_device->logicalDevice, offscreen.image, &memReqs);
            memAlloc.allocationSize = memReqs.size;
            memAlloc.memoryTypeIndex = _vulkan_device->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            VK_CHECK_RESULT(vkAllocateMemory(_vulkan_device->logicalDevice, &memAlloc, nullptr, &offscreen.memory));
            VK_CHECK_RESULT(vkBindImageMemory(_vulkan_device->logicalDevice, offscreen.image, offscreen.memory, 0));

            VkImageViewCreateInfo color_image_view = vks::initializers::imageViewCreateInfo();
            color_image_view.viewType = VK_IMAGE_VIEW_TYPE_2D;
            color_image_view.format = format;
            color_image_view.flags = 0;
            color_image_view.subresourceRange = {};
            color_image_view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            color_image_view.subresourceRange.baseMipLevel = 0;
            color_image_view.subresourceRange.levelCount = 1;
            color_image_view.subresourceRange.baseArrayLayer = 0;
            color_image_view.subresourceRange.layerCount = 1;
            color_image_view.image = offscreen.image;
            VK_CHECK_RESULT(vkCreateImageView(_vulkan_device->logicalDevice, &color_image_view, nullptr, &offscreen.view));

            VkFramebufferCreateInfo framebuffer_create_info = vks::initializers::framebufferCreateInfo();
            framebuffer_create_info.renderPass = renderpass;
            framebuffer_create_info.attachmentCount = 1;
            framebuffer_create_info.pAttachments = &offscreen.view;
            framebuffer_create_info.width = dim;
            framebuffer_create_info.height = dim;
            framebuffer_create_info.layers = 1;
            VK_CHECK_RESULT(vkCreateFramebuffer(_vulkan_device->logicalDevice, &framebuffer_create_info, nullptr, &offscreen.framebuffer));
            
            VkCommandBuffer layout_cmd = _vulkan_device->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);
            vks::tools::setImageLayout(layout_cmd, offscreen.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
            _vulkan_device->flushCommandBuffer(layout_cmd, _queue, true);
        }

        // Descriptors
        VkDescriptorSetLayout descriptorsetlayout;
        std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings = {
            vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 0),
        };
        VkDescriptorSetLayoutCreateInfo descriptorsetlayoutCI = vks::initializers::descriptorSetLayoutCreateInfo(setLayoutBindings);
        VK_CHECK_RESULT(vkCreateDescriptorSetLayout(_vulkan_device->logicalDevice, &descriptorsetlayoutCI, nullptr, &descriptorsetlayout));

        // Descriptor Pool
        std::vector<VkDescriptorPoolSize> poolSizes = { vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1) };
        VkDescriptorPoolCreateInfo descriptorPoolCI = vks::initializers::descriptorPoolCreateInfo(poolSizes, 2);
        VkDescriptorPool descriptorpool;
        VK_CHECK_RESULT(vkCreateDescriptorPool(_vulkan_device->logicalDevice, &descriptorPoolCI, nullptr, &descriptorpool));

        TextureList& texture_list = _resources->GetTextures();
        vks::Texture const& env_texture = texture_list.Get(STATIC_CRC32("EnvMap"));

        // Descriptor sets
        VkDescriptorSet descriptorset;
        VkDescriptorSetAllocateInfo allocInfo = vks::initializers::descriptorSetAllocateInfo(descriptorpool, &descriptorsetlayout, 1);
        VK_CHECK_RESULT(vkAllocateDescriptorSets(_vulkan_device->logicalDevice, &allocInfo, &descriptorset));
        VkWriteDescriptorSet writeDescriptorSet = vks::initializers::writeDescriptorSet(descriptorset, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 0, const_cast<VkDescriptorImageInfo*>(&env_texture.descriptor));
        vkUpdateDescriptorSets(_vulkan_device->logicalDevice, 1, &writeDescriptorSet, 0, nullptr);

        // Pipeline layout
        struct PushBlock {
            glm::mat4 mvp;
            // Sampling deltas
            float deltaPhi = (2.0f * float(M_PI)) / 180.0f;
            float deltaTheta = (0.5f * float(M_PI)) / 64.0f;
        } pushBlock;

        VkPipelineLayout pipelinelayout;
        std::vector<VkPushConstantRange> pushConstantRanges = {
            vks::initializers::pushConstantRange(VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(PushBlock), 0),
        };
        VkPipelineLayoutCreateInfo pipelineLayoutCI = vks::initializers::pipelineLayoutCreateInfo(&descriptorsetlayout, 1);
        pipelineLayoutCI.pushConstantRangeCount = 1;
        pipelineLayoutCI.pPushConstantRanges = pushConstantRanges.data();
        VK_CHECK_RESULT(vkCreatePipelineLayout(_vulkan_device->logicalDevice, &pipelineLayoutCI, nullptr, &pipelinelayout));

        // Pipeline
        VkPipelineInputAssemblyStateCreateInfo inputAssemblyState = vks::initializers::pipelineInputAssemblyStateCreateInfo(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 0, VK_FALSE);
        VkPipelineRasterizationStateCreateInfo rasterizationState = vks::initializers::pipelineRasterizationStateCreateInfo(VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE);
        VkPipelineColorBlendAttachmentState blendAttachmentState = vks::initializers::pipelineColorBlendAttachmentState(0xf, VK_FALSE);
        VkPipelineColorBlendStateCreateInfo colorBlendState = vks::initializers::pipelineColorBlendStateCreateInfo(1, &blendAttachmentState);
        VkPipelineDepthStencilStateCreateInfo depthStencilState = vks::initializers::pipelineDepthStencilStateCreateInfo(VK_FALSE, VK_FALSE, VK_COMPARE_OP_LESS_OR_EQUAL);
        VkPipelineViewportStateCreateInfo viewportState = vks::initializers::pipelineViewportStateCreateInfo(1, 1);
        VkPipelineMultisampleStateCreateInfo multisampleState = vks::initializers::pipelineMultisampleStateCreateInfo(VK_SAMPLE_COUNT_1_BIT);
        std::vector<VkDynamicState> dynamicStateEnables = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
        VkPipelineDynamicStateCreateInfo dynamicState = vks::initializers::pipelineDynamicStateCreateInfo(dynamicStateEnables);
        // Vertex input state
        VkVertexInputBindingDescription vertexInputBinding = vks::initializers::vertexInputBindingDescription(0, _vertex_layout.stride(), VK_VERTEX_INPUT_RATE_VERTEX);
        VkVertexInputAttributeDescription vertexInputAttribute = vks::initializers::vertexInputAttributeDescription(0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0);

        VkPipelineVertexInputStateCreateInfo vertexInputState = vks::initializers::pipelineVertexInputStateCreateInfo();
        vertexInputState.vertexBindingDescriptionCount = 1;
        vertexInputState.pVertexBindingDescriptions = &vertexInputBinding;
        vertexInputState.vertexAttributeDescriptionCount = 1;
        vertexInputState.pVertexAttributeDescriptions = &vertexInputAttribute;

        ShaderList& shader_list = _resources->GetShaderList();
        PipelineList& pipeline_list = _resources->GetPipepineList();

        std::array<VkPipelineShaderStageCreateInfo, 2> shader_stages = {
            shader_list.Load("../Baikal/Kernels/VK/shaders/filtercube.vert.spv", VK_SHADER_STAGE_VERTEX_BIT),
            shader_list.Load("../Baikal/Kernels/VK/shaders/irradiancecube.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT) };

        VkGraphicsPipelineCreateInfo pipelineCI = vks::initializers::pipelineCreateInfo(pipelinelayout, renderpass);
        pipelineCI.pInputAssemblyState = &inputAssemblyState;
        pipelineCI.pRasterizationState = &rasterizationState;
        pipelineCI.pColorBlendState = &colorBlendState;
        pipelineCI.pMultisampleState = &multisampleState;
        pipelineCI.pViewportState = &viewportState;
        pipelineCI.pDepthStencilState = &depthStencilState;
        pipelineCI.pDynamicState = &dynamicState;
        pipelineCI.stageCount = 2;
        pipelineCI.pStages = shader_stages.data();
        pipelineCI.pVertexInputState = &vertexInputState;
        pipelineCI.renderPass = renderpass;

        VkPipeline pipeline;
        VK_CHECK_RESULT(vkCreateGraphicsPipelines(_vulkan_device->logicalDevice, pipeline_list.GetPipelineCache(), 1, &pipelineCI, nullptr, &pipeline));

        // Render

        VkClearValue clearValues[1];
        clearValues[0].color = { { 0.0f, 0.0f, 0.2f, 0.0f } };

        VkRenderPassBeginInfo renderPassBeginInfo = vks::initializers::renderPassBeginInfo();
        // Reuse render pass from example pass
        renderPassBeginInfo.renderPass = renderpass;
        renderPassBeginInfo.framebuffer = offscreen.framebuffer;
        renderPassBeginInfo.renderArea.extent.width = dim;
        renderPassBeginInfo.renderArea.extent.height = dim;
        renderPassBeginInfo.clearValueCount = 1;
        renderPassBeginInfo.pClearValues = clearValues;

        std::vector<glm::mat4> matrices = {
            // POSITIVE_X
            glm::rotate(glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(0.0f, 1.0f, 0.0f)), glm::radians(180.0f), glm::vec3(1.0f, 0.0f, 0.0f)),
            // NEGATIVE_X
            glm::rotate(glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f), glm::vec3(0.0f, 1.0f, 0.0f)), glm::radians(180.0f), glm::vec3(1.0f, 0.0f, 0.0f)),
            // POSITIVE_Y
            glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f), glm::vec3(1.0f, 0.0f, 0.0f)),
            // NEGATIVE_Y
            glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f)),
            // POSITIVE_Z
            glm::rotate(glm::mat4(1.0f), glm::radians(180.0f), glm::vec3(1.0f, 0.0f, 0.0f)),
            // NEGATIVE_Z
            glm::rotate(glm::mat4(1.0f), glm::radians(180.0f), glm::vec3(0.0f, 0.0f, 1.0f)),
        };

        VkCommandBuffer cmdBuf = _vulkan_device->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);

        VkViewport viewport = vks::initializers::viewport((float)dim, (float)dim, 0.0f, 1.0f);
        VkRect2D scissor = vks::initializers::rect2D(dim, dim, 0, 0);

        vkCmdSetViewport(cmdBuf, 0, 1, &viewport);
        vkCmdSetScissor(cmdBuf, 0, 1, &scissor);

        VkImageSubresourceRange subresourceRange = {};
        subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        subresourceRange.baseMipLevel = 0;
        subresourceRange.levelCount = numMips;
        subresourceRange.layerCount = 6;

        // Change image layout for all cubemap faces to transfer destination
        vks::tools::setImageLayout(
            cmdBuf,
            irradiance_cube.image,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            subresourceRange);

        for (uint32_t m = 0; m < numMips; m++) {
            for (uint32_t f = 0; f < 6; f++) {
                viewport.width = static_cast<float>(dim * std::pow(0.5f, m));
                viewport.height = static_cast<float>(dim * std::pow(0.5f, m));
                vkCmdSetViewport(cmdBuf, 0, 1, &viewport);

                // Render scene from cube face's point of view
                vkCmdBeginRenderPass(cmdBuf, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

                // Update shader push constant block
                pushBlock.mvp = glm::perspective((float)(M_PI / 2.0), 1.0f, 0.1f, 512.0f) * matrices[f];

                vkCmdPushConstants(cmdBuf, pipelinelayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushBlock), &pushBlock);

                vkCmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
                vkCmdBindDescriptorSets(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelinelayout, 0, 1, &descriptorset, 0, NULL);

                VkDeviceSize offsets[1] = { 0 };

                vkCmdBindVertexBuffers(cmdBuf, 0, 1, &_cube.vertices.buffer, offsets);
                vkCmdBindIndexBuffer(cmdBuf, _cube.indices.buffer, 0, VK_INDEX_TYPE_UINT32);
                vkCmdDrawIndexed(cmdBuf, _cube.indexCount, 1, 0, 0, 0);

                vkCmdEndRenderPass(cmdBuf);

                vks::tools::setImageLayout(
                    cmdBuf,
                    offscreen.image,
                    VK_IMAGE_ASPECT_COLOR_BIT,
                    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

                // Copy region for transfer from framebuffer to cube face
                VkImageCopy copyRegion = {};

                copyRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                copyRegion.srcSubresource.baseArrayLayer = 0;
                copyRegion.srcSubresource.mipLevel = 0;
                copyRegion.srcSubresource.layerCount = 1;
                copyRegion.srcOffset = { 0, 0, 0 };

                copyRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                copyRegion.dstSubresource.baseArrayLayer = f;
                copyRegion.dstSubresource.mipLevel = m;
                copyRegion.dstSubresource.layerCount = 1;
                copyRegion.dstOffset = { 0, 0, 0 };

                copyRegion.extent.width = static_cast<uint32_t>(viewport.width);
                copyRegion.extent.height = static_cast<uint32_t>(viewport.height);
                copyRegion.extent.depth = 1;

                vkCmdCopyImage(
                    cmdBuf,
                    offscreen.image,
                    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    irradiance_cube.image,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    1,
                    &copyRegion);

                // Transform framebuffer color attachment back 
                vks::tools::setImageLayout(
                    cmdBuf,
                    offscreen.image,
                    VK_IMAGE_ASPECT_COLOR_BIT,
                    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
            }
        }

        vks::tools::setImageLayout(
            cmdBuf,
            irradiance_cube.image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            subresourceRange);

        _vulkan_device->flushCommandBuffer(cmdBuf, _queue);

        // todo: cleanup
        vkDestroyRenderPass(_vulkan_device->logicalDevice, renderpass, nullptr);
        vkDestroyFramebuffer(_vulkan_device->logicalDevice, offscreen.framebuffer, nullptr);
        vkFreeMemory(_vulkan_device->logicalDevice, offscreen.memory, nullptr);
        vkDestroyImageView(_vulkan_device->logicalDevice, offscreen.view, nullptr);
        vkDestroyImage(_vulkan_device->logicalDevice, offscreen.image, nullptr);
        vkDestroyDescriptorPool(_vulkan_device->logicalDevice, descriptorpool, nullptr);
        vkDestroyDescriptorSetLayout(_vulkan_device->logicalDevice, descriptorsetlayout, nullptr);
        vkDestroyPipeline(_vulkan_device->logicalDevice, pipeline, nullptr);
        vkDestroyPipelineLayout(_vulkan_device->logicalDevice, pipelinelayout, nullptr);

        texture_list.Set(STATIC_CRC32("IrradianceCube"), irradiance_cube);
    }

    void AllocateSH9ProjectResources(vks::Texture const& tex) {
        ShaderList& shader_list = _resources->GetShaderList();
        PipelineList& pipeline_list = _resources->GetPipepineList();
        PipelineLayoutList& pipelinelayout_list = _resources->GetPipepineLayoutList();
        DescriptionSetLayoutList& desc_set_layout_list = _resources->GetDescriptionSetLayoutList();
        DescriptionSetList& desc_set_list = _resources->GetDescriptionSetList();
        TextureList& texture_list = _resources->GetTextures();
        BuffersList& buffer_list = _resources->GetBuffersList();

        if (!desc_set_layout_list.Present(_gen_sh9_project)) {
            std::vector<VkDescriptorSetLayoutBinding> set_layout_bindings = {
                vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT, 0),
                vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT, 1)
            };

            VkDescriptorSetLayoutCreateInfo descriptor_layout = vks::initializers::descriptorSetLayoutCreateInfo(set_layout_bindings.data(), set_layout_bindings.size());

            VkDescriptorSetLayout desc_set_layout_project;
            VK_CHECK_RESULT(vkCreateDescriptorSetLayout(_vulkan_device->logicalDevice, &descriptor_layout, nullptr, &desc_set_layout_project));

            desc_set_layout_list.Set(_gen_sh9_project, desc_set_layout_project);
        }

        if (!pipelinelayout_list.Present(_gen_sh9_project)) {
            VkDescriptorSetLayout desc_set_layout = desc_set_layout_list.Get(_gen_sh9_project);

            VkPipelineLayout pipeline_layout;
            VkPipelineLayoutCreateInfo pipeline_create_info = vks::initializers::pipelineLayoutCreateInfo(&desc_set_layout, 1);

            VK_CHECK_RESULT(vkCreatePipelineLayout(_vulkan_device->logicalDevice, &pipeline_create_info, nullptr, &pipeline_layout));

            pipelinelayout_list.Set(_gen_sh9_project, pipeline_layout);
        }

        if (!desc_set_list.Present(_gen_sh9_project)) {
            VkDescriptorSetLayout descriptor_set_layout = desc_set_layout_list.Get(_gen_sh9_project);
            VkDescriptorSetAllocateInfo alloc_info = vks::initializers::descriptorSetAllocateInfo(desc_set_list.GetDescriptorPool(), &descriptor_set_layout, 1);

            VkDescriptorSet descriptor_set_project;
            VK_CHECK_RESULT(vkAllocateDescriptorSets(_vulkan_device->logicalDevice, &alloc_info, &descriptor_set_project));
            desc_set_list.Set(_gen_sh9_project, descriptor_set_project);
        }
        
        //Build mip-chain for project pass and further downsampling
        if (_sh9_buffers.empty()){      
            const uint32_t num_mips = static_cast<uint32_t>(floor(log2(std::max(tex.width, tex.height)))) + 1;
            const uint32_t num_cube_faces = 6;

            _sh9_buffers.resize(num_mips);
            _buffers_semaphores.resize(num_mips);
            
            VkSemaphoreCreateInfo semaphore_create_info = vks::initializers::semaphoreCreateInfo();

            for (int i = 0; i < num_mips; i++) {
                uint32_t width = tex.width >> i;
                uint32_t height = tex.height >> i;
                uint32_t buffer_size = num_cube_faces * width * height * sizeof(SH9Color);

                VK_CHECK_RESULT(_vulkan_device->createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &_sh9_buffers[i], buffer_size));
                VK_CHECK_RESULT(vkCreateSemaphore(_vulkan_device->logicalDevice, &semaphore_create_info, nullptr, &_buffers_semaphores[i]));
            }
        }

        if (!pipeline_list.Present(_gen_sh9_project)) {
            VkPipelineLayout pipeline_layout = pipelinelayout_list.Get(_gen_sh9_project);

            VkPipelineShaderStageCreateInfo shader_stage = shader_list.Load("../Baikal/Kernels/VK/shaders/cubemap_sh9_project.comp.spv", VK_SHADER_STAGE_COMPUTE_BIT);

            VkComputePipelineCreateInfo computePipelineCreateInfo = vks::initializers::computePipelineCreateInfo(pipeline_layout, 0);
            computePipelineCreateInfo.stage = shader_stage;

            VkPipeline pipeline;
            VK_CHECK_RESULT(vkCreateComputePipelines(_vulkan_device->logicalDevice, pipeline_list.GetPipelineCache(), 1, &computePipelineCreateInfo, nullptr, &pipeline));

            pipeline_list.Set(_gen_sh9_project, pipeline);
        }
    }

    void AllocateSH9DownsampleResources(vks::Texture const& tex) {
        ShaderList& shader_list = _resources->GetShaderList();
        PipelineList& pipeline_list = _resources->GetPipepineList();
        PipelineLayoutList& pipelinelayout_list = _resources->GetPipepineLayoutList();
        DescriptionSetLayoutList& desc_set_layout_list = _resources->GetDescriptionSetLayoutList();
        DescriptionSetList& desc_set_list = _resources->GetDescriptionSetList();
        TextureList& texture_list = _resources->GetTextures();
        BuffersList& buffer_list = _resources->GetBuffersList();

        if (!desc_set_layout_list.Present(_gen_sh9_downsample)) {
            std::vector<VkDescriptorSetLayoutBinding> set_layout_bindings = {
                vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT, 0),
                vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT, 1)
            };

            VkDescriptorSetLayoutCreateInfo descriptor_layout = vks::initializers::descriptorSetLayoutCreateInfo(set_layout_bindings.data(), set_layout_bindings.size());

            VkDescriptorSetLayout desc_set_layout_downsample;
            VK_CHECK_RESULT(vkCreateDescriptorSetLayout(_vulkan_device->logicalDevice, &descriptor_layout, nullptr, &desc_set_layout_downsample));

            desc_set_layout_list.Set(_gen_sh9_downsample, desc_set_layout_downsample);
        }

        if (!pipelinelayout_list.Present(_gen_sh9_downsample)) {
            VkDescriptorSetLayout desc_set_layout = desc_set_layout_list.Get(_gen_sh9_downsample);

            VkPipelineLayout pipeline_layout;
            VkPipelineLayoutCreateInfo pipeline_create_info = vks::initializers::pipelineLayoutCreateInfo(&desc_set_layout, 1);
            
            VkPushConstantRange push_constants_range = vks::initializers::pushConstantRange(VK_SHADER_STAGE_COMPUTE_BIT, 4 * sizeof(int), 0);
            pipeline_create_info.pushConstantRangeCount = 1;
            pipeline_create_info.pPushConstantRanges = &push_constants_range;

            VK_CHECK_RESULT(vkCreatePipelineLayout(_vulkan_device->logicalDevice, &pipeline_create_info, nullptr, &pipeline_layout));

            pipelinelayout_list.Set(_gen_sh9_downsample, pipeline_layout);
        }

        if (!desc_set_list.Present(_gen_sh9_downsample)) {
            VkDescriptorSetLayout descriptor_set_layout = desc_set_layout_list.Get(_gen_sh9_downsample);
            VkDescriptorSetAllocateInfo alloc_info = vks::initializers::descriptorSetAllocateInfo(desc_set_list.GetDescriptorPool(), &descriptor_set_layout, 1);

            VkDescriptorSet descriptor_set_downsample;
            VK_CHECK_RESULT(vkAllocateDescriptorSets(_vulkan_device->logicalDevice, &alloc_info, &descriptor_set_downsample));
            desc_set_list.Set(_gen_sh9_downsample, descriptor_set_downsample);
        }

        if (!pipeline_list.Present(_gen_sh9_downsample)) {
            VkPipelineLayout pipeline_layout = pipelinelayout_list.Get(_gen_sh9_downsample);

            VkPipelineShaderStageCreateInfo shader_stage = shader_list.Load("../Baikal/Kernels/VK/shaders/cubemap_sh9_downsample.comp.spv", VK_SHADER_STAGE_COMPUTE_BIT);

            VkComputePipelineCreateInfo computePipelineCreateInfo = vks::initializers::computePipelineCreateInfo(pipeline_layout, 0);
            computePipelineCreateInfo.stage = shader_stage;

            VkPipeline pipeline;
            VK_CHECK_RESULT(vkCreateComputePipelines(_vulkan_device->logicalDevice, pipeline_list.GetPipelineCache(), 1, &computePipelineCreateInfo, nullptr, &pipeline));

            pipeline_list.Set(_gen_sh9_downsample, pipeline);
        }

        if (!pipeline_list.Present(_gen_sh9_final)) {
            VkPipelineLayout pipeline_layout = pipelinelayout_list.Get(_gen_sh9_downsample);

            VkPipelineShaderStageCreateInfo shader_stage = shader_list.Load("../Baikal/Kernels/VK/shaders/cubemap_sh9_final.comp.spv", VK_SHADER_STAGE_COMPUTE_BIT);

            VkComputePipelineCreateInfo computePipelineCreateInfo = vks::initializers::computePipelineCreateInfo(pipeline_layout, 0);
            computePipelineCreateInfo.stage = shader_stage;

            VkPipeline pipeline;
            VK_CHECK_RESULT(vkCreateComputePipelines(_vulkan_device->logicalDevice, pipeline_list.GetPipelineCache(), 1, &computePipelineCreateInfo, nullptr, &pipeline));

            pipeline_list.Set(_gen_sh9_final, pipeline);
        }
    }

    void CubemapSH9ProjectPass(vks::Texture const& tex, std::vector<VkSemaphore>& semaphores) {
        DescriptionSetList& desc_set_list = _resources->GetDescriptionSetList();
        PipelineList& pipeline_list = _resources->GetPipepineList();
        PipelineLayoutList& pipelinelayout_list = _resources->GetPipepineLayoutList();
        TextureList& texture_list = _resources->GetTextures();
     
        static bool project_first_invocation = true;
        project_first_invocation = true;

        if (project_first_invocation) {
            VkDescriptorSet descriptor_set_sh9_project = desc_set_list.Get(_gen_sh9_project);

            // update sh9 project desc set
            VkWriteDescriptorSet writeDescriptorSets[2] = {
                vks::initializers::writeDescriptorSet(descriptor_set_sh9_project, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 0, const_cast<VkDescriptorImageInfo*>(&tex.descriptor)),
                vks::initializers::writeDescriptorSet(descriptor_set_sh9_project, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, &_sh9_buffers[0].descriptor)
            };

            vkUpdateDescriptorSets(_vulkan_device->logicalDevice, sizeof(writeDescriptorSets) / sizeof(VkWriteDescriptorSet), writeDescriptorSets, 0, NULL);

            VkCommandBufferBeginInfo cmd_buf_info = vks::initializers::commandBufferBeginInfo();
            VK_CHECK_RESULT(vkBeginCommandBuffer(_sh9_cmd_buf_project, &cmd_buf_info));

            VkPipeline pipeline = pipeline_list.Get(_gen_sh9_project);
            VkPipelineLayout pipeline_layout = pipelinelayout_list.Get(_gen_sh9_project);

            vkCmdBindPipeline(_sh9_cmd_buf_project, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
            vkCmdBindDescriptorSets(_sh9_cmd_buf_project, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_layout, 0, 1, &descriptor_set_sh9_project, 0, 0);

            uint32_t groupSize = 8;
            uint32_t groupCountX = (tex.width + groupSize - 1) / groupSize;
            uint32_t groupCountY = (tex.height + groupSize - 1) / groupSize;

            vkCmdDispatch(_sh9_cmd_buf_project, groupCountX, groupCountY, 6);

            vkEndCommandBuffer(_sh9_cmd_buf_project);
            project_first_invocation = false;
        }

        VkSubmitInfo submit_info = {};
        submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit_info.commandBufferCount = 1;
        submit_info.waitSemaphoreCount = semaphores.size();
        submit_info.pWaitSemaphores = &semaphores[0];
        
        VkPipelineStageFlags stage_flags[6] = { 
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
        };

        submit_info.pWaitDstStageMask = &stage_flags[0];
        submit_info.signalSemaphoreCount = 1;
        submit_info.pSignalSemaphores = &_buffers_semaphores[0];
        submit_info.pCommandBuffers = &_sh9_cmd_buf_project;

        VkQueue compute_queue;
        vkGetDeviceQueue(_vulkan_device->logicalDevice, _vulkan_device->queueFamilyIndices.compute, 0, &compute_queue);
        VK_CHECK_RESULT(vkQueueSubmit(compute_queue, 1, &submit_info, VK_NULL_HANDLE));
    }

    void CubemapSH9IntegratePass(vks::Texture const& tex, vks::Buffer sh9_buffer, int probe_idx) {
        DescriptionSetList& desc_set_list = _resources->GetDescriptionSetList();
        PipelineList& pipeline_list = _resources->GetPipepineList();
        PipelineLayoutList& pipelinelayout_list = _resources->GetPipepineLayoutList();
        TextureList& texture_list = _resources->GetTextures();

        VkDescriptorSet descriptor_set_sh9_downsample = desc_set_list.Get(_gen_sh9_downsample);

        const uint32_t num_mips = static_cast<uint32_t>(floor(log2(std::max(tex.width, tex.height)))) + 1;

        assert(num_mips == _sh9_buffers.size());

        for (int i = 0; i < num_mips - 1; i++) {
            const uint32_t src_tex_width = tex.width >> i;
            const uint32_t src_tex_height = tex.height >> i;
            const uint32_t dst_tex_width = tex.width >> (i + 1);
            const uint32_t dst_tex_height = tex.height >> (i + 1);

            // update sh9 desc set
            VkWriteDescriptorSet writeDescriptorSets[2] = {
                vks::initializers::writeDescriptorSet(descriptor_set_sh9_downsample, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 0, &_sh9_buffers[i].descriptor),
                vks::initializers::writeDescriptorSet(descriptor_set_sh9_downsample, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, &_sh9_buffers[i + 1].descriptor)
            };

            vkUpdateDescriptorSets(_vulkan_device->logicalDevice, sizeof(writeDescriptorSets) / sizeof(VkWriteDescriptorSet), writeDescriptorSets, 0, NULL);

            VkCommandBufferBeginInfo cmd_buf_info = vks::initializers::commandBufferBeginInfo();
            VK_CHECK_RESULT(vkBeginCommandBuffer(_sh9_cmd_buf_downsample, &cmd_buf_info));

            VkPipeline pipeline = pipeline_list.Get(_gen_sh9_downsample);
            VkPipelineLayout pipeline_layout = pipelinelayout_list.Get(_gen_sh9_downsample);

            vkCmdBindPipeline(_sh9_cmd_buf_downsample, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
            vkCmdBindDescriptorSets(_sh9_cmd_buf_downsample, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_layout, 0, 1, &descriptor_set_sh9_downsample, 0, 0);

            uint32_t groupSize = 2;
            uint32_t groupCountX = (dst_tex_width + groupSize - 1) / groupSize;
            uint32_t groupCountY = (dst_tex_height + groupSize - 1) / groupSize;

            int push_constants[4] = { src_tex_width, src_tex_height, dst_tex_width, dst_tex_height };
            vkCmdPushConstants(_sh9_cmd_buf_downsample, pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push_constants), push_constants);

            vkCmdDispatch(_sh9_cmd_buf_downsample, groupCountX, groupCountY, 6);

            vkEndCommandBuffer(_sh9_cmd_buf_downsample);

            VkPipelineStageFlags stage_flags = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;

            VkSubmitInfo submit_info = {};
            submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            
            submit_info.waitSemaphoreCount = 1;
            submit_info.pWaitSemaphores = &_buffers_semaphores[i];
            submit_info.pWaitDstStageMask = &stage_flags;

            submit_info.signalSemaphoreCount = 1;
            submit_info.pSignalSemaphores = &_buffers_semaphores[i + 1];
            submit_info.pCommandBuffers = &_sh9_cmd_buf_downsample;
            submit_info.commandBufferCount = 1;

            VkQueue compute_queue;
            vkGetDeviceQueue(_vulkan_device->logicalDevice, _vulkan_device->queueFamilyIndices.compute, 0, &compute_queue);
            VK_CHECK_RESULT(vkQueueSubmit(compute_queue, 1, &submit_info, VK_NULL_HANDLE));

            vkQueueWaitIdle(compute_queue);
        }

        // Final integral
        {
            // update sh9 desc set
            VkWriteDescriptorSet writeDescriptorSets[2] = {
                vks::initializers::writeDescriptorSet(descriptor_set_sh9_downsample, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 0, &_sh9_buffers[num_mips - 1].descriptor),
                vks::initializers::writeDescriptorSet(descriptor_set_sh9_downsample, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, &sh9_buffer.descriptor)
            };

            vkUpdateDescriptorSets(_vulkan_device->logicalDevice, sizeof(writeDescriptorSets) / sizeof(VkWriteDescriptorSet), writeDescriptorSets, 0, NULL);

            VkCommandBufferBeginInfo cmd_buf_info = vks::initializers::commandBufferBeginInfo();
            VK_CHECK_RESULT(vkBeginCommandBuffer(_sh9_cmd_buf_downsample, &cmd_buf_info));

            VkPipeline pipeline = pipeline_list.Get(_gen_sh9_final);
            VkPipelineLayout pipeline_layout = pipelinelayout_list.Get(_gen_sh9_downsample);

            vkCmdBindPipeline(_sh9_cmd_buf_downsample, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
            vkCmdBindDescriptorSets(_sh9_cmd_buf_downsample, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_layout, 0, 1, &descriptor_set_sh9_downsample, 0, 0);
            
            int push_constants[4] = { probe_idx, 0, 0, 0 };
            vkCmdPushConstants(_sh9_cmd_buf_downsample, pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push_constants), push_constants);

            vkCmdDispatch(_sh9_cmd_buf_downsample, 1, 1, 1);

            vkEndCommandBuffer(_sh9_cmd_buf_downsample);

            VkPipelineStageFlags stage_flags = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;

            VkSubmitInfo submit_info = {};
            submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

            submit_info.waitSemaphoreCount = 1;
            submit_info.pWaitSemaphores = &_buffers_semaphores[num_mips - 1];
            submit_info.pWaitDstStageMask = &stage_flags;

            submit_info.signalSemaphoreCount = 0;
            submit_info.pCommandBuffers = &_sh9_cmd_buf_downsample;
            submit_info.commandBufferCount = 1;

            VkQueue compute_queue;
            vkGetDeviceQueue(_vulkan_device->logicalDevice, _vulkan_device->queueFamilyIndices.compute, 0, &compute_queue);
            VK_CHECK_RESULT(vkQueueSubmit(compute_queue, 1, &submit_info, VK_NULL_HANDLE));

            vkQueueWaitIdle(compute_queue);
        }
    }

    void GenerateIrradianceSH9Coefficients(vks::Texture const& tex, vks::Buffer sh9_buffer, int probe_idx, std::vector<VkSemaphore>& semaphores) {
        AllocateSH9ProjectResources(tex);
        AllocateSH9DownsampleResources(tex);

        CubemapSH9ProjectPass(tex, semaphores);
        CubemapSH9IntegratePass(tex, sh9_buffer, probe_idx);
    }

    void GeneratePrefilteredCube() {
        vks::TextureCubeMap prefiltered_cube;

        const VkFormat format = VK_FORMAT_R16G16B16A16_SFLOAT;
        const int32_t dim = 512;
        const uint32_t numMips = static_cast<uint32_t>(floor(log2(dim))) + 1;

        // Pre-filtered cube map
        // Image
        VkImageCreateInfo image_create_info = vks::initializers::imageCreateInfo();
        image_create_info.imageType = VK_IMAGE_TYPE_2D;
        image_create_info.format = format;
        image_create_info.extent.width = dim;
        image_create_info.extent.height = dim;
        image_create_info.extent.depth = 1;
        image_create_info.mipLevels = numMips;
        image_create_info.arrayLayers = 6;
        image_create_info.samples = VK_SAMPLE_COUNT_1_BIT;
        image_create_info.tiling = VK_IMAGE_TILING_OPTIMAL;
        image_create_info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        image_create_info.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
        VK_CHECK_RESULT(vkCreateImage(_vulkan_device->logicalDevice, &image_create_info, nullptr, &prefiltered_cube.image));

        VkMemoryAllocateInfo memAlloc = vks::initializers::memoryAllocateInfo();
        VkMemoryRequirements memReqs;
        vkGetImageMemoryRequirements(_vulkan_device->logicalDevice, prefiltered_cube.image, &memReqs);
        memAlloc.allocationSize = memReqs.size;
        memAlloc.memoryTypeIndex = _vulkan_device->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        VK_CHECK_RESULT(vkAllocateMemory(_vulkan_device->logicalDevice, &memAlloc, nullptr, &prefiltered_cube.deviceMemory));
        VK_CHECK_RESULT(vkBindImageMemory(_vulkan_device->logicalDevice, prefiltered_cube.image, prefiltered_cube.deviceMemory, 0));

        // Image view
        VkImageViewCreateInfo view_create_info = vks::initializers::imageViewCreateInfo();
        view_create_info.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
        view_create_info.format = format;
        view_create_info.subresourceRange = {};
        view_create_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        view_create_info.subresourceRange.levelCount = numMips;
        view_create_info.subresourceRange.layerCount = 6;
        view_create_info.image = prefiltered_cube.image;
        VK_CHECK_RESULT(vkCreateImageView(_vulkan_device->logicalDevice, &view_create_info, nullptr, &prefiltered_cube.view));

        // Sampler
        VkSamplerCreateInfo sampler_create_info = vks::initializers::samplerCreateInfo();
        sampler_create_info.magFilter = VK_FILTER_LINEAR;
        sampler_create_info.minFilter = VK_FILTER_LINEAR;
        sampler_create_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        sampler_create_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler_create_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler_create_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler_create_info.minLod = 0.0f;
        sampler_create_info.maxLod = static_cast<float>(numMips);
        sampler_create_info.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
        VK_CHECK_RESULT(vkCreateSampler(_vulkan_device->logicalDevice, &sampler_create_info, nullptr, &prefiltered_cube.sampler));

        prefiltered_cube.descriptor.imageView = prefiltered_cube.view;
        prefiltered_cube.descriptor.sampler = prefiltered_cube.sampler;
        prefiltered_cube.descriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        prefiltered_cube.device = _vulkan_device;

        VkAttachmentDescription attributes_description = {};
        // Color attachment
        attributes_description.format = format;
        attributes_description.samples = VK_SAMPLE_COUNT_1_BIT;
        attributes_description.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attributes_description.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attributes_description.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attributes_description.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attributes_description.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attributes_description.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        VkAttachmentReference color_reference = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };

        VkSubpassDescription subpass_description = {};
        subpass_description.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass_description.colorAttachmentCount = 1;
        subpass_description.pColorAttachments = &color_reference;

        // Use subpass dependencies for layout transitions
        std::array<VkSubpassDependency, 2> dependencies;
        dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[0].dstSubpass = 0;
        dependencies[0].srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
        dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependencies[0].srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
        dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
        dependencies[1].srcSubpass = 0;
        dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependencies[1].dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
        dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        dependencies[1].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
        dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

        // Renderpass
        VkRenderPassCreateInfo render_pass_create_info = vks::initializers::renderPassCreateInfo();
        render_pass_create_info.attachmentCount = 1;
        render_pass_create_info.pAttachments = &attributes_description;
        render_pass_create_info.subpassCount = 1;
        render_pass_create_info.pSubpasses = &subpass_description;
        render_pass_create_info.dependencyCount = 2;
        render_pass_create_info.pDependencies = dependencies.data();
        VkRenderPass renderpass;
        VK_CHECK_RESULT(vkCreateRenderPass(_vulkan_device->logicalDevice, &render_pass_create_info, nullptr, &renderpass));

        struct {
            VkImage image;
            VkImageView view;
            VkDeviceMemory memory;
            VkFramebuffer framebuffer;
        } offscreen;

        // Offscreen framebuffer
        {
            // Color attachment
            VkImageCreateInfo image_create_info = vks::initializers::imageCreateInfo();
            image_create_info.imageType = VK_IMAGE_TYPE_2D;
            image_create_info.format = format;
            image_create_info.extent.width = dim;
            image_create_info.extent.height = dim;
            image_create_info.extent.depth = 1;
            image_create_info.mipLevels = 1;
            image_create_info.arrayLayers = 1;
            image_create_info.samples = VK_SAMPLE_COUNT_1_BIT;
            image_create_info.tiling = VK_IMAGE_TILING_OPTIMAL;
            image_create_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            image_create_info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
            image_create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            VK_CHECK_RESULT(vkCreateImage(_vulkan_device->logicalDevice, &image_create_info, nullptr, &offscreen.image));

            VkMemoryAllocateInfo memAlloc = vks::initializers::memoryAllocateInfo();
            VkMemoryRequirements memReqs;
            vkGetImageMemoryRequirements(_vulkan_device->logicalDevice, offscreen.image, &memReqs);
            memAlloc.allocationSize = memReqs.size;
            memAlloc.memoryTypeIndex = _vulkan_device->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            VK_CHECK_RESULT(vkAllocateMemory(_vulkan_device->logicalDevice, &memAlloc, nullptr, &offscreen.memory));
            VK_CHECK_RESULT(vkBindImageMemory(_vulkan_device->logicalDevice, offscreen.image, offscreen.memory, 0));

            VkImageViewCreateInfo color_image_view = vks::initializers::imageViewCreateInfo();
            color_image_view.viewType = VK_IMAGE_VIEW_TYPE_2D;
            color_image_view.format = format;
            color_image_view.flags = 0;
            color_image_view.subresourceRange = {};
            color_image_view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            color_image_view.subresourceRange.baseMipLevel = 0;
            color_image_view.subresourceRange.levelCount = 1;
            color_image_view.subresourceRange.baseArrayLayer = 0;
            color_image_view.subresourceRange.layerCount = 1;
            color_image_view.image = offscreen.image;
            VK_CHECK_RESULT(vkCreateImageView(_vulkan_device->logicalDevice, &color_image_view, nullptr, &offscreen.view));

            VkFramebufferCreateInfo framebuffer_create_info = vks::initializers::framebufferCreateInfo();
            framebuffer_create_info.renderPass = renderpass;
            framebuffer_create_info.attachmentCount = 1;
            framebuffer_create_info.pAttachments = &offscreen.view;
            framebuffer_create_info.width = dim;
            framebuffer_create_info.height = dim;
            framebuffer_create_info.layers = 1;
            VK_CHECK_RESULT(vkCreateFramebuffer(_vulkan_device->logicalDevice, &framebuffer_create_info, nullptr, &offscreen.framebuffer));

            VkCommandBuffer layout_cmd = _vulkan_device->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);;
            vks::tools::setImageLayout(layout_cmd, offscreen.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
            _vulkan_device->flushCommandBuffer(layout_cmd, _queue, true);
        }

        // Descriptors
        VkDescriptorSetLayout descriptorsetlayout;
        std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings = {
            vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 0),
        };
        VkDescriptorSetLayoutCreateInfo descriptorsetlayoutCI = vks::initializers::descriptorSetLayoutCreateInfo(setLayoutBindings);
        VK_CHECK_RESULT(vkCreateDescriptorSetLayout(_vulkan_device->logicalDevice, &descriptorsetlayoutCI, nullptr, &descriptorsetlayout));

        // Descriptor Pool
        std::vector<VkDescriptorPoolSize> poolSizes = { vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1) };
        VkDescriptorPoolCreateInfo descriptorPoolCI = vks::initializers::descriptorPoolCreateInfo(poolSizes, 2);
        VkDescriptorPool descriptorpool;
        VK_CHECK_RESULT(vkCreateDescriptorPool(_vulkan_device->logicalDevice, &descriptorPoolCI, nullptr, &descriptorpool));

        TextureList& texture_list = _resources->GetTextures();
        vks::Texture const& env_texture = texture_list.Get(STATIC_CRC32("EnvMap"));

        // Descriptor sets
        VkDescriptorSet descriptorset;
        VkDescriptorSetAllocateInfo allocInfo = vks::initializers::descriptorSetAllocateInfo(descriptorpool, &descriptorsetlayout, 1);
        VK_CHECK_RESULT(vkAllocateDescriptorSets(_vulkan_device->logicalDevice, &allocInfo, &descriptorset));
        VkWriteDescriptorSet writeDescriptorSet = vks::initializers::writeDescriptorSet(descriptorset, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 0, const_cast<VkDescriptorImageInfo*>(&env_texture.descriptor));
        vkUpdateDescriptorSets(_vulkan_device->logicalDevice, 1, &writeDescriptorSet, 0, nullptr);

        // Pipeline layout
        struct PushBlock {
            glm::mat4 mvp;
            float roughness;
            uint32_t numSamples = 32u;
        } pushBlock;

        VkPipelineLayout pipelinelayout;
        std::vector<VkPushConstantRange> pushConstantRanges = {
            vks::initializers::pushConstantRange(VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(PushBlock), 0),
        };
        VkPipelineLayoutCreateInfo pipelineLayoutCI = vks::initializers::pipelineLayoutCreateInfo(&descriptorsetlayout, 1);
        pipelineLayoutCI.pushConstantRangeCount = 1;
        pipelineLayoutCI.pPushConstantRanges = pushConstantRanges.data();
        VK_CHECK_RESULT(vkCreatePipelineLayout(_vulkan_device->logicalDevice, &pipelineLayoutCI, nullptr, &pipelinelayout));

        // Pipeline
        VkPipelineInputAssemblyStateCreateInfo inputAssemblyState = vks::initializers::pipelineInputAssemblyStateCreateInfo(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 0, VK_FALSE);
        VkPipelineRasterizationStateCreateInfo rasterizationState = vks::initializers::pipelineRasterizationStateCreateInfo(VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE);
        VkPipelineColorBlendAttachmentState blendAttachmentState = vks::initializers::pipelineColorBlendAttachmentState(0xf, VK_FALSE);
        VkPipelineColorBlendStateCreateInfo colorBlendState = vks::initializers::pipelineColorBlendStateCreateInfo(1, &blendAttachmentState);
        VkPipelineDepthStencilStateCreateInfo depthStencilState = vks::initializers::pipelineDepthStencilStateCreateInfo(VK_FALSE, VK_FALSE, VK_COMPARE_OP_LESS_OR_EQUAL);
        VkPipelineViewportStateCreateInfo viewportState = vks::initializers::pipelineViewportStateCreateInfo(1, 1);
        VkPipelineMultisampleStateCreateInfo multisampleState = vks::initializers::pipelineMultisampleStateCreateInfo(VK_SAMPLE_COUNT_1_BIT);
        std::vector<VkDynamicState> dynamicStateEnables = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
        VkPipelineDynamicStateCreateInfo dynamicState = vks::initializers::pipelineDynamicStateCreateInfo(dynamicStateEnables);
        // Vertex input state
        VkVertexInputBindingDescription vertexInputBinding = vks::initializers::vertexInputBindingDescription(0, _vertex_layout.stride(), VK_VERTEX_INPUT_RATE_VERTEX);
        VkVertexInputAttributeDescription vertexInputAttribute = vks::initializers::vertexInputAttributeDescription(0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0);

        VkPipelineVertexInputStateCreateInfo vertexInputState = vks::initializers::pipelineVertexInputStateCreateInfo();
        vertexInputState.vertexBindingDescriptionCount = 1;
        vertexInputState.pVertexBindingDescriptions = &vertexInputBinding;
        vertexInputState.vertexAttributeDescriptionCount = 1;
        vertexInputState.pVertexAttributeDescriptions = &vertexInputAttribute;

        PipelineList& pipeline_list = _resources->GetPipepineList();
        ShaderList& shader_list = _resources->GetShaderList();

        std::array<VkPipelineShaderStageCreateInfo, 2> shader_stages = {
            shader_list.Load("../Baikal/Kernels/VK/shaders/filtercube.vert.spv", VK_SHADER_STAGE_VERTEX_BIT),
            shader_list.Load("../Baikal/Kernels/VK/shaders/prefilterenvmap.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT) };

        VkGraphicsPipelineCreateInfo pipeline_create_info = vks::initializers::pipelineCreateInfo(pipelinelayout, renderpass);
        pipeline_create_info.pInputAssemblyState = &inputAssemblyState;
        pipeline_create_info.pRasterizationState = &rasterizationState;
        pipeline_create_info.pColorBlendState = &colorBlendState;
        pipeline_create_info.pMultisampleState = &multisampleState;
        pipeline_create_info.pViewportState = &viewportState;
        pipeline_create_info.pDepthStencilState = &depthStencilState;
        pipeline_create_info.pDynamicState = &dynamicState;
        pipeline_create_info.stageCount = 2;
        pipeline_create_info.pStages = shader_stages.data();
        pipeline_create_info.pVertexInputState = &vertexInputState;
        pipeline_create_info.renderPass = renderpass;

        VkPipeline pipeline;
        VK_CHECK_RESULT(vkCreateGraphicsPipelines(_vulkan_device->logicalDevice, pipeline_list.GetPipelineCache(), 1, &pipeline_create_info, nullptr, &pipeline));

        // Render
        VkClearValue clearValues[1];
        clearValues[0].color = { { 0.0f, 0.0f, 0.2f, 0.0f } };

        VkRenderPassBeginInfo renderPassBeginInfo = vks::initializers::renderPassBeginInfo();
        // Reuse render pass from example pass
        renderPassBeginInfo.renderPass = renderpass;
        renderPassBeginInfo.framebuffer = offscreen.framebuffer;
        renderPassBeginInfo.renderArea.extent.width = dim;
        renderPassBeginInfo.renderArea.extent.height = dim;
        renderPassBeginInfo.clearValueCount = 1;
        renderPassBeginInfo.pClearValues = clearValues;

        std::vector<glm::mat4> matrices = {
            glm::rotate(glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(0.0f, 1.0f, 0.0f)), glm::radians(180.0f), glm::vec3(1.0f, 0.0f, 0.0f)),
            glm::rotate(glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f), glm::vec3(0.0f, 1.0f, 0.0f)), glm::radians(180.0f), glm::vec3(1.0f, 0.0f, 0.0f)),
            glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f), glm::vec3(1.0f, 0.0f, 0.0f)),
            glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f)),
            glm::rotate(glm::mat4(1.0f), glm::radians(180.0f), glm::vec3(1.0f, 0.0f, 0.0f)),
            glm::rotate(glm::mat4(1.0f), glm::radians(180.0f), glm::vec3(0.0f, 0.0f, 1.0f)),
        };

        VkCommandBuffer cmd_buf = _vulkan_device->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);

        VkViewport viewport = vks::initializers::viewport((float)dim, (float)dim, 0.0f, 1.0f);
        VkRect2D scissor = vks::initializers::rect2D(dim, dim, 0, 0);

        vkCmdSetViewport(cmd_buf, 0, 1, &viewport);
        vkCmdSetScissor(cmd_buf, 0, 1, &scissor);

        VkImageSubresourceRange subresource_range = {};
        subresource_range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        subresource_range.baseMipLevel = 0;
        subresource_range.levelCount = numMips;
        subresource_range.layerCount = 6;

        // Change image layout for all cubemap faces to transfer destination
        vks::tools::setImageLayout(cmd_buf, prefiltered_cube.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, subresource_range);

        for (uint32_t m = 0; m < numMips; m++) {
            pushBlock.roughness = (float)m / (float)(numMips - 1);
            for (uint32_t f = 0; f < 6; f++) {
                viewport.width = static_cast<float>(dim * std::pow(0.5f, m));
                viewport.height = static_cast<float>(dim * std::pow(0.5f, m));
                vkCmdSetViewport(cmd_buf, 0, 1, &viewport);

                // Render scene from cube face's point of view
                vkCmdBeginRenderPass(cmd_buf, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

                // Update shader push constant block
                pushBlock.mvp = glm::perspective((float)(M_PI / 2.0), 1.0f, 0.1f, 512.0f) * matrices[f];

                vkCmdPushConstants(cmd_buf, pipelinelayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushBlock), &pushBlock);

                vkCmdBindPipeline(cmd_buf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
                vkCmdBindDescriptorSets(cmd_buf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelinelayout, 0, 1, &descriptorset, 0, NULL);

                VkDeviceSize offsets[1] = { 0 };

                vkCmdBindVertexBuffers(cmd_buf, 0, 1, &_cube.vertices.buffer, offsets);
                vkCmdBindIndexBuffer(cmd_buf, _cube.indices.buffer, 0, VK_INDEX_TYPE_UINT32);
                vkCmdDrawIndexed(cmd_buf, _cube.indexCount, 1, 0, 0, 0);

                vkCmdEndRenderPass(cmd_buf);

                vks::tools::setImageLayout(
                    cmd_buf,
                    offscreen.image,
                    VK_IMAGE_ASPECT_COLOR_BIT,
                    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

                // Copy region for transfer from framebuffer to cube face
                VkImageCopy copy_region = {};

                copy_region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                copy_region.srcSubresource.baseArrayLayer = 0;
                copy_region.srcSubresource.mipLevel = 0;
                copy_region.srcSubresource.layerCount = 1;
                copy_region.srcOffset = { 0, 0, 0 };

                copy_region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                copy_region.dstSubresource.baseArrayLayer = f;
                copy_region.dstSubresource.mipLevel = m;
                copy_region.dstSubresource.layerCount = 1;
                copy_region.dstOffset = { 0, 0, 0 };

                copy_region.extent.width = static_cast<uint32_t>(viewport.width);
                copy_region.extent.height = static_cast<uint32_t>(viewport.height);
                copy_region.extent.depth = 1;

                vkCmdCopyImage(cmd_buf, offscreen.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, prefiltered_cube.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy_region);

                // Transform framebuffer color attachment back 
                vks::tools::setImageLayout(cmd_buf, offscreen.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
            }
        }

        vks::tools::setImageLayout(
            cmd_buf,
            prefiltered_cube.image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            subresource_range);

        _vulkan_device->flushCommandBuffer(cmd_buf, _queue);

        vkDestroyRenderPass(_vulkan_device->logicalDevice, renderpass, nullptr);
        vkDestroyFramebuffer(_vulkan_device->logicalDevice, offscreen.framebuffer, nullptr);
        vkFreeMemory(_vulkan_device->logicalDevice, offscreen.memory, nullptr);
        vkDestroyImageView(_vulkan_device->logicalDevice, offscreen.view, nullptr);
        vkDestroyImage(_vulkan_device->logicalDevice, offscreen.image, nullptr);
        vkDestroyDescriptorPool(_vulkan_device->logicalDevice, descriptorpool, nullptr);
        vkDestroyDescriptorSetLayout(_vulkan_device->logicalDevice, descriptorsetlayout, nullptr);
        vkDestroyPipeline(_vulkan_device->logicalDevice, pipeline, nullptr);
        vkDestroyPipelineLayout(_vulkan_device->logicalDevice, pipelinelayout, nullptr);

        texture_list.Set(STATIC_CRC32("PrefilteredCube"), prefiltered_cube);
    }

    void GenerateBRDFLUT() {
        vks::Texture lut_brdf;

        const VkFormat format = VK_FORMAT_R16G16_SFLOAT;
        const int32_t dim = 512;

        // Image
        VkImageCreateInfo image_create_info = vks::initializers::imageCreateInfo();
        image_create_info.imageType = VK_IMAGE_TYPE_2D;
        image_create_info.format = format;
        image_create_info.extent.width = dim;
        image_create_info.extent.height = dim;
        image_create_info.extent.depth = 1;
        image_create_info.mipLevels = 1;
        image_create_info.arrayLayers = 1;
        image_create_info.samples = VK_SAMPLE_COUNT_1_BIT;
        image_create_info.tiling = VK_IMAGE_TILING_OPTIMAL;
        image_create_info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        VK_CHECK_RESULT(vkCreateImage(_vulkan_device->logicalDevice, &image_create_info, nullptr, &lut_brdf.image));

        VkMemoryAllocateInfo memAlloc = vks::initializers::memoryAllocateInfo();
        VkMemoryRequirements memReqs;
        vkGetImageMemoryRequirements(_vulkan_device->logicalDevice, lut_brdf.image, &memReqs);
        memAlloc.allocationSize = memReqs.size;
        memAlloc.memoryTypeIndex = _vulkan_device->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        VK_CHECK_RESULT(vkAllocateMemory(_vulkan_device->logicalDevice, &memAlloc, nullptr, &lut_brdf.deviceMemory));
        VK_CHECK_RESULT(vkBindImageMemory(_vulkan_device->logicalDevice, lut_brdf.image, lut_brdf.deviceMemory, 0));

        // Image view
        VkImageViewCreateInfo view_create_info = vks::initializers::imageViewCreateInfo();
        view_create_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view_create_info.format = format;
        view_create_info.subresourceRange = {};
        view_create_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        view_create_info.subresourceRange.levelCount = 1;
        view_create_info.subresourceRange.layerCount = 1;
        view_create_info.image = lut_brdf.image;
        VK_CHECK_RESULT(vkCreateImageView(_vulkan_device->logicalDevice, &view_create_info, nullptr, &lut_brdf.view));

        // Sampler
        VkSamplerCreateInfo sampler_create_info = vks::initializers::samplerCreateInfo();
        sampler_create_info.magFilter = VK_FILTER_LINEAR;
        sampler_create_info.minFilter = VK_FILTER_LINEAR;
        sampler_create_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        sampler_create_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler_create_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler_create_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler_create_info.minLod = 0.0f;
        sampler_create_info.maxLod = 1.0f;
        sampler_create_info.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
        VK_CHECK_RESULT(vkCreateSampler(_vulkan_device->logicalDevice, &sampler_create_info, nullptr, &lut_brdf.sampler));

        lut_brdf.descriptor.imageView = lut_brdf.view;
        lut_brdf.descriptor.sampler = lut_brdf.sampler;
        lut_brdf.descriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        lut_brdf.device = _vulkan_device;

        VkAttachmentDescription attributes_description = {};
        // Color attachment
        attributes_description.format = format;
        attributes_description.samples = VK_SAMPLE_COUNT_1_BIT;
        attributes_description.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attributes_description.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attributes_description.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attributes_description.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attributes_description.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attributes_description.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        VkAttachmentReference color_reference = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };

        VkSubpassDescription subpass_description = {};
        subpass_description.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass_description.colorAttachmentCount = 1;
        subpass_description.pColorAttachments = &color_reference;

        // Use subpass dependencies for layout transitions
        std::array<VkSubpassDependency, 2> dependencies;
        dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[0].dstSubpass = 0;
        dependencies[0].srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
        dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependencies[0].srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
        dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
        dependencies[1].srcSubpass = 0;
        dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependencies[1].dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
        dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        dependencies[1].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
        dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

        // Create the actual renderpass
        VkRenderPassCreateInfo renderPassCI = vks::initializers::renderPassCreateInfo();
        renderPassCI.attachmentCount = 1;
        renderPassCI.pAttachments = &attributes_description;
        renderPassCI.subpassCount = 1;
        renderPassCI.pSubpasses = &subpass_description;
        renderPassCI.dependencyCount = 2;
        renderPassCI.pDependencies = dependencies.data();

        VkRenderPass renderpass;
        VK_CHECK_RESULT(vkCreateRenderPass(_vulkan_device->logicalDevice, &renderPassCI, nullptr, &renderpass));

        VkFramebufferCreateInfo framebuffer_create_info = vks::initializers::framebufferCreateInfo();
        framebuffer_create_info.renderPass = renderpass;
        framebuffer_create_info.attachmentCount = 1;
        framebuffer_create_info.pAttachments = &lut_brdf.view;
        framebuffer_create_info.width = dim;
        framebuffer_create_info.height = dim;
        framebuffer_create_info.layers = 1;

        VkFramebuffer framebuffer;
        VK_CHECK_RESULT(vkCreateFramebuffer(_vulkan_device->logicalDevice, &framebuffer_create_info, nullptr, &framebuffer));

        // Desriptors
        VkDescriptorSetLayout descriptorsetlayout;
        std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings = {};
        VkDescriptorSetLayoutCreateInfo descriptorsetlayoutCI = vks::initializers::descriptorSetLayoutCreateInfo(setLayoutBindings);
        VK_CHECK_RESULT(vkCreateDescriptorSetLayout(_vulkan_device->logicalDevice, &descriptorsetlayoutCI, nullptr, &descriptorsetlayout));

        // Descriptor Pool
        std::vector<VkDescriptorPoolSize> poolSizes = { vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1) };
        VkDescriptorPoolCreateInfo descriptorPoolCI = vks::initializers::descriptorPoolCreateInfo(poolSizes, 2);
        VkDescriptorPool descriptorpool;
        VK_CHECK_RESULT(vkCreateDescriptorPool(_vulkan_device->logicalDevice, &descriptorPoolCI, nullptr, &descriptorpool));

        // Descriptor sets
        VkDescriptorSet descriptorset;
        VkDescriptorSetAllocateInfo allocInfo = vks::initializers::descriptorSetAllocateInfo(descriptorpool, &descriptorsetlayout, 1);
        VK_CHECK_RESULT(vkAllocateDescriptorSets(_vulkan_device->logicalDevice, &allocInfo, &descriptorset));

        // Pipeline layout
        VkPipelineLayout pipelinelayout;
        VkPipelineLayoutCreateInfo pipelineLayoutCI = vks::initializers::pipelineLayoutCreateInfo(&descriptorsetlayout, 1);
        VK_CHECK_RESULT(vkCreatePipelineLayout(_vulkan_device->logicalDevice, &pipelineLayoutCI, nullptr, &pipelinelayout));

        // Pipeline
        VkPipelineInputAssemblyStateCreateInfo inputAssemblyState = vks::initializers::pipelineInputAssemblyStateCreateInfo(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 0, VK_FALSE);
        VkPipelineRasterizationStateCreateInfo rasterizationState = vks::initializers::pipelineRasterizationStateCreateInfo(VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE);
        VkPipelineColorBlendAttachmentState blendAttachmentState = vks::initializers::pipelineColorBlendAttachmentState(0xf, VK_FALSE);
        VkPipelineColorBlendStateCreateInfo colorBlendState = vks::initializers::pipelineColorBlendStateCreateInfo(1, &blendAttachmentState);
        VkPipelineDepthStencilStateCreateInfo depthStencilState = vks::initializers::pipelineDepthStencilStateCreateInfo(VK_FALSE, VK_FALSE, VK_COMPARE_OP_LESS_OR_EQUAL);
        VkPipelineViewportStateCreateInfo viewportState = vks::initializers::pipelineViewportStateCreateInfo(1, 1);
        VkPipelineMultisampleStateCreateInfo multisampleState = vks::initializers::pipelineMultisampleStateCreateInfo(VK_SAMPLE_COUNT_1_BIT);
        std::vector<VkDynamicState> dynamicStateEnables = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
        VkPipelineDynamicStateCreateInfo dynamicState = vks::initializers::pipelineDynamicStateCreateInfo(dynamicStateEnables);
        VkPipelineVertexInputStateCreateInfo emptyInputState = vks::initializers::pipelineVertexInputStateCreateInfo();

        PipelineList& pipeline_list = _resources->GetPipepineList();
        ShaderList& shader_list = _resources->GetShaderList();

        // Look-up-table (from BRDF) pipeline
        std::array<VkPipelineShaderStageCreateInfo, 2> shader_stages = {
            shader_list.Load("../Baikal/Kernels/VK/shaders/genbrdflut.vert.spv", VK_SHADER_STAGE_VERTEX_BIT),
            shader_list.Load("../Baikal/Kernels/VK/shaders/genbrdflut.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT) };

        VkGraphicsPipelineCreateInfo pipeline_create_info = vks::initializers::pipelineCreateInfo(pipelinelayout, renderpass);
        pipeline_create_info.pInputAssemblyState = &inputAssemblyState;
        pipeline_create_info.pRasterizationState = &rasterizationState;
        pipeline_create_info.pColorBlendState = &colorBlendState;
        pipeline_create_info.pMultisampleState = &multisampleState;
        pipeline_create_info.pViewportState = &viewportState;
        pipeline_create_info.pDepthStencilState = &depthStencilState;
        pipeline_create_info.pDynamicState = &dynamicState;
        pipeline_create_info.stageCount = 2;
        pipeline_create_info.pStages = shader_stages.data();
        pipeline_create_info.pVertexInputState = &emptyInputState;

        VkPipeline pipeline;
        VK_CHECK_RESULT(vkCreateGraphicsPipelines(_vulkan_device->logicalDevice, pipeline_list.GetPipelineCache(), 1, &pipeline_create_info, nullptr, &pipeline));

        // Render
        VkClearValue clearValues[1];
        clearValues[0].color = { { 0.0f, 0.0f, 0.0f, 1.0f } };

        VkRenderPassBeginInfo renderPassBeginInfo = vks::initializers::renderPassBeginInfo();
        renderPassBeginInfo.renderPass = renderpass;
        renderPassBeginInfo.renderArea.extent.width = dim;
        renderPassBeginInfo.renderArea.extent.height = dim;
        renderPassBeginInfo.clearValueCount = 1;
        renderPassBeginInfo.pClearValues = clearValues;
        renderPassBeginInfo.framebuffer = framebuffer;

        VkCommandBuffer cmdBuf = _vulkan_device->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);
        vkCmdBeginRenderPass(cmdBuf, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
        VkViewport viewport = vks::initializers::viewport((float)dim, (float)dim, 0.0f, 1.0f);
        VkRect2D scissor = vks::initializers::rect2D(dim, dim, 0, 0);
        vkCmdSetViewport(cmdBuf, 0, 1, &viewport);
        vkCmdSetScissor(cmdBuf, 0, 1, &scissor);
        vkCmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        vkCmdDraw(cmdBuf, 3, 1, 0, 0);
        vkCmdEndRenderPass(cmdBuf);
        _vulkan_device->flushCommandBuffer(cmdBuf, _queue);

        vkQueueWaitIdle(_queue);

        vkDestroyPipeline(_vulkan_device->logicalDevice, pipeline, nullptr);
        vkDestroyPipelineLayout(_vulkan_device->logicalDevice, pipelinelayout, nullptr);
        vkDestroyRenderPass(_vulkan_device->logicalDevice, renderpass, nullptr);
        vkDestroyFramebuffer(_vulkan_device->logicalDevice, framebuffer, nullptr);
        vkDestroyDescriptorSetLayout(_vulkan_device->logicalDevice, descriptorsetlayout, nullptr);
        vkDestroyDescriptorPool(_vulkan_device->logicalDevice, descriptorpool, nullptr);

        TextureList& texture_list = _resources->GetTextures();
        texture_list.Set(STATIC_CRC32("brdfLUT"), lut_brdf);
    }

protected:
    vks::Model _cube;
    vks::VulkanDevice* _vulkan_device;
    VkQueue _queue;
    ResourceManager* _resources;
    std::vector<vks::Buffer> _sh9_buffers;
    std::vector<VkDescriptorSet> _sh9_descriptor_sets;
    std::vector<VkSemaphore> _buffers_semaphores;

    const uint32_t _sh9_buffer_name = STATIC_CRC32("SH9Buffer");
    const uint32_t _gen_sh9_project = STATIC_CRC32("SH9Project");
    const uint32_t _gen_sh9_downsample = STATIC_CRC32("SH9Downsample");
    const uint32_t _gen_sh9_final = STATIC_CRC32("SH9Final");

    VkCommandBuffer _sh9_cmd_buf;
    VkCommandBuffer _sh9_cmd_buf_project;
    VkCommandBuffer _sh9_cmd_buf_downsample;

    vks::VertexLayout _vertex_layout = vks::VertexLayout({ vks::VERTEX_COMPONENT_POSITION, vks::VERTEX_COMPONENT_NORMAL, vks::VERTEX_COMPONENT_UV });
};