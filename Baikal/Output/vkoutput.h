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
        } framebuffers;
    private:
        void VkOutput::ShadowSetup();
        void VkOutput::DeferredSetup();

        vks::VulkanDevice* m_vulkan_device;
    };
}
