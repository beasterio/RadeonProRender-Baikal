#pragma once

#include "resource_list.h"

class PipelineLayoutList : public ResourceList<VkPipelineLayout>
{
public:
    PipelineLayoutList(vks::VulkanDevice& dev) : ResourceList(dev) {};

    ~PipelineLayoutList() {
        for (auto it : _resources) {
            vkDestroyPipelineLayout(_device, it.second, nullptr);
        }
    };
};

class PipelineList : public ResourceList<VkPipeline>
{
public:
    PipelineList(vks::VulkanDevice& dev) : ResourceList(dev), _pipeline_cache(VK_NULL_HANDLE) {
        VkPipelineCacheCreateInfo pipeline_cache_info = {};
        pipeline_cache_info.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;

        VK_CHECK_RESULT(vkCreatePipelineCache(_device, &pipeline_cache_info, nullptr, &_pipeline_cache));
    };

    ~PipelineList() {
        for (auto it : _resources) {
            vkDestroyPipeline(_device, it.second, nullptr);
        }

        vkDestroyPipelineCache(_device, _pipeline_cache, nullptr);
    }

    inline VkPipelineCache GetPipelineCache() { return _pipeline_cache; }
private:
    VkPipelineCache _pipeline_cache;
};