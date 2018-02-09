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
        DrawSetup();
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

    void VkOutput::DrawSetup()
    {
        SetupRenderPass();
        SetupDepthStencil();
        SetupOutput();

        auto& device = m_vulkan_device->logicalDevice;
        VkImageView attachments[2];

        // Depth/Stencil attachment is the same for all frame buffers
        attachments[1] = m_depth_stencil.view;

        VkFramebufferCreateInfo frameBufferCreateInfo = {};
        frameBufferCreateInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        frameBufferCreateInfo.pNext = NULL;
        frameBufferCreateInfo.renderPass = framebuffers.draw_render_pass;
        frameBufferCreateInfo.attachmentCount = 2;
        frameBufferCreateInfo.pAttachments = attachments;
        frameBufferCreateInfo.width = width();
        frameBufferCreateInfo.height = height();
        frameBufferCreateInfo.layers = 1;

        // Create frame buffers for every swap chain image
        {
            attachments[0] = m_out.view;
            VK_CHECK_RESULT(vkCreateFramebuffer(device, &frameBufferCreateInfo, nullptr, &framebuffers.draw_fb));
        }
    }

    void VkOutput::SetupRenderPass()
    {
        auto& device = m_vulkan_device->logicalDevice;

        std::array<VkAttachmentDescription, 2> attachments = {};
        // Color attachment
        attachments[0].format = VK_FORMAT_B8G8R8A8_UNORM;
        attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
        attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachments[0].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        // Depth attachment
        attachments[1].format = VK_FORMAT_D32_SFLOAT_S8_UINT;
        attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
        attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkAttachmentReference colorReference = {};
        colorReference.attachment = 0;
        colorReference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkAttachmentReference depthReference = {};
        depthReference.attachment = 1;
        depthReference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpassDescription = {};
        subpassDescription.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpassDescription.colorAttachmentCount = 1;
        subpassDescription.pColorAttachments = &colorReference;
        subpassDescription.pDepthStencilAttachment = &depthReference;
        subpassDescription.inputAttachmentCount = 0;
        subpassDescription.pInputAttachments = nullptr;
        subpassDescription.preserveAttachmentCount = 0;
        subpassDescription.pPreserveAttachments = nullptr;
        subpassDescription.pResolveAttachments = nullptr;

        // Subpass dependencies for layout transitions
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

        VkRenderPassCreateInfo renderPassInfo = {};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
        renderPassInfo.pAttachments = attachments.data();
        renderPassInfo.subpassCount = 1;
        renderPassInfo.pSubpasses = &subpassDescription;
        renderPassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
        renderPassInfo.pDependencies = dependencies.data();

        VK_CHECK_RESULT(vkCreateRenderPass(device, &renderPassInfo, nullptr, &framebuffers.draw_render_pass));
    }

    void VkOutput::SetupDepthStencil()
    {
        VkFormat depth_format = VK_FORMAT_D32_SFLOAT_S8_UINT;

        VkImageCreateInfo image = {};
        image.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        image.pNext = NULL;
        image.imageType = VK_IMAGE_TYPE_2D;
        image.format = depth_format;
        image.extent = { (uint32_t)width(), (uint32_t)height(), 1 };
        image.mipLevels = 1;
        image.arrayLayers = 1;
        image.samples = VK_SAMPLE_COUNT_1_BIT;
        image.tiling = VK_IMAGE_TILING_OPTIMAL;
        image.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        image.flags = 0;

        VkMemoryAllocateInfo mem_alloc = {};
        mem_alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        mem_alloc.pNext = NULL;
        mem_alloc.allocationSize = 0;
        mem_alloc.memoryTypeIndex = 0;

        VkImageViewCreateInfo depthStencilView = {};
        depthStencilView.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        depthStencilView.pNext = NULL;
        depthStencilView.viewType = VK_IMAGE_VIEW_TYPE_2D;
        depthStencilView.format = depth_format;
        depthStencilView.flags = 0;
        depthStencilView.subresourceRange = {};
        depthStencilView.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
        depthStencilView.subresourceRange.baseMipLevel = 0;
        depthStencilView.subresourceRange.levelCount = 1;
        depthStencilView.subresourceRange.baseArrayLayer = 0;
        depthStencilView.subresourceRange.layerCount = 1;

        VkMemoryRequirements memReqs;

        auto device = m_vulkan_device->logicalDevice;
        VK_CHECK_RESULT(vkCreateImage(device, &image, nullptr, &m_depth_stencil.image));
        vkGetImageMemoryRequirements(device, m_depth_stencil.image, &memReqs);
        mem_alloc.allocationSize = memReqs.size;
        mem_alloc.memoryTypeIndex = m_vulkan_device->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        VK_CHECK_RESULT(vkAllocateMemory(device, &mem_alloc, nullptr, &m_depth_stencil.mem));
        VK_CHECK_RESULT(vkBindImageMemory(device, m_depth_stencil.image, m_depth_stencil.mem, 0));

        depthStencilView.image = m_depth_stencil.image;
        VK_CHECK_RESULT(vkCreateImageView(device, &depthStencilView, nullptr, &m_depth_stencil.view));
    }

    void VkOutput::SetupOutput()
    {
        VkImageAspectFlags aspectMask = 0;
        VkImageLayout imageLayout;

        //auto format = VK_FORMAT_D32_SFLOAT_S8_UINT;
        auto format = VK_FORMAT_B8G8R8A8_UNORM;
        aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;


        assert(aspectMask > 0);

        VkImageCreateInfo image = vks::initializers::imageCreateInfo();
        image.imageType = VK_IMAGE_TYPE_2D;
        image.format = format;
        image.extent.width = width();
        image.extent.height = height();
        image.extent.depth = 1;
        image.mipLevels = 1;
        image.arrayLayers = 1;
        image.samples = VK_SAMPLE_COUNT_1_BIT;
        image.tiling = VK_IMAGE_TILING_OPTIMAL;
        image.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

        auto device = m_vulkan_device->logicalDevice;
        VK_CHECK_RESULT(vkCreateImage(device, &image, nullptr, &m_out.image));

        VkMemoryAllocateInfo memAlloc = vks::initializers::memoryAllocateInfo();
        VkMemoryRequirements memReqs;
        vkGetImageMemoryRequirements(device, m_out.image, &memReqs);
        memAlloc.allocationSize = memReqs.size;
        memAlloc.memoryTypeIndex = m_vulkan_device->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        VK_CHECK_RESULT(vkAllocateMemory(device, &memAlloc, nullptr, &m_out.mem));
        VK_CHECK_RESULT(vkBindImageMemory(device, m_out.image, m_out.mem, 0));

        VkImageViewCreateInfo imageView = vks::initializers::imageViewCreateInfo();
        imageView.viewType = VK_IMAGE_VIEW_TYPE_2D;
        imageView.format = format;
        imageView.subresourceRange = {};
        imageView.subresourceRange.aspectMask = aspectMask;
        imageView.subresourceRange.baseMipLevel = 0;
        imageView.subresourceRange.levelCount = 1;
        imageView.subresourceRange.baseArrayLayer = 0;
        imageView.subresourceRange.layerCount = 1;
        imageView.image = m_out.image;
        VK_CHECK_RESULT(vkCreateImageView(device, &imageView, nullptr, &m_out.view));
    }
}
