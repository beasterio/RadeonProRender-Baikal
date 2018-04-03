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
        rrInitInstance(m_device->logicalDevice, m_device->physicalDevice, m_device->computeCommandPool, &m_instance);
        VkQueue queue;
        vkGetDeviceQueue(m_device->logicalDevice, m_device->queueFamilyIndices.graphics, 0, &queue);
        m_resources.reset(new ResourceManager(*m_device, queue));

        TextureList& textures = m_resources->GetTextures();


        // Add dummy textures for objects without texture
        textures.addTexture2D(STATIC_CRC32("dummy.diffuse"), "../Resources/Textures/dummy.dds", VK_FORMAT_BC2_UNORM_BLOCK);
        textures.addTexture2D(STATIC_CRC32("dummy.specular"), "../Resources/Textures/dummy_specular.dds", VK_FORMAT_BC2_UNORM_BLOCK);
        textures.addTexture2D(STATIC_CRC32("dummy.bump"), "../Resources/Textures/dummy_ddn.dds", VK_FORMAT_BC2_UNORM_BLOCK);
        textures.addTexture2D(STATIC_CRC32("dialectric.metallic"), "../Resources/Textures/Dielectric_metallic_TGA_BC2_1.DDS", VK_FORMAT_BC2_UNORM_BLOCK);
    }

    VkRenderFactory::~VkRenderFactory()
    {
        rrShutdownInstance(m_instance);
    }

    // Create a renderer of specified type
    std::unique_ptr<Renderer<VkScene>> VkRenderFactory::CreateRenderer(
                                                    RendererType type) const
    {
        switch (type)
        {
            case RendererType::kUnidirectionalPathTracer:
            {
                VkRenderer* renderer = new VkRenderer(m_device, &m_instance, m_resources.get());
                m_offscreen_buffer = renderer->GetOffscreenBuffer();

                return std::unique_ptr<Renderer<VkScene>>(renderer);
            }
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
        return std::make_unique<VkSceneController>(m_device, m_instance, m_offscreen_buffer, m_resources.get());
    }
}
