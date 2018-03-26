#include "gpu_profiler_view.h"

#include "gpu_profiler.h"
//#include "imgui_impl_win32_vulkan.h"

GPUProfilerView::GPUProfilerView() {

}

GPUProfilerView::~GPUProfilerView() {
}

void GPUProfilerView::Render(GPUProfiler* gpu_profiler)
{
    ImGui_ImplWin32Vulkan_NewFrame();

    if (!ImGui::Begin("GpuProfiler"))
    {
        ImGui::End();
        return;
    }

    float overall = 0.f;

    for (auto it : _query_pairs)
    {
        uint64_t label_key = uint64_t(it.first) | uint64_t(it.second) << 32;

        float dt = gpu_profiler->GetTimeBetweenQueries(it.first, it.second);
        
        ImGui::LabelText(_labels[label_key], "%f ms", dt);
        overall += dt;
    }

    ImGui::LabelText("Overall", "%f ms", overall);

    ImGui::End();
}

GPUProfilerView::QueryPair GPUProfilerView::RegisterQueryPair(const char* label)
{
    static uint32_t query_id = 0;

    std::pair<uint32_t, uint32_t> pair = { query_id++, query_id++ };

    _query_pairs.push_back(std::pair<uint32_t, uint32_t>(pair.first, pair.second));

    uint64_t label_key = uint64_t(pair.first) | uint64_t(pair.second) << 32;

    _labels[label_key] = label;

    return pair;
}