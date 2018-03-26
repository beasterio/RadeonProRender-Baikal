#pragma once

#include <vector>

#include "vulkan/vulkan.h"

#include "VulkanDevice.hpp"

class RenderPass
{
public:
    RenderPass(vks::VulkanDevice* device) : _signal_finished(VK_NULL_HANDLE), _device(device) {
    
    }

    virtual ~RenderPass() { 
        _dependencies.clear(); 

        // do not forget to deallocate resources!
        assert(_signal_finished == VK_NULL_HANDLE);
    }

    inline void SetDependencies(std::vector<VkSemaphore>& dependencies, std::vector<VkPipelineStageFlags>& stageWaitFlags) {
        _dependencies = dependencies; 
        _stage_wait_flags = stageWaitFlags;
    }
    
    inline void GetDependencies(std::vector<VkSemaphore>& dependencies) { 
        dependencies = _dependencies; 
    }

    inline void GetStageWaitFlags(std::vector<VkPipelineStageFlags>& stageWaitFlags) {
        stageWaitFlags = _stage_wait_flags;
    }

    inline VkSemaphore GetSyncPrimitive() { return _signal_finished; }

    virtual void Execute(VkQueue queue) = 0;
    virtual const char* GetName() = 0;
protected:
    std::vector<VkSemaphore>            _dependencies;
    std::vector<VkPipelineStageFlags>   _stage_wait_flags;
    vks::VulkanDevice*                  _device;

    VkSemaphore                         _signal_finished;
};