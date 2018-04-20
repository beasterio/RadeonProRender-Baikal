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
#include "vk_memory_allocator.h"
#include "vk_scoped_object.h"
#include <unordered_map>
#include <list>

namespace vkw
{
    class MemoryManager
    {
    public:
        MemoryManager(VkDevice device,
                        std::uint32_t queue_family_index,
                        MemoryAllocator& allocator);
        
        VkScopedObject<VkBuffer> CreateBuffer(VkDeviceSize size,
                                              VkMemoryPropertyFlags memory_type,
                                              VkBufferUsageFlags usage,
                                              void* init_data = nullptr);
        
        void WriteBuffer(VkBuffer buffer,
                         VkDeviceSize offset,
                         VkDeviceSize size,
                         void const* data);
        
        void ReadBuffer(VkBuffer buffer,
                        VkDeviceSize offset,
                        VkDeviceSize size,
                        void* data);
        
        VkScopedObject<VkImage> CreateImage(VkExtent3D size,
                                            VkFormat format,
                                            VkImageUsageFlags usage);
        
    private:
        static void CopyToHostVisibleBlock(VkDevice device,
                                           MemoryAllocator::StorageBlock const& block,
                                           VkDeviceSize size,
                                           void const* data);
        
        static void CopyFromHostVisibleBlock(VkDevice device,
                                             MemoryAllocator::StorageBlock const& block,
                                             VkDeviceSize size,
                                             void* data);
        
        void GetStagingBufferAndBlock(VkDeviceSize size,
                                      VkBuffer& buffer,
                                      MemoryAllocator::StorageBlock& block);
        
        void CopyBuffer(VkDevice device,
                        VkBuffer src_buffer,
                        VkBuffer dst_buffer,
                        VkDeviceSize dst_offset,
                        VkDeviceSize src_offset,
                        VkDeviceSize size);
        
        VkDevice device_;
        MemoryAllocator& allocator_;
        std::uint32_t queue_family_index_;
        VkScopedObject<VkCommandPool> command_pool_;
        
        std::unordered_map<VkBuffer, MemoryAllocator::StorageBlock> buffer_bindings_;
        std::unordered_map<VkImage, MemoryAllocator::StorageBlock> image_bindings_;

        std::list<VkScopedObject<VkBuffer>> staging_buffer_pool_;
    };
}
