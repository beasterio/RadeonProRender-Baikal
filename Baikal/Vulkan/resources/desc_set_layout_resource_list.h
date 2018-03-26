#pragma once

#include "resource_list.h"

class DescriptionSetLayoutList : public ResourceList<VkDescriptorSetLayout>
{
public:
    DescriptionSetLayoutList(vks::VulkanDevice& dev) : ResourceList(dev) {};
    ~DescriptionSetLayoutList() {
        for (auto it : _resources) {
            vkDestroyDescriptorSetLayout(_device.logicalDevice, it.second, nullptr);
        }
    }
};

class DescriptionSetList : public ResourceList<VkDescriptorSet>
{
public:
    DescriptionSetList(vks::VulkanDevice& dev) : ResourceList(dev), _descriptor_pool(VK_NULL_HANDLE) {
        std::vector<VkDescriptorPoolSize> pool_sizes = {
            vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 32),
            vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 64),
            vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 32),
            vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 32)
        };

        VkDescriptorPoolCreateInfo descriptorPoolInfo = vks::initializers::descriptorPoolCreateInfo( static_cast<uint32_t>(pool_sizes.size()),
                pool_sizes.data(),
                64);

        VK_CHECK_RESULT(vkCreateDescriptorPool(_device.logicalDevice, &descriptorPoolInfo, nullptr, &_descriptor_pool));
    };

    ~DescriptionSetList() { 
        vkDestroyDescriptorPool(_device.logicalDevice, _descriptor_pool, nullptr);
    }

    inline VkDescriptorPool GetDescriptorPool() {
        return _descriptor_pool;
    }
private:
    VkDescriptorPool _descriptor_pool;
};