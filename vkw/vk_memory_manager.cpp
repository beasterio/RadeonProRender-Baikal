#include "vk_memory_manager.h"

namespace vkw
{
    void MemoryManager::CopyToHostVisibleBlock(VkDevice device,
                                                 MemoryAllocator::StorageBlock const& storage_block,
                                                 VkDeviceSize size,
                                                 void const* data)
    {
        void* mapped_ptr = nullptr;
        auto res = vkMapMemory(device,
                               storage_block.memory,
                               storage_block.offset,
                               storage_block.size,
                               0,
                               &mapped_ptr);
        
        if (res != VK_SUCCESS)
        {
            throw std::runtime_error("VkMemoryManager: Cannot map host visible buffer");
        }
        
        std::copy((char const*)data, (char const*)data + size, (char*)mapped_ptr);
        
        VkMappedMemoryRange mapped_range;
        mapped_range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
        mapped_range.pNext = nullptr;
        mapped_range.memory = storage_block.memory;
        mapped_range.offset = storage_block.offset;
        mapped_range.size = storage_block.size;
        
        vkFlushMappedMemoryRanges(device, 1u, &mapped_range);
        vkUnmapMemory(device, storage_block.memory);
    }
    
    void MemoryManager::CopyFromHostVisibleBlock(VkDevice device,
                                                   MemoryAllocator::StorageBlock const& storage_block,
                                                   VkDeviceSize size,
                                                   void* data)
    {
        void* mapped_ptr = nullptr;
        auto res = vkMapMemory(device,
                               storage_block.memory,
                               storage_block.offset,
                               storage_block.size,
                               0,
                               &mapped_ptr);
        
        if (res != VK_SUCCESS)
        {
            throw std::runtime_error("VkMemoryManager: Cannot map host visible buffer");
        }
        
        VkMappedMemoryRange mapped_range;
        mapped_range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
        mapped_range.pNext = nullptr;
        mapped_range.memory = storage_block.memory;
        mapped_range.offset = storage_block.offset;
        mapped_range.size = storage_block.size;
        
        vkInvalidateMappedMemoryRanges(device, 1u, &mapped_range);
        
        std::copy((char*)mapped_ptr, (char*)mapped_ptr + size, (char*)data);
        
        vkUnmapMemory(device, storage_block.memory);
    }
    
    void MemoryManager::ReadBuffer(VkBuffer buffer, VkDeviceSize offset, VkDeviceSize size, void* data)
    {
        auto iter = buffer_bindings_.find(buffer);
        
        if (iter != buffer_bindings_.cend())
        {
            if (iter->second.memory_type_index & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)
            {
                CopyFromHostVisibleBlock(device_, iter->second, size, data);
            }
            else
            {
                VkBuffer staging_buffer = nullptr;
                MemoryAllocator::StorageBlock staging_block;
                GetStagingBufferAndBlock(size, staging_buffer, staging_block);
                
                CopyBuffer(device_, buffer, staging_buffer, offset, 0u, size);
                
                CopyFromHostVisibleBlock(device_, staging_block, size, data);
            }
        }
        else
        {
            throw std::runtime_error("VkMemoryManager: Unregistered buffer");
        }
    }
    
    void MemoryManager::WriteBuffer(VkBuffer buffer,
                                      VkDeviceSize offset,
                                      VkDeviceSize size,
                                      void const* data)
    {
        auto iter = buffer_bindings_.find(buffer);
        
        if (iter != buffer_bindings_.cend())
        {
            if (iter->second.memory_type_index & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)
            {
                CopyToHostVisibleBlock(device_, iter->second, size, data);
            }
            else
            {
                VkBuffer staging_buffer = nullptr;
                MemoryAllocator::StorageBlock staging_block;
                GetStagingBufferAndBlock(size, staging_buffer, staging_block);
                
                CopyToHostVisibleBlock(device_, staging_block, size, data);
                
                CopyBuffer(device_, staging_buffer, buffer, 0u, offset, size);
            }
        }
        else
        {
            throw std::runtime_error("VkMemoryManager: Unregistered buffer");
        }
    }
    
    MemoryManager::MemoryManager(VkDevice device,
                                     std::uint32_t queue_family_index,
                                     MemoryAllocator& allocator)
    : device_(device)
    , allocator_(allocator)
    , queue_family_index_(queue_family_index)
    {
        VkCommandPoolCreateInfo pool_create_info;
        pool_create_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        pool_create_info.queueFamilyIndex = queue_family_index_;
        pool_create_info.flags = 0;

        VkCommandPool command_pool = nullptr;
        auto res = vkCreateCommandPool(device_, &pool_create_info, nullptr, &command_pool);
        
        if (res != VK_SUCCESS)
        {
            throw std::runtime_error("VkMemoryManager: Cannot create command pool");
        }
        
        command_pool_ = VkScopedObject<VkCommandPool>(command_pool,
                                                      [device](VkCommandPool pool)
                                                      {
                                                          vkDestroyCommandPool(device, pool, nullptr);
                                                      });
    }
    
    void MemoryManager::CopyBuffer(VkDevice device,
                                     VkBuffer src_buffer,
                                     VkBuffer dst_buffer,
                                     VkDeviceSize src_offset,
                                     VkDeviceSize dst_offset,
                                     VkDeviceSize size)
    {
        VkCommandBufferAllocateInfo command_buffer_alloc_info;
        command_buffer_alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        command_buffer_alloc_info.pNext = nullptr;
        command_buffer_alloc_info.commandPool = command_pool_;
        command_buffer_alloc_info.commandBufferCount = 1u;
        command_buffer_alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        
        VkCommandBuffer command_buffer = nullptr;
        vkAllocateCommandBuffers(device_, &command_buffer_alloc_info, &command_buffer);
        
        VkCommandBufferBeginInfo begin_info;
        begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin_info.pNext = nullptr;
        begin_info.pInheritanceInfo = nullptr;
        begin_info.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
        
        vkBeginCommandBuffer(command_buffer, &begin_info);

        VkBufferCopy copy_region;
        copy_region.srcOffset = src_offset;
        copy_region.dstOffset = dst_offset;
        copy_region.size = size;
        vkCmdCopyBuffer(command_buffer, src_buffer, dst_buffer, 1u, &copy_region);
        
        vkEndCommandBuffer(command_buffer);
        
        VkSubmitInfo submit_info;
        submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit_info.pNext = nullptr;
        submit_info.commandBufferCount = 1u;
        submit_info.pCommandBuffers = &command_buffer;
        submit_info.pSignalSemaphores = nullptr;
        submit_info.signalSemaphoreCount = 0u;
        submit_info.pWaitSemaphores = nullptr;
        submit_info.waitSemaphoreCount = 0u;
        submit_info.pWaitDstStageMask = nullptr;
        
        VkQueue queue = nullptr;
        vkGetDeviceQueue(device_, queue_family_index_, 0u, &queue);
        
        vkQueueSubmit(queue, 1u, &submit_info, VK_NULL_HANDLE);
        vkQueueWaitIdle(queue);
    }
    
    VkScopedObject<VkBuffer> MemoryManager::CreateBuffer(VkDeviceSize size,
                                                           VkMemoryPropertyFlags memory_type,
                                                           VkBufferUsageFlags usage,
                                                           void* init_data)
    {
        if (init_data && !(memory_type & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT))
        {
            usage |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        }
        
        VkBufferCreateInfo buffer_create_info;
        buffer_create_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        buffer_create_info.pNext = nullptr;
        buffer_create_info.usage = usage;
        buffer_create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        buffer_create_info.size = size;
        buffer_create_info.flags = 0;
        buffer_create_info.queueFamilyIndexCount = 0u;
        buffer_create_info.pQueueFamilyIndices = nullptr;
        
        VkBuffer buffer = nullptr;
        auto res = vkCreateBuffer(device_, &buffer_create_info, nullptr, &buffer);
        
        if (res != VK_SUCCESS)
        {
            throw std::runtime_error("VkMemoryAllocator: Cannot create Vulkan buffer");
        }
        
        VkMemoryRequirements mem_reqs;
        vkGetBufferMemoryRequirements(device_, buffer, &mem_reqs);
        
        auto storage_block = allocator_.allocate(memory_type,
                                                 mem_reqs.size,
                                                 mem_reqs.alignment);
        
        res = vkBindBufferMemory(device_,
                                 buffer,
                                 storage_block.memory,
                                 storage_block.offset);
        
        if (res != VK_SUCCESS)
        {
            throw std::runtime_error("VkMemoryManager: Cannot bind buffer memory");
        }
        
        buffer_bindings_[buffer] = storage_block;
        
        auto deleter = [this](VkBuffer buffer)
        {
            auto iter = buffer_bindings_.find(buffer);
            
            vkDestroyBuffer(device_, buffer, nullptr);
            
            if (iter != buffer_bindings_.cend())
            {
                allocator_.deallocate(iter->second);
                buffer_bindings_.erase(iter);
            }
        };
        
        if (init_data)
        {
            if (memory_type & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)
            {
                CopyToHostVisibleBlock(device_, storage_block, size, init_data);
            }
            else
            {
                VkBuffer staging_buffer = nullptr;
                MemoryAllocator::StorageBlock staging_block;
                GetStagingBufferAndBlock(size, staging_buffer, staging_block);
                
                CopyToHostVisibleBlock(device_, staging_block, size, init_data);
                
                CopyBuffer(device_, staging_buffer, buffer, 0u, 0u, size);
            }
        }
    
        return VkScopedObject<VkBuffer>(buffer, deleter);
    }
    
    VkScopedObject<VkImage> MemoryManager::CreateImage(VkExtent3D size,
                                                         VkFormat format,
                                                         VkImageUsageFlags usage)
    {
        VkImageType image_type = VK_IMAGE_TYPE_1D;
        image_type = size.width > 1 && size.height > 1 ? VK_IMAGE_TYPE_2D : image_type;
        image_type = size.width > 1 && size.height > 1 && size.depth > 1 ? VK_IMAGE_TYPE_3D : image_type;
        
        VkImageCreateInfo image_create_info = {};
        image_create_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        image_create_info.pNext = nullptr;
        image_create_info.imageType = image_type;
        image_create_info.format = format;
        image_create_info.extent.width = size.width;
        image_create_info.extent.height = size.height;
        //image_create_info.extent.depth = size.width;
        image_create_info.extent.depth = 1;
        image_create_info.mipLevels = 1;
        image_create_info.arrayLayers = 1;
        image_create_info.samples = VK_SAMPLE_COUNT_1_BIT;
        image_create_info.tiling = VK_IMAGE_TILING_OPTIMAL;
        image_create_info.usage = usage;
        image_create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        image_create_info.queueFamilyIndexCount = 0;
        image_create_info.pQueueFamilyIndices = nullptr;
        image_create_info.flags = 0;
        image_create_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        
        VkImage image = nullptr;
        auto res = vkCreateImage(device_, &image_create_info, nullptr, &image);
        
        if (res != VK_SUCCESS)
        {
            throw std::runtime_error("VkMemoryManager: Cannot create Vulkan image");
        }
        
        VkMemoryRequirements mem_reqs;
        vkGetImageMemoryRequirements(device_, image, &mem_reqs);
        
        auto storage_block = allocator_.allocate(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                                 mem_reqs.size,
                                                 mem_reqs.alignment);
        
        res = vkBindImageMemory(device_,
                                image,
                                storage_block.memory,
                                0);
        
        if (res != VK_SUCCESS)
        {
            throw std::runtime_error("VkMemoryManager: Cannot bind image memory");
        }
        
        image_bindings_[image] = storage_block;
        
        auto deleter = [this](VkImage image)
        {
            auto iter = image_bindings_.find(image);
            
            vkDestroyImage(device_, image, nullptr);
            
            if (iter != image_bindings_.cend())
            {
                allocator_.deallocate(iter->second);
                image_bindings_.erase(iter);
            }
        };
        
        return VkScopedObject<VkImage>(image, deleter);
    }
    
    void MemoryManager::GetStagingBufferAndBlock(VkDeviceSize size,
                                                 VkBuffer& buffer,
                                                 MemoryAllocator::StorageBlock& block)
    {
        auto iter = std::find_if(staging_buffer_pool_.begin(),
                                 staging_buffer_pool_.end(),
                                 [this, size](VkScopedObject<VkBuffer>& buffer)
                                 {
                                     auto& block = buffer_bindings_[buffer];
                                     return block.size >= size;
                                 });
        
        if (iter == staging_buffer_pool_.cend())
        {
            auto staging_buffer = CreateBuffer(size,
                                               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                                               VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
                                               VK_BUFFER_USAGE_TRANSFER_DST_BIT);
            buffer = staging_buffer;
            staging_buffer_pool_.emplace_front(std::move(staging_buffer));
        }
        else
        {
            buffer = *iter;
        }
        
        block = buffer_bindings_[buffer];
    }
}
