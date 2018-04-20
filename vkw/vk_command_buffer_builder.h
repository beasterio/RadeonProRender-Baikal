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
#include "vk_scoped_object.h"
#include "vk_pipeline_manager.h"

namespace vkw
{
    using CommandBuffer = VkScopedObject<VkCommandBuffer>;
    class CommandBufferBuilder
    {
    public:
        CommandBufferBuilder(VkDevice device, std::uint32_t queue_family_index);
        
        void BeginCommandBuffer();
        
        void Dispatch(ComputePipeline& pipeline,
                      Shader& shader,
                      std::uint32_t group_size_x,
                      std::uint32_t group_size_y,
                      std::uint32_t group_size_z);
                      //std::uint32_t num_groups_x,
                      //std::uint32_t num_groups_y,
                      //std::uint32_t num_groups_z);
        
        void Barrier(VkBuffer buffer,
                     VkAccessFlags src_access,
                     VkAccessFlags dst_access,
                     VkPipelineStageFlags src_stage,
                     VkPipelineStageFlags dst_stage);
        
        CommandBuffer EndCommandBuffer();
        
    private:
        VkDevice device_ = VK_NULL_HANDLE;
        VkCommandBuffer current_command_buffer_ = VK_NULL_HANDLE;
        VkScopedObject<VkCommandPool> command_pool_ = VK_NULL_HANDLE;
    };
}
