#pragma once

#include "resource_list.h"

#include "VulkanTexture.hpp"

class TextureList : public ResourceList<vks::Texture>
{
public:
    TextureList(vks::VulkanDevice& dev, VkQueue queue) :
        ResourceList(dev), queue(queue)
    { };

    ~TextureList()
    {
        for (auto& texture : _resources)
        {
            texture.second.destroy();
        }
    }

    vks::Texture2D addTexture2D(uint32_t name, 
        void const* buffer,
        VkDeviceSize bufferSize,
        VkFormat format,
        uint32_t width,
        uint32_t height,
        vks::VulkanDevice *device,
        VkQueue copyQueue)
    {
        vks::Texture2D texture;
        texture.fromBuffer(buffer, bufferSize, format, width, height, device, copyQueue);
        _resources[name] = texture;
        return texture;
    }

    vks::Texture2D addTexture2D(uint32_t name, std::string const& filename, VkFormat format)
    {
        vks::Texture2D texture;
        texture.loadFromFile(filename, format, &_device, queue);
        _resources[name] = texture;
        return texture;
    }

    vks::TextureCubeMap addCubemap(uint32_t name, std::string const& filename, VkFormat format)
    {
        vks::TextureCubeMap texture;
        texture.loadFromFile(filename, format, &_device, queue);
        _resources[name] = texture;
        return texture;
    }

protected:
    VkQueue queue;
};