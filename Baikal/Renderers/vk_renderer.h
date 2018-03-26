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

//TODO: remove defines to another place
#define LIGHT_COUNT 3
#define VERTEX_BUFFER_BIND_ID 0

#include <memory>
#include <map>

#include "math/int2.h"
#include "renderer.h"
#include "Vulkan/VulkanDevice.hpp"
#include "Vulkan/VulkanModel.hpp"
#include "Vulkan/render_passes/deferred_render_pass.h"
#include "Vulkan/render_passes/gbuffer_render_pass.h"
#include "Vulkan/render_passes/shadow_render_pass.h"
#include "Vulkan/profiler/gpu_profiler_view.h"
#include "Vulkan/rteffects.h"


#include "Output/vkoutput.h"
#include "radeonrays.h"
#include "SceneGraph/vkscene.h"

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>


class GPUProfiler;

namespace Baikal
{
    class VkOutput;

    ///< Renderer implementation
    class VkRenderer : public Renderer<VkScene>
    {
    public:

        VkRenderer(vks::VulkanDevice* device, rr_instance* instance);

        ~VkRenderer();

        // Renderer overrides
        void Clear(RadeonRays::float3 const& val,
                   Output& output) const override;

        // Render the scene into the output
        void Render(VkScene const& scene) override;

        // Render single tile
        void RenderTile(VkScene const& scene,
                        RadeonRays::int2 const& tile_origin,
                        RadeonRays::int2 const& tile_size) override;

        void SetRandomSeed(std::uint32_t seed) override;
       
        void SetOutput(OutputType type, Output* output)  override;

        rr_instance* GetRRInstance() const { return m_rr_instance; }
        vks::Buffer* GetOffscreenBuffer() { return &m_uniform_buffers.vsOffscreen; }
    protected:
    private:
        void Draw(VkScene const& scene);
        //void BuildDeferredCommandBuffer(VkScene const* scene);
        void BuildDrawCommandBuffers();
        void BuildCommandBuffers();

        void RenderScene(VkScene const* scene, VkCommandBuffer cmdBuffer, bool shadow);
        void PreparePipelines(VkScene const* scene);
        VkPipelineShaderStageCreateInfo LoadShader(std::string fileName, VkShaderStageFlagBits stage);
        void CreatePipelineCache();
        void SetupDescriptorSetLayout();
        void SetupDescriptorSet(VkScene const* scene);
        void SetupDescriptorPool();
        void SetupVertexDescriptions();
        void PrepareUniformBuffers();
        void PrepareTextureTarget(vks::Texture *tex, uint32_t width, uint32_t height, VkFormat format);
        void PrepareQuadBuffers();
        void InitRte(VkScene* scene);

        // recreate buffers for scene textures
        void PrepareTextureBuffers(VkScene const* scene);

        void PrepareRayBuffers();
        void UpdateUniformBuffers(VkScene const* scene);


        void UpdateUniformBuffersScreen();
        void UpdateUniformBufferDeferredMatrices(VkScene const* scene);
        void UpdateUniformBufferDeferredLights(VkScene const* scene);

        vks::VulkanDevice* m_vulkan_device;

        mutable bool m_view_updated;
        mutable bool m_output_changed;
        // List of shader modules created (stored for cleanup)
        std::vector<VkShaderModule> m_shader_modules;

        VkSemaphore m_ao_complete = VK_NULL_HANDLE;
        VkSemaphore m_gi_complete = VK_NULL_HANDLE;
        //VkSemaphore m_ao_resolve_complete = VK_NULL_HANDLE;
        //VkSemaphore m_gi_resolve_complete = VK_NULL_HANDLE;
        VkSemaphore m_bilateral_filter_complete = VK_NULL_HANDLE;
        //VkSemaphore m_bilateral_filter_ao_complete = VK_NULL_HANDLE;
        //VkSemaphore m_offscreen_semaphore = VK_NULL_HANDLE;
        //VkSemaphore m_shadow_semaphore = VK_NULL_HANDLE;
        VkSemaphore m_transfer_complete = VK_NULL_HANDLE;
        //VkSemaphore m_generate_rays_complete = VK_NULL_HANDLE;
        //VkSemaphore m_gi_trace_complete[2] = { VK_NULL_HANDLE };
        //VkSemaphore m_ao_trace_complete = VK_NULL_HANDLE;


        struct
        {
            //VkCommandBuffer deferred = VK_NULL_HANDLE;
            //VkCommandBuffer shadow[LIGHT_COUNT];
            //VkCommandBuffer transferToHost = VK_NULL_HANDLE;
            //VkCommandBuffer ao = VK_NULL_HANDLE;
            //VkCommandBuffer gi = VK_NULL_HANDLE;
            //VkCommandBuffer aoResolve = VK_NULL_HANDLE;
            //VkCommandBuffer giResolve = VK_NULL_HANDLE;
            //VkCommandBuffer aoResolveAndClear = VK_NULL_HANDLE;
            //VkCommandBuffer giResolveAndClear = VK_NULL_HANDLE;
            //VkCommandBuffer bilateralFilter = VK_NULL_HANDLE;
            //VkCommandBuffer bilateralFilterAO = VK_NULL_HANDLE;
            //VkCommandBuffer textureRepack = VK_NULL_HANDLE;
            //VkCommandBuffer debug = VK_NULL_HANDLE;
            //VkCommandBuffer traceRays = VK_NULL_HANDLE;
            //VkCommandBuffer dbg_transferRaysToHost = VK_NULL_HANDLE;
            VkCommandBuffer drawCmdBuffers = VK_NULL_HANDLE;
            VkCommandBuffer ao[3] = { VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE };
            VkCommandBuffer gi[17] = { VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE };
            VkCommandBuffer bilateralFilter = VK_NULL_HANDLE;
            VkCommandBuffer textureRepack = VK_NULL_HANDLE;
            VkCommandBuffer rtClear = VK_NULL_HANDLE;
        } m_command_buffers;

        ShadowRenderPass*   m_shadow_pass = nullptr;
        GBufferRenderPass*  m_gbuffer_pass = nullptr;
        DeferredRenderPass* m_deferred_pass = nullptr;
        IrradianceGrid*     m_irradiance_grid = nullptr;

        GPUProfilerView::QueryPair m_ao_query;
        GPUProfilerView::QueryPair m_gi_query;
        GPUProfilerView::QueryPair m_filter;

        struct
        {
            //VkPipeline deferred = VK_NULL_HANDLE;
            //VkPipeline offscreen = VK_NULL_HANDLE;
            //VkPipeline shadow = VK_NULL_HANDLE;
            //VkPipeline ao = VK_NULL_HANDLE;
            //VkPipeline gi = VK_NULL_HANDLE;
            //VkPipeline aoResolve = VK_NULL_HANDLE;
            //VkPipeline giResolve = VK_NULL_HANDLE;
            //VkPipeline bilateralFilter = VK_NULL_HANDLE;
            //VkPipeline textureRepack = VK_NULL_HANDLE;
            //VkPipeline debug = VK_NULL_HANDLE;
            VkPipeline bilateralFilter = VK_NULL_HANDLE;
            VkPipeline textureRepack = VK_NULL_HANDLE;
            VkPipeline rtClear = VK_NULL_HANDLE;
        } m_pipelines;

        struct {
            //VkPipelineLayout deferred = VK_NULL_HANDLE;
            //VkPipelineLayout offscreen = VK_NULL_HANDLE;
            //VkPipelineLayout shadow = VK_NULL_HANDLE;
            //VkPipelineLayout generateRays = VK_NULL_HANDLE;
            //VkPipelineLayout aoResolve = VK_NULL_HANDLE;
            //VkPipelineLayout giResolve = VK_NULL_HANDLE;
            //VkPipelineLayout bilateralFilter = VK_NULL_HANDLE;
            //VkPipelineLayout textureRepack = VK_NULL_HANDLE;
            VkPipelineLayout bilateralFilter = VK_NULL_HANDLE;
            VkPipelineLayout textureRepack = VK_NULL_HANDLE;
            VkPipelineLayout rtClear = VK_NULL_HANDLE;
        } m_pipeline_layouts;

        struct {
            //VkDescriptorSetLayout deferred = VK_NULL_HANDLE;
            //VkDescriptorSetLayout offscreen = VK_NULL_HANDLE;
            //VkDescriptorSetLayout shadow = VK_NULL_HANDLE;
            //VkDescriptorSetLayout generateRays = VK_NULL_HANDLE;
            //VkDescriptorSetLayout aoResolve = VK_NULL_HANDLE;
            //VkDescriptorSetLayout giResolve = VK_NULL_HANDLE;
            //VkDescriptorSetLayout bilateralFilter = VK_NULL_HANDLE;
            //VkDescriptorSetLayout textureRepack = VK_NULL_HANDLE;
            VkDescriptorSetLayout bilateralFilter;
            VkDescriptorSetLayout textureRepack;
            VkDescriptorSetLayout rtClear;
        } m_descriptor_set_layouts;

        struct {
            //VkDescriptorSet deferred = VK_NULL_HANDLE;
            //VkDescriptorSet shadow = VK_NULL_HANDLE;
            //VkDescriptorSet ao = VK_NULL_HANDLE;
            //VkDescriptorSet gi = VK_NULL_HANDLE;
            //VkDescriptorSet aoResolve = VK_NULL_HANDLE;
            //VkDescriptorSet giResolve = VK_NULL_HANDLE;
            //VkDescriptorSet bilateralFilter = VK_NULL_HANDLE;
            //VkDescriptorSet bilateralFilterAO = VK_NULL_HANDLE;
            //VkDescriptorSet debug = VK_NULL_HANDLE;
            //VkDescriptorSet textureRepack = VK_NULL_HANDLE;
            VkDescriptorSet bilateralFilter = VK_NULL_HANDLE;
            VkDescriptorSet debug = VK_NULL_HANDLE;
            VkDescriptorSet textureRepack = VK_NULL_HANDLE;
            VkDescriptorSet rtClear = VK_NULL_HANDLE;
        } m_descriptor_sets;

        struct
        {
            vks::Buffer raysStaging;
            vks::Buffer raysLocal;
            vks::Buffer hitsStaging;
            vks::Buffer hitsLocal;
            vks::Buffer textures;
            vks::Buffer textureData;
        } m_buffers;

        struct {
            vks::Buffer vsFullScreen;
            vks::Buffer vsOffscreen;
            vks::Buffer fsLights;
        } m_uniform_buffers;

        struct {
            glm::mat4 projection;
            glm::mat4 view;
            glm::mat4 prevViewProjection;
            glm::vec4 params;			// x,y - viewport dimensions, w - atan(fov / 2.0f)
            glm::vec4 cameraPosition;
        } uboVS, uboOffscreenVS;

        struct {
            glm::vec4 viewPos;
            glm::mat4 view;
            glm::mat4 invView;
            glm::mat4 invProj;
            glm::vec4 params;			// x,y - viewport dimensions, w - atan(fov / 2.0f)
            VkLight lights[LIGHT_COUNT];
        } uboFragmentLights;

        struct {
            VkFence transferToHost;
        } fences;

        struct {
            VkPipelineVertexInputStateCreateInfo inputState;
            std::vector<VkVertexInputBindingDescription> bindingDescriptions;
            std::vector<VkVertexInputAttributeDescription> attributeDescriptions;
        } vertices;

        struct {
            //vks::Texture2D ao;
            //vks::Texture2D filteredAO;
            //vks::Texture2D gi;
            //vks::Texture2D filteredGI;
            vks::Texture2D sampleCounters;       // xyz - GI and w - AO
            vks::Texture2D traceResults;         // xyz - GI and w - AO
            vks::Texture2D filteredTraceResults; // xyz - GI and w - AO
        } textures;

        VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
        VkPipelineCache m_pipeline_cache;

        // Depth bias (and slope) are used to avoid shadowing artifacts
        float m_depth_bias_constant;
        float m_depth_bias_slope;
        uint32_t m_frame_counter;

        rr_instance* m_rr_instance;
        rte_instance m_rte_instance;
        GPUProfiler* m_profiler;
        //GPUProfilerView m_profiler_view;

        struct
        {
            vks::Model quad;
        } models;

    };
}
