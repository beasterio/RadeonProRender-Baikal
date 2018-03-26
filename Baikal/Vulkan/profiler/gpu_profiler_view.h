#pragma once
#include "vulkan/vulkan.h"

#include <tuple>
#include <vector>
#include <unordered_map>

class GPUProfiler;

class GPUProfilerView
{
public:
    typedef std::pair<uint32_t, uint32_t> QueryPair;
public:
    GPUProfilerView();
    ~GPUProfilerView();
public:
    void Render(GPUProfiler* gpu_profiler);
    GPUProfilerView::QueryPair RegisterQueryPair(const char* label);
private:
    typedef std::pair<uint32_t, uint32_t> QueryPair;
    std::vector<QueryPair> _query_pairs;
    std::unordered_map<uint64_t, const char*> _labels;
};