#pragma once

#include "vkoutput.h"
#include "Vulkan/VulkanFrameBuffer.hpp"

#define SHADOWMAP_DIM 2048
#define SHADOWMAP_FORMAT VK_FORMAT_D32_SFLOAT_S8_UINT

namespace Baikal
{
        VkOutput::VkOutput(vks::VulkanDevice* vulkan_device, std::uint32_t w, std::uint32_t h)
            : Output(w, h)
            , m_vulkan_device(vulkan_device)
        {
            ShadowSetup();
            DeferredSetup();
        }

        VkOutput::~VkOutput()
        {

        }

        void VkOutput::GetData(RadeonRays::float3* data) const
        {
        }
        void VkOutput::ShadowSetup()
        {
            for (int i = 0; i < LIGHT_COUNT; i++)
            {
                framebuffers.shadow[i] = new vks::Framebuffer(m_vulkan_device);

                framebuffers.shadow[i]->width = SHADOWMAP_DIM;
                framebuffers.shadow[i]->height = SHADOWMAP_DIM;

                vks::AttachmentCreateInfo attachmentInfo = {};
                attachmentInfo.format = SHADOWMAP_FORMAT;
                attachmentInfo.width = SHADOWMAP_DIM;
                attachmentInfo.height = SHADOWMAP_DIM;
                attachmentInfo.layerCount = 1;
                attachmentInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
                framebuffers.shadow[i]->addAttachment(attachmentInfo);

                VK_CHECK_RESULT(framebuffers.shadow[i]->createSampler(VK_FILTER_LINEAR, VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE));
                VK_CHECK_RESULT(framebuffers.shadow[i]->createRenderPass());
            }
        }

        // Prepare the framebuffer for offscreen rendering with multiple attachments used as render targets inside the fragment shaders
        void VkOutput::DeferredSetup()
        {
            framebuffers.deferred = new vks::Framebuffer(m_vulkan_device);

            framebuffers.deferred->width = width();
            framebuffers.deferred->height = height();

            // Attachments (3 color, 1 depth)
            vks::AttachmentCreateInfo attachmentInfo = {};
            attachmentInfo.width = width();
            attachmentInfo.height = height();
            attachmentInfo.layerCount = 1;
            attachmentInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

            // Color attachments
            // Attachment 0: Packed view space normals (RG16F), depth - bit z/w, mesh id - last 8 bits
            attachmentInfo.format = VK_FORMAT_R16G16B16A16_UINT;
            framebuffers.deferred->addAttachment(attachmentInfo);

            // Attachment 1: Albedo
            attachmentInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
            framebuffers.deferred->addAttachment(attachmentInfo);

            // Attachment 2: Motion, roughness, metaliness
            attachmentInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
            framebuffers.deferred->addAttachment(attachmentInfo);

            // Depth attachment
            // Find a suitable depth format
            VkFormat attDepthFormat;
            VkBool32 validDepthFormat = vks::tools::getSupportedDepthFormat(m_vulkan_device->physicalDevice, &attDepthFormat);
            assert(validDepthFormat);

            attachmentInfo.format = attDepthFormat;
            attachmentInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
            framebuffers.deferred->addAttachment(attachmentInfo);

            // Create sampler to sample from the color attachments
            VK_CHECK_RESULT(framebuffers.deferred->createSampler(VK_FILTER_NEAREST, VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE));

            // Create default renderpass for the framebuffer
            VK_CHECK_RESULT(framebuffers.deferred->createRenderPass());
        }
}
