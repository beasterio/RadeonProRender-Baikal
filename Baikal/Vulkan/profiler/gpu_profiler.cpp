#include "gpu_profiler.h"

#include <assert.h>

#include <stdio.h>

GPUProfiler::GPUProfiler(VkDevice const& device, VkPhysicalDevice const& physical_device, uint32_t query_count) : _device(device), _query_count(query_count), _frame_idx(0) {

    VkQueryPoolCreateInfo info;
    info.flags = 0;
    info.pipelineStatistics = 0;
    info.pNext = NULL;
    info.queryCount = query_count;
    info.queryType = VK_QUERY_TYPE_TIMESTAMP;
    info.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;

    for (int i = 0; i < 2; i++)
    {
        VkResult r = vkCreateQueryPool(_device, &info, NULL, &_query_pool[i]);
        assert(r == VK_SUCCESS);
    }

    VkPhysicalDeviceProperties physical_device_properties;
    vkGetPhysicalDeviceProperties(physical_device, &physical_device_properties);

    _timestamp_period = physical_device_properties.limits.timestampPeriod;
}

GPUProfiler::~GPUProfiler() {

    for (int i = 0; i < 2; i++)
    {
        vkDestroyQueryPool(_device, _query_pool[i], nullptr);
    }
}

void GPUProfiler::WriteTimestamp(VkCommandBuffer const& command_buffer, uint32_t user_query_idx) {

    assert(user_query_idx != -1);
    assert(user_query_idx < _query_count);

    vkCmdResetQueryPool(command_buffer, _query_pool[_frame_idx], user_query_idx, 1);
    vkCmdWriteTimestamp(command_buffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, _query_pool[_frame_idx], user_query_idx);
}

float GPUProfiler::GetTimeBetweenQueries(uint32_t begin_query_idx, uint32_t end_query_idx) {

    assert(begin_query_idx != -1);
    assert(end_query_idx != -1);

    uint64_t begin_time[2] = { 0, 0 };
    uint64_t end_time[2] = { 0, 0 };

    VkResult result;
    uint8_t prev_frame_idx = (_frame_idx + 1) % 2;

    result = vkGetQueryPoolResults(_device, _query_pool[prev_frame_idx], begin_query_idx, 1, sizeof(uint64_t) * 2, &begin_time, 0, VK_QUERY_RESULT_WITH_AVAILABILITY_BIT | VK_QUERY_RESULT_64_BIT);

    if (begin_time[1] != 1 || result != VK_SUCCESS)
        return 0.f;

    result = vkGetQueryPoolResults(_device, _query_pool[prev_frame_idx], end_query_idx, 1, sizeof(uint64_t) * 2, &end_time, 0, VK_QUERY_RESULT_WITH_AVAILABILITY_BIT | VK_QUERY_RESULT_64_BIT);

    if (end_time[1] != 1 || result != VK_SUCCESS)
        return 0.f;

    return end_time[0] < begin_time[0] ? 0 : static_cast<float>((end_time[0] - begin_time[0]) * _timestamp_period) / 1e6f;
}