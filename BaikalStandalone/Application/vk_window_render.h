
/**********************************************************************
 Copyright (c) 2017 Advanced Micro Devices, Inc. All rights reserved.
 
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
#pragma once

#include <vulkan/vulkan.h>
#include "GLFW/glfw3.h"
#include "Application/app_utils.h"
#include "Output/vkoutput.h"

namespace Baikal
{
    class AppVkWindowRender
    {
    public:
        AppVkWindowRender(const AppSettings& settings, vks::VulkanDevice* device);
        ~AppVkWindowRender();

        void SetWindow(VkInstance instance, GLFWwindow* window);
        void SetOutput(VkOutput* out);

        VkCommandBuffer BeginFrame(GLFWwindow* window);
        void PresentFrame();

        VkRenderPass GetRenderPass() { return m_render_pass; }
    private:
        VkSurfaceKHR m_surface;
        VkSwapchainKHR m_swapchain;

        int m_width;
        int m_height;
        vks::VulkanDevice* m_device;
        VkCommandBuffer m_cmd;
        struct  
        {
            VkSemaphore present_complete;
            VkSemaphore copy_complete;
        } m_semaphores;

        VkRenderPass m_render_pass;
        uint32_t m_next_img;
        std::vector<VkImage> m_swapchain_images;
        std::vector<VkImageView> m_swapchain_image_views;
        std::vector<VkFramebuffer> m_swapchain_image_framebuffers;
        std::vector<VkCommandBuffer> m_copy_buffers;
        VkOutput* m_output;
    };
}
