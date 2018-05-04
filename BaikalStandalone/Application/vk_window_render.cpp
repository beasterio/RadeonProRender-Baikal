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
#include "vk_window_render.h"
#include "VulkanDevice.hpp"
#include "VulkanTools.h"
#include "Output/vkoutput.h"
#include "VulkanSwapChain.hpp"

namespace
{
    struct SwapChainSupportDetails
    {
        VkSurfaceCapabilitiesKHR capabilities;
        std::vector<VkSurfaceFormatKHR> formats;
        std::vector<VkPresentModeKHR> present_modes;
    };

    struct QueueFamilyIndices
    {
        int graphics_family = -1;
        int present_family = -1;

        bool IsComplete() {
            return graphics_family >= 0 && present_family >= 0;
        }
    };

    QueueFamilyIndices FindQueueIndices(VkPhysicalDevice dev, VkSurfaceKHR surface)
    {
        QueueFamilyIndices indices;
        uint32_t queueFamilyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(dev, &queueFamilyCount, nullptr);

        std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(dev, &queueFamilyCount, queueFamilies.data());

        int i = 0;
        for (const auto& f : queueFamilies)
        {
            if (f.queueCount > 0 && f.queueFlags & VK_QUEUE_GRAPHICS_BIT)
            {
                indices.graphics_family = i;
            }

            VkBool32 presentSupport = false;
            VK_CHECK_RESULT(vkGetPhysicalDeviceSurfaceSupportKHR(dev, i, surface, &presentSupport));
            if (f.queueCount > 0 && presentSupport)
            {
                indices.present_family = i;
            }

            if (indices.IsComplete()) {
                break;
            }

            i++;
        }


        return indices;
    }

    SwapChainSupportDetails QuerySwapChainSupport(VkPhysicalDevice dev, VkSurfaceKHR surface)
    {
        SwapChainSupportDetails details;
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(dev, surface, &details.capabilities);

        uint32_t format_cnt;
        vkGetPhysicalDeviceSurfaceFormatsKHR(dev, surface, &format_cnt, nullptr);

        if (format_cnt != 0)
        {
            details.formats.resize(format_cnt);
            vkGetPhysicalDeviceSurfaceFormatsKHR(dev, surface, &format_cnt, details.formats.data());
        }

        uint32_t mode_cnt;
        vkGetPhysicalDeviceSurfacePresentModesKHR(dev, surface, &mode_cnt, nullptr);

        if (mode_cnt != 0)
        {
            details.present_modes.resize(mode_cnt);
            vkGetPhysicalDeviceSurfacePresentModesKHR(dev, surface, &mode_cnt, details.present_modes.data());
        }

        return details;
    }

    VkSurfaceFormatKHR ChooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& available_formats)
    {
        if (available_formats.size() == 1 && available_formats[0].format == VK_FORMAT_UNDEFINED)
        {
            return{ VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR };
        }

        for (const auto& f : available_formats)
        {
            if (f.format == VK_FORMAT_B8G8R8A8_UNORM && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
                return f;
            }
        }
    }

    VkPresentModeKHR ChooseSwapPresentMode(const std::vector<VkPresentModeKHR> available_modes)
    {
        VkPresentModeKHR mode = VK_PRESENT_MODE_FIFO_KHR;
        for (const auto& m : available_modes)
        {
            if (m == VK_PRESENT_MODE_MAILBOX_KHR)
            {
                return m;
            }
            else if (m == VK_PRESENT_MODE_IMMEDIATE_KHR)
            {
                mode = m;
            }
        }

        return mode;
    }

    VkExtent2D ChooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities, int w, int h)
    {
        if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max())
        {
            return capabilities.currentExtent;
        }
        else {
            VkExtent2D actualExtent = { w, h };

            actualExtent.width = std::max(capabilities.minImageExtent.width, std::min(capabilities.maxImageExtent.width, actualExtent.width));
            actualExtent.height = std::max(capabilities.minImageExtent.height, std::min(capabilities.maxImageExtent.height, actualExtent.height));

            return actualExtent;
        }
    }

    bool IsSwapchainSupported(VkPhysicalDevice dev, VkInstance instance, VkSurfaceKHR surface)
    {

        SwapChainSupportDetails details = QuerySwapChainSupport(dev, surface);

        return !details.formats.empty() && !details.present_modes.empty();
    }
}

namespace Baikal
{


    AppVkWindowRender::AppVkWindowRender(const AppSettings& settings, vks::VulkanDevice* device)
        : m_width(settings.width)
        , m_height(settings.height)
        , m_device(device)
    {
        VkSemaphoreCreateInfo semaphoreCreateInfo = vks::initializers::semaphoreCreateInfo();
        VK_CHECK_RESULT(vkCreateSemaphore(m_device->logicalDevice, &semaphoreCreateInfo, nullptr, &m_semaphores.present_complete));
        VK_CHECK_RESULT(vkCreateSemaphore(m_device->logicalDevice, &semaphoreCreateInfo, nullptr, &m_semaphores.copy_complete));

        //render pass
        {
            VkAttachmentDescription attachment = {};
            attachment.format = VK_FORMAT_B8G8R8A8_UNORM;
            attachment.samples = VK_SAMPLE_COUNT_1_BIT;
            attachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
            attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
            VkAttachmentReference color_attachment = {};
            color_attachment.attachment = 0;
            color_attachment.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            VkSubpassDescription subpass = {};
            subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
            subpass.colorAttachmentCount = 1;
            subpass.pColorAttachments = &color_attachment;
            VkRenderPassCreateInfo info = {};
            info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
            info.attachmentCount = 1;
            info.pAttachments = &attachment;
            info.subpassCount = 1;
            info.pSubpasses = &subpass;
            VK_CHECK_RESULT(vkCreateRenderPass(device->logicalDevice, &info, nullptr, &m_render_pass));
        }
    }

    AppVkWindowRender::~AppVkWindowRender()
    {
        auto device = m_device->logicalDevice;
        vkDestroySwapchainKHR(device, m_swapchain, nullptr);
        vkDestroySemaphore(device, m_semaphores.present_complete, nullptr);
        vkDestroySemaphore(device, m_semaphores.copy_complete, nullptr);
        vkDestroyRenderPass(device, m_render_pass, nullptr);

        for (auto view : m_swapchain_image_views)
        {
            vkDestroyImageView(device, view, nullptr);
        }

        for (auto framebuffer : m_swapchain_image_framebuffers)
        {
            vkDestroyFramebuffer(device, framebuffer, nullptr);
        }
    }



    void AppVkWindowRender::SetWindow(VkInstance instance, GLFWwindow* window)
    {
        auto dev = m_device->physicalDevice;
        VkResult err = glfwCreateWindowSurface(instance, window, NULL, &m_surface);
        if (err)
        {
            throw std::runtime_error("failed to create window surface");
        }

        if (!IsSwapchainSupported(dev, instance, m_surface))
        {
            throw std::runtime_error("Swapchain isn't supported.");
        }

        SwapChainSupportDetails details = QuerySwapChainSupport(dev, m_surface);
        VkSurfaceFormatKHR format = ChooseSwapSurfaceFormat(details.formats);
        VkPresentModeKHR mode = ChooseSwapPresentMode(details.present_modes);
        VkExtent2D extent = ChooseSwapExtent(details.capabilities, m_width, m_height);

        uint32_t image_cnt = details.capabilities.minImageCount + 1;
        if (details.capabilities.maxImageCount > 0 && image_cnt > details.capabilities.maxImageCount)
        {
            image_cnt = details.capabilities.maxImageCount;
        }

        VkSwapchainCreateInfoKHR create_info = {};
        create_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        create_info.pNext = nullptr;
        create_info.surface = m_surface;
        create_info.minImageCount = image_cnt;
        create_info.imageFormat = format.format;
        create_info.imageColorSpace = format.colorSpace;
        create_info.imageExtent = extent;
        create_info.imageArrayLayers = 1;
        //create_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        create_info.imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        create_info.preTransform = details.capabilities.currentTransform;
        create_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        create_info.presentMode = mode;
        create_info.clipped = VK_TRUE;
        create_info.oldSwapchain = VK_NULL_HANDLE;

        QueueFamilyIndices indices = FindQueueIndices(dev, m_surface);
        uint32_t queueFamilyIndices[] = { (uint32_t)indices.graphics_family, (uint32_t)indices.present_family };

        if (indices.graphics_family != indices.present_family)
        {
            create_info.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
            create_info.queueFamilyIndexCount = 2;
            create_info.pQueueFamilyIndices = queueFamilyIndices;
        }
        else
        {
            create_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
            create_info.queueFamilyIndexCount = 0; // Optional
            create_info.pQueueFamilyIndices = nullptr; // Optional
        }


        uint32_t extensionCount;
        vkEnumerateDeviceExtensionProperties(m_device->physicalDevice, nullptr, &extensionCount, nullptr);

        std::vector<VkExtensionProperties> availableExtensions(extensionCount);
        vkEnumerateDeviceExtensionProperties(m_device->physicalDevice, nullptr, &extensionCount, availableExtensions.data());

        VK_CHECK_RESULT(vkCreateSwapchainKHR(m_device->logicalDevice, &create_info, nullptr, &m_swapchain));

        uint32_t num_images = 0;
        VK_CHECK_RESULT(vkGetSwapchainImagesKHR(m_device->logicalDevice, m_swapchain, &num_images, nullptr));
        assert(num_images);
        m_swapchain_images.resize(num_images);
        VK_CHECK_RESULT(vkGetSwapchainImagesKHR(m_device->logicalDevice, m_swapchain, &num_images, m_swapchain_images.data()));


        //views
        {
            m_swapchain_image_views.resize(num_images);

            VkImageSubresourceRange  image_range = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

            VkImageViewCreateInfo info = {};
            info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            info.viewType = VK_IMAGE_VIEW_TYPE_2D;
            info.format = VK_FORMAT_B8G8R8A8_UNORM;
            info.components.r = VK_COMPONENT_SWIZZLE_R;
            info.components.g = VK_COMPONENT_SWIZZLE_G;
            info.components.b = VK_COMPONENT_SWIZZLE_B;
            info.components.a = VK_COMPONENT_SWIZZLE_A;
            info.subresourceRange = image_range;
            for (uint32_t i = 0; i < num_images; i++)
            {
                info.image = m_swapchain_images[i];
                VK_CHECK_RESULT(vkCreateImageView(m_device->logicalDevice, &info, nullptr, &m_swapchain_image_views[i]));
            }
        }
        //framebuffers
        {
            m_swapchain_image_framebuffers.resize(num_images);

            VkImageView attachment[1];
            VkFramebufferCreateInfo info = {};
            info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            info.renderPass = m_render_pass;
            info.attachmentCount = 1;
            info.pAttachments = attachment;
            info.width = m_width;
            info.height = m_height;
            info.layers = 1;
            for (uint32_t i = 0; i < num_images; i++)
            {
                attachment[0] = m_swapchain_image_views[i];
                VK_CHECK_RESULT(vkCreateFramebuffer(m_device->logicalDevice, &info, nullptr, &m_swapchain_image_framebuffers[i]));
            }
        }
    }

    void AppVkWindowRender::SetOutput(VkOutput* out)
    {
        m_output = out;
        //for (auto b : )
    }

    VkCommandBuffer AppVkWindowRender::BeginFrame(GLFWwindow* window)
    {
        VK_CHECK_RESULT(vkAcquireNextImageKHR(m_device->logicalDevice, m_swapchain, UINT64_MAX, m_semaphores.present_complete, VK_NULL_HANDLE, &m_next_img));

        VkQueue queue;
        vkGetDeviceQueue(m_device->logicalDevice, m_device->queueFamilyIndices.graphics, 0, &queue);

        //copy data to swapchain
        m_cmd = m_device->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY);
        {
            VkCommandBufferBeginInfo cmd_buf_info = vks::initializers::commandBufferBeginInfo();

            VkImageSubresourceRange subresource_range = {};
            subresource_range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            subresource_range.baseMipLevel = 0;
            subresource_range.levelCount = 1;
            subresource_range.layerCount = 1;

            VkImageCopy copy_region = {};

            copy_region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            copy_region.srcSubresource.baseArrayLayer = 0;
            copy_region.srcSubresource.mipLevel = 0;
            copy_region.srcSubresource.layerCount = 1;
            copy_region.srcOffset = { 0, 0, 0 };

            copy_region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            copy_region.dstSubresource.baseArrayLayer = 0;
            copy_region.dstSubresource.mipLevel = 0;
            copy_region.dstSubresource.layerCount = 1;
            copy_region.dstOffset = { 0, 0, 0 };

            copy_region.extent.width = static_cast<uint32_t>(m_output->width());
            copy_region.extent.height = static_cast<uint32_t>(m_output->height());
            copy_region.extent.depth = 1;

            vkBeginCommandBuffer(m_cmd, &cmd_buf_info);

            vks::tools::setImageLayout(m_cmd, m_output->GetImage(), VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, subresource_range);
            vks::tools::setImageLayout(m_cmd, m_swapchain_images[m_next_img], VK_IMAGE_LAYOUT_UNDEFINED,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, subresource_range);

            vkCmdCopyImage(
                m_cmd,
                m_output->GetImage(),
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                m_swapchain_images[m_next_img],
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                1,
                &copy_region);

            vks::tools::setImageLayout(m_cmd, m_output->GetImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, subresource_range);
            vks::tools::setImageLayout(m_cmd, m_swapchain_images[m_next_img], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                VK_IMAGE_LAYOUT_PRESENT_SRC_KHR , subresource_range);
        }

        VkRenderPassBeginInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        info.renderPass = m_render_pass;
        info.framebuffer = m_swapchain_image_framebuffers[m_next_img];
        info.renderArea.extent.width = m_width;
        info.renderArea.extent.height = m_height;
        info.clearValueCount = 0;
        info.pClearValues = nullptr;
        vkCmdBeginRenderPass(m_cmd, &info, VK_SUBPASS_CONTENTS_INLINE);

        return m_cmd;
    }

    void AppVkWindowRender::PresentFrame()
    {
        VkQueue queue;
        vkGetDeviceQueue(m_device->logicalDevice, m_device->queueFamilyIndices.graphics, 0, &queue);

        //end and submit command buffer
        vkCmdEndRenderPass(m_cmd);
        VK_CHECK_RESULT(vkEndCommandBuffer(m_cmd));
        VkPipelineStageFlags stageFlags = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        VkSubmitInfo submit_info;
        submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit_info.pNext = nullptr;
        submit_info.pWaitSemaphores = &m_semaphores.present_complete;
        submit_info.waitSemaphoreCount = 1;
        submit_info.pWaitDstStageMask = &stageFlags;
        submit_info.pSignalSemaphores = &m_semaphores.copy_complete;
        submit_info.signalSemaphoreCount = 1;
        submit_info.commandBufferCount = 1;
        submit_info.pCommandBuffers = &m_cmd;
        VK_CHECK_RESULT(vkQueueSubmit(queue, 1, &submit_info, VK_NULL_HANDLE));

        //present
        VkPresentInfoKHR info = {};
        info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        info.pNext = NULL;
        info.swapchainCount = 1;
        info.pSwapchains = &m_swapchain;
        info.pImageIndices = &m_next_img;
        info.pWaitSemaphores = &m_semaphores.copy_complete;
        info.waitSemaphoreCount = 1;
        VK_CHECK_RESULT(vkQueuePresentKHR(queue, &info));
        VK_CHECK_RESULT(vkQueueWaitIdle(queue));

        vkFreeCommandBuffers(m_device->logicalDevice, m_device->commandPool, 1, &m_cmd);
    }

} // Baikal
