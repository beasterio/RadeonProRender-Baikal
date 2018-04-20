#include "vk_descriptor_manager.h"

namespace vkw
{
    DescriptorManager::DescriptorManager(VkDevice device)
    : device_(device)
    {
        VkDescriptorPoolSize pool_sizes[] =
        {
            { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, kNumDescriptors },
            { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, kNumDescriptors },
            { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, kNumDescriptors },
            { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kNumDescriptors },
            { VK_DESCRIPTOR_TYPE_SAMPLER, kNumDescriptors },
            { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, kNumDescriptors }
        };
        
        VkDescriptorPoolCreateInfo pool_create_info;
        pool_create_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool_create_info.pNext = nullptr;
        pool_create_info.flags = 0;
        pool_create_info.maxSets = kMaxSets;
        pool_create_info.poolSizeCount = sizeof(pool_sizes) / sizeof(VkDescriptorPoolSize);
        pool_create_info.pPoolSizes = pool_sizes;
        
        VkDescriptorPool pool = nullptr;
        auto res = vkCreateDescriptorPool(device, &pool_create_info, nullptr, &pool);
        
        if (res != VK_SUCCESS)
        {
            throw std::runtime_error("VkDescriptorManager: Cannot create descriptor pool");
        }
        
        pool_ = VkScopedObject<VkDescriptorPool>(pool,
                                                 [device](VkDescriptorPool pool)
                                                 {
                                                     vkDestroyDescriptorPool(device, pool, nullptr);
                                                 });
    }
    
    VkScopedObject<VkDescriptorSet> DescriptorManager::AllocateDescriptorSet(VkDescriptorSetLayout layout)
    {
        VkDescriptorSetAllocateInfo allocate_info;
        allocate_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocate_info.pNext = nullptr;
        allocate_info.descriptorPool = pool_;
        allocate_info.descriptorSetCount = 1u;
        allocate_info.pSetLayouts = &layout;
        
        VkDescriptorSet descriptor_set = nullptr;
        auto res = vkAllocateDescriptorSets(device_, &allocate_info, &descriptor_set);
        
        if (res != VK_SUCCESS)
        {
            throw std::runtime_error("VkDescriptorManager: Cannot allocate descriptors");
        }
        
        return VkScopedObject<VkDescriptorSet>(descriptor_set,
                                               [this](VkDescriptorSet desctiptor_set)
                                               {
                                                   vkFreeDescriptorSets(device_, pool_, 1u, &desctiptor_set);
                                               });
    }
    
}
