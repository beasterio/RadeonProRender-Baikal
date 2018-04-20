#include "vk_render_target_manager.h"
#include "vk_utils.h"

namespace vkw
{
    RenderTarget RenderTargetManager::CreateRenderTarget(std::vector<RenderTargetCreateInfo> const& rt_create_info)
    {
        RenderTarget render_target;

        render_target.attachments.resize(rt_create_info.size());
        std::vector<VkAttachmentDescription> descriptions(rt_create_info.size());
        std::vector<VkImageView> views(rt_create_info.size());
        
        for (auto i = 0; i < rt_create_info.size(); i++)
        {
            InitAttachment(rt_create_info[i], render_target.attachments[i]);
            
            descriptions[i] = render_target.attachments[i].description;
            views[i]        = render_target.attachments[i].view;
        }
        
        render_target.render_pass = CreateDefaultRenderPass(descriptions);
        render_target.framebuffer = CreateFrameBuffer(views, render_target.render_pass, rt_create_info[0].width, rt_create_info[0].height, 1);
        
        return render_target;
    }
    
    void RenderTargetManager::InitAttachment(RenderTargetCreateInfo const& params, RenderTargetAttachment& attachment)
    {
        VkExtent3D size = {params.width, params.height, 1};
        
        attachment.image = memory_manager_.CreateImage(size, params.format, params.usage);
        attachment.view  = CreateImageView(attachment.image, params.format, params.usage);
        
        attachment.format = params.format;
        attachment.width = params.width;
        attachment.height = params.height;
        
        attachment.description.samples = VK_SAMPLE_COUNT_1_BIT;
        attachment.description.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachment.description.storeOp = (params.usage & VK_IMAGE_USAGE_SAMPLED_BIT) ? VK_ATTACHMENT_STORE_OP_STORE : VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachment.description.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachment.description.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachment.description.format = attachment.format;
        attachment.description.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachment.description.finalLayout = (ContainsDepth(params.format) || ContainsStencil(params.format)) ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }
    
    VkScopedObject<VkFramebuffer> RenderTargetManager::CreateFrameBuffer(std::vector<VkImageView>& views,
                                                                         VkRenderPass render_pass,
                                                                         uint32_t width,
                                                                         uint32_t height,
                                                                         uint32_t max_layers)
    {
        VkFramebufferCreateInfo framebuffer_create_info = {};
        framebuffer_create_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebuffer_create_info.renderPass = render_pass;
        framebuffer_create_info.pAttachments = views.data();
        framebuffer_create_info.attachmentCount = static_cast<uint32_t>(views.size());
        framebuffer_create_info.width = width;
        framebuffer_create_info.height = height;
        framebuffer_create_info.layers = max_layers;
        
        VkFramebuffer framebuffer;
        vkCreateFramebuffer(device_, &framebuffer_create_info, nullptr, &framebuffer);
        
        auto deleter = [this](VkFramebuffer framebuffer)
        {
            vkDestroyFramebuffer(device_, framebuffer, nullptr);
        };
        
        return VkScopedObject<VkFramebuffer>(framebuffer, deleter);
    }
    
    VkScopedObject<VkRenderPass> RenderTargetManager::CreateDefaultRenderPass(std::vector<VkAttachmentDescription>& descriptions)
    {
        std::vector<VkAttachmentReference> color_reference;
        VkAttachmentReference depth_reference = {};
        
        uint32_t idx = 0;
        
        bool depth_found = false;
        bool color_found = false;
        
        for (auto & description : descriptions)
        {
            if (ContainsStencil(description.format) || ContainsDepth(description.format))
            {
                if(depth_found)
                {
                    throw std::runtime_error("Can't create render pass - only one depth attachment is allowed");
                }
                
                depth_reference.attachment = idx;
                depth_reference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
                depth_found = true;
            }
            else
            {
                color_reference.push_back({ idx, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL });
                color_found = true;
            }
            
            idx++;
        };
        
        VkSubpassDescription subpass = {};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        
        if (color_found)
        {
            subpass.pColorAttachments = color_reference.data();
            subpass.colorAttachmentCount = static_cast<uint32_t>(color_reference.size());
        }
        
        if (depth_found)
        {
            subpass.pDepthStencilAttachment = &depth_reference;
        }
        
        VkSubpassDependency dependencies[2];
        
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
        
        VkRenderPassCreateInfo render_pass_info = {};
        render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        render_pass_info.pAttachments = descriptions.data();
        render_pass_info.attachmentCount = static_cast<uint32_t>(descriptions.size());
        render_pass_info.subpassCount = 1;
        render_pass_info.pSubpasses = &subpass;
        render_pass_info.dependencyCount = 2;
        render_pass_info.pDependencies = dependencies;
        
        VkRenderPass render_pass = nullptr;
        auto res = vkCreateRenderPass(device_, &render_pass_info, nullptr, &render_pass);
        
        if (res != VK_SUCCESS)
        {
            throw std::runtime_error("Can't create default render pass");
        }
        
        auto deleter = [this](VkRenderPass render_pass)
        {
            vkDestroyRenderPass(device_, render_pass, nullptr);
        };
        
        return VkScopedObject<VkRenderPass>(render_pass, deleter);
    }
    
    VkScopedObject<VkImageView> RenderTargetManager::CreateImageView(VkImage image,
                                                                     VkFormat format,
                                                                     VkImageUsageFlags usage)
    {
        VkImageAspectFlags image_aspect_flags = 0;
        
        bool contains_depth     = ContainsDepth(format);
        bool contains_stencil   = ContainsStencil(format);
        
        if (usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)
        {
            image_aspect_flags = VK_IMAGE_ASPECT_COLOR_BIT;
        }
        
        if (usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)
        {
            if (contains_depth)
            {
                image_aspect_flags = VK_IMAGE_ASPECT_DEPTH_BIT;
            }
            
            if (contains_stencil)
            {
                image_aspect_flags = image_aspect_flags | VK_IMAGE_ASPECT_STENCIL_BIT;
            }
        }
        
        VkImageSubresourceRange subresource_range = {};
        subresource_range.aspectMask = image_aspect_flags;
        subresource_range.levelCount = 1;
        subresource_range.layerCount = 1;
        
        VkImageViewCreateInfo image_view_create_info = {};
        image_view_create_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        image_view_create_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        image_view_create_info.format = format;
        image_view_create_info.subresourceRange = subresource_range;
        image_view_create_info.subresourceRange.aspectMask = (ContainsDepth(format)) ? VK_IMAGE_ASPECT_DEPTH_BIT : image_aspect_flags;
        image_view_create_info.image = image;
        
        VkImageView image_view = nullptr;
        auto res = vkCreateImageView(device_, &image_view_create_info, nullptr, &image_view);
        
        if (res != VK_SUCCESS)
        {
            throw std::runtime_error("VkMemoryManager: Cannot create Vulkan image view");
        }
        
        auto deleter = [this](VkImageView image_view)
        {
            vkDestroyImageView(device_, image_view, nullptr);
        };
        
        return VkScopedObject<VkImageView>(image_view, deleter);
    }
}
