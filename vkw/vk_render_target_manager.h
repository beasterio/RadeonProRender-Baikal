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
#pragma once
#include <vulkan/vulkan.h>

#include "vk_scoped_object.h"
#include "vk_memory_manager.h"

namespace vkw
{
    struct RenderTargetAttachment
    {
        uint32_t            width;
        uint32_t            height;
        
        VkScopedObject<VkImage>     image;
        VkScopedObject<VkImageView> view;
        
        VkAttachmentDescription     description;
        VkFormat                    format;
    };
    
    struct RenderTargetCreateInfo
    {
        uint32_t width;
        uint32_t height;
        
        VkFormat            format;
        VkImageUsageFlags   usage;
    };
    
    struct RenderTarget
    {
        std::vector<RenderTargetAttachment>     attachments;
        VkScopedObject<VkFramebuffer>           framebuffer;
        VkScopedObject<VkRenderPass>            render_pass;
    };

    class RenderTargetManager
    {
    public:
        RenderTargetManager(VkDevice device, MemoryManager& memory_manager)
        : device_(device)
        , memory_manager_(memory_manager)
        {
        }
        
        RenderTarget CreateRenderTarget(std::vector<RenderTargetCreateInfo> const& rt_create_info);
        
    private:
        void InitAttachment(RenderTargetCreateInfo const& rt_create_info, RenderTargetAttachment& attachment);
        
        VkScopedObject<VkFramebuffer> CreateFrameBuffer(std::vector<VkImageView>& views,
                                                        VkRenderPass render_pass,
                                                        uint32_t width,
                                                        uint32_t height,
                                                        uint32_t max_layers);
        
        VkScopedObject<VkRenderPass> CreateDefaultRenderPass(std::vector<VkAttachmentDescription>& descriptions);
        
        VkScopedObject<VkImageView> CreateImageView(VkImage image,
                                                    VkFormat format,
                                                    VkImageUsageFlags usage);
        
    private:
        MemoryManager& memory_manager_;
        VkDevice device_;
    };
}
