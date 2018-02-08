#include "vk_render_factory.h"

#include "Output/vkoutput.h"
#include "Renderers/vk_renderer.h"
#include "Controllers/vk_scene_controller.h"

#include <memory>

namespace Baikal
{
    VkRenderFactory::VkRenderFactory(vks::VulkanDevice* device)
        : m_device(device)
    {
        //rrInitInstance(m_device->logicalDevice, m_device->physicalDevice, vulkanDevice->computeCommandPool, &m_instance);
    }

    VkRenderFactory::~VkRenderFactory()
    {
        //rrShutdownInstance(m_instance);
    }

    // Create a renderer of specified type
    std::unique_ptr<Renderer<VkScene>> VkRenderFactory::CreateRenderer(
                                                    RendererType type) const
    {
        switch (type)
        {
            case RendererType::kUnidirectionalPathTracer:
                return std::unique_ptr<Renderer<VkScene>>(
                    new VkRenderer(m_device, m_instance));
            default:
                throw std::runtime_error("Renderer not supported");
        }
    }

    std::unique_ptr<Output> VkRenderFactory::CreateOutput(std::uint32_t w,
                                                           std::uint32_t h)
                                                           const
    {
        return std::unique_ptr<Output>(new VkOutput(m_device, w, h));
    }

    std::unique_ptr<PostEffect> VkRenderFactory::CreatePostEffect(
                                                    PostEffectType type) const
    {
        switch (type)
        {
            //case PostEffectType::kBilateralDenoiser:
            //    return std::unique_ptr<PostEffect>(
            //                                new BilateralDenoiser(m_context));
            //case PostEffectType::kWaveletDenoiser:
            //    return std::unique_ptr<PostEffect>(
            //                                new WaveletDenoiser(m_context));
            default:
                throw std::runtime_error("PostEffect not supported");
        }
    }

    std::unique_ptr<SceneController<VkScene>> VkRenderFactory::CreateSceneController() const
    {
        return std::make_unique<VkSceneController>(m_device, m_instance);
    }
}
