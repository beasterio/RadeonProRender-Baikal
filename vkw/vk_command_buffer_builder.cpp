#include "vk_command_buffer_builder.h"

namespace vkw
{
    CommandBufferBuilder::CommandBufferBuilder(VkDevice device, std::uint32_t queue_family_index)
    : device_(device)
    {
        VkCommandPoolCreateInfo pool_create_info;
        pool_create_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        pool_create_info.pNext = nullptr;
        pool_create_info.flags = 0;
        pool_create_info.queueFamilyIndex = queue_family_index;
        
        VkCommandPool command_pool = nullptr;
        auto res = vkCreateCommandPool(device, &pool_create_info, nullptr, &command_pool);
        
        if (res != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create command pool\n");
        }
        
       command_pool_ = VkScopedObject<VkCommandPool>(command_pool,
                                             [device](VkCommandPool pool)
                                             {
                                                 vkDestroyCommandPool(device, pool, nullptr);
                                             });
    }
    
    void CommandBufferBuilder::BeginCommandBuffer()
    {
        if (current_command_buffer_)
        {
            throw std::runtime_error("CommandBufferManager: Begin command buffer has been already called");
        }
        
        VkCommandBufferAllocateInfo command_buffer_alloc_info;
        command_buffer_alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        command_buffer_alloc_info.pNext = nullptr;
        command_buffer_alloc_info.commandPool = command_pool_;
        command_buffer_alloc_info.commandBufferCount = 1u;
        command_buffer_alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        
        vkAllocateCommandBuffers(device_, &command_buffer_alloc_info, &current_command_buffer_);
        
        VkCommandBufferBeginInfo begin_info;
        begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin_info.pNext = nullptr;
        begin_info.pInheritanceInfo = nullptr;
        begin_info.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
        
        vkBeginCommandBuffer(current_command_buffer_, &begin_info);
    }
    
    void CommandBufferBuilder::Dispatch(ComputePipeline& pipeline,
                  Shader& shader,
                  std::uint32_t num_groups_x,
                  std::uint32_t num_groups_y,
                  std::uint32_t num_groups_z)
    {
        if (!current_command_buffer_)
        {
            throw std::runtime_error("CommandBufferManager: Attempt to record command without calling Begin()");
        }
        
        shader.CommitArgs();
        
        vkCmdBindPipeline(current_command_buffer_, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.pipeline);
        vkCmdBindDescriptorSets(current_command_buffer_,
                                VK_PIPELINE_BIND_POINT_COMPUTE,
                                pipeline.layout,
                                0,
                                1u,
                                shader.descriptor_set.GetObjectPtr(), 0u,
                                nullptr);
        
          vkCmdDispatch(current_command_buffer_, num_groups_x, num_groups_y, num_groups_z);
        
//        VkBufferMemoryBarrier barrier;
//        barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
//        barrier.pNext = nullptr;
//        barrier.buffer = buffer_c;
//        barrier.offset = 0u;
//        barrier.size = VK_WHOLE_SIZE;
//        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
//        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
//        barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
//        barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        
//        vkCmdPipelineBarrier(command_buffer,
//                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
//                             VK_PIPELINE_STAGE_HOST_BIT,
//                             0u,
//                             0u,
//                             nullptr,
//                             1u,
//                             &barrier,
//                             0u,
//                             nullptr);
        
    }
    
    CommandBuffer CommandBufferBuilder::EndCommandBuffer()
    {
        if (!current_command_buffer_)
        {
            throw std::runtime_error("CommandBufferManager: End called without calling Begin");
        }
        
        vkEndCommandBuffer(current_command_buffer_);
        
        auto command_buffer = current_command_buffer_;
        current_command_buffer_ = nullptr;
        
        return CommandBuffer(command_buffer,
                             [device = device_, pool = (VkCommandPool)command_pool_](VkCommandBuffer buffer)
                             {
                                 vkFreeCommandBuffers(device, pool, 1u, &buffer);
                             });
    }
    
    
    void CommandBufferBuilder::Barrier(VkBuffer buffer,
                                       VkAccessFlags src_access,
                                       VkAccessFlags dst_access,
                                       VkPipelineStageFlags src_stage,
                                       VkPipelineStageFlags dst_stage)
    {
        if (!current_command_buffer_)
        {
            throw std::runtime_error("CommandBufferManager: Attempt to record command without calling Begin()");
        }
        
        VkBufferMemoryBarrier barrier;
        barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        barrier.pNext = nullptr;
        barrier.buffer = buffer;
        barrier.offset = 0u;
        barrier.size = VK_WHOLE_SIZE;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.srcAccessMask = src_access;
        barrier.dstAccessMask = dst_access;
        
        vkCmdPipelineBarrier(current_command_buffer_,
                             src_stage,
                             dst_stage,
                             0u,
                             0u,
                             nullptr,
                             1u,
                             &barrier,
                             0u,
                             nullptr);
        
    }
}
