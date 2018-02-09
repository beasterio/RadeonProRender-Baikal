#pragma once

#include "output.h"
#include "Renderers/vk_renderer.h"
#include "Vulkan/VulkanFrameBuffer.hpp"

namespace Baikal
{
    class VkOutput : public Output
    {
    public:
        VkOutput(vks::VulkanDevice* vulkan_device, std::uint32_t w, std::uint32_t h);
        ~VkOutput();

        void GetData(RadeonRays::float3* data) const override;
        struct
        {
            // Framebuffer resources for the deferred pass
            vks::Framebuffer *deferred;
            // Framebuffer resources for the shadow pass
            vks::Framebuffer *shadow[LIGHT_COUNT];

            VkRenderPass draw_render_pass;
            VkFramebuffer draw_fb;
        } framebuffers;
    private:
        void ShadowSetup();
        void DeferredSetup();
        void DrawSetup();
        void SetupRenderPass();
        void SetupDepthStencil();
        void SetupOutput();

        vks::VulkanDevice* m_vulkan_device;

        struct OutputFb
        {
            VkImage image;
            VkDeviceMemory mem;
            VkImageView view;
        };

        OutputFb m_depth_stencil;
        OutputFb m_out;
    };
}
