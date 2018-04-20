#include "vk_execution_manager.h"

namespace vkw
{
    void ExecutionManager::Submit(VkCommandBuffer buffer)
    {
        VkSubmitInfo submit_info;
        submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit_info.pNext = nullptr;
        submit_info.commandBufferCount = 1u;
        submit_info.pCommandBuffers = &buffer;
        submit_info.pSignalSemaphores = nullptr;
        submit_info.signalSemaphoreCount = 0u;
        submit_info.pWaitSemaphores = nullptr;
        submit_info.waitSemaphoreCount = 0u;
        submit_info.pWaitDstStageMask = nullptr;
        
        VkQueue queue = nullptr;
        vkGetDeviceQueue(device_, queue_family_index_, 0u, &queue);
        
        vkQueueSubmit(queue, 1u, &submit_info, VK_NULL_HANDLE);
    }
    
    void ExecutionManager::WaitIdle()
    {
        VkQueue queue = nullptr;
        vkGetDeviceQueue(device_, queue_family_index_, 0u, &queue);
        vkQueueWaitIdle(queue);
    }
}
