#pragma once

#include "vulkan/vulkan.h"

class GPUProfiler
{
public:
    GPUProfiler(VkDevice const& device, VkPhysicalDevice const& physical_device, uint32_t query_count);
    ~GPUProfiler();
public:
    void WriteTimestamp(VkCommandBuffer const& command_buffer, uint32_t user_query_idx);
    // Returns time in ms
    float GetTimeBetweenQueries(uint32_t begin_query_idx, uint32_t end_query_idx);
    VkQueryPool GetQueryPool() { return _query_pool[_frame_idx]; }
    void Update() { _frame_idx = (_frame_idx + 1) % 2; }

private:
    VkDevice    _device;
    VkQueryPool _query_pool[2];
    uint32_t    _query_count;
    float       _timestamp_period;

    uint8_t    _frame_idx;
};