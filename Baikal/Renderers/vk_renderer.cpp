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
#include "vk_renderer.h"
#include "SceneGraph/vkscene.h"
#include "Vulkan/profiler/gpu_profiler.h"
#include "Vulkan/rteffects.h"

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

//#define STB_IMAGE_WRITE_IMPLEMENTATION
//#include "stb_image_write.h"

#define ENABLE_VALIDATION true
#define ENABLE_PROFILER false

#define MAX_DEBUG_IMAGES 3

namespace
{
    bool aoEnabled = true;
    bool giEnabled = true;
}

namespace Baikal
{
    using namespace RadeonRays;
    
    VkRenderer::VkRenderer(vks::VulkanDevice* device, rr_instance* instance, ResourceManager* resources)
        : m_vulkan_device(device)
        , m_view_updated(true)
        , m_output_changed(true)
        , m_depth_bias_constant(25.0f)
        , m_depth_bias_slope(25.0f)
        , m_profiler(nullptr)
        , m_rr_instance(instance)
        , m_frame_counter(0)
        , m_resources(resources)
        , m_rte_instance(nullptr)
    {
        PrepareUniformBuffers();
        m_profiler = new GPUProfiler(m_vulkan_device->logicalDevice, m_vulkan_device->physicalDevice, 128);

        CreatePipelineCache();
        SetupDescriptorPool();
        PrepareQuadBuffers();
        SetupDescriptorSetLayout();
        SetupVertexDescriptions();


        VkSemaphoreCreateInfo semaphoreCreateInfo = vks::initializers::semaphoreCreateInfo();

        VK_CHECK_RESULT(vkCreateSemaphore(m_vulkan_device->logicalDevice, &semaphoreCreateInfo, nullptr, &m_semaphores.ao_complete));
        VK_CHECK_RESULT(vkCreateSemaphore(m_vulkan_device->logicalDevice, &semaphoreCreateInfo, nullptr, &m_semaphores.gi_complete));
        VK_CHECK_RESULT(vkCreateSemaphore(m_vulkan_device->logicalDevice, &semaphoreCreateInfo, nullptr, &m_semaphores.bilateral_filter_complete));
        VK_CHECK_RESULT(vkCreateSemaphore(m_vulkan_device->logicalDevice, &semaphoreCreateInfo, nullptr, &m_semaphores.transfer_complete));
    }

    VkRenderer::~VkRenderer()
    {
        delete m_profiler;
        m_profiler = nullptr;
    }

    // Renderer overrides
    void VkRenderer::Clear(RadeonRays::float3 const& val,
               Output& output) const
    {
        m_view_updated = true;

    }

    // Render the scene into the output
    void VkRenderer::Render(VkScene const& scene)
    {
        bool shapes_changed = scene.dirty_flags & VkScene::SHAPES;
        bool uniform_buffers_changed = scene.dirty_flags & VkScene::CAMERA ||
                                        scene.dirty_flags & VkScene::LIGHTS;
        bool scene_changed = scene.dirty_flags & VkScene::CURRENT_SCENE ||
                                scene.dirty_flags & VkScene::SCENE_ATTRIBUTES;
        bool textures_changed = scene.dirty_flags & VkScene::TEXTURES || !m_shadow_pass;

        m_view_updated |= m_output_changed;



        if (shapes_changed || m_output_changed)
        {
            PrepareRayBuffers();
        }
        if (textures_changed)
        {
            PrepareTextureBuffers(&scene);
        }

        if (shapes_changed || scene_changed || m_output_changed || textures_changed)
        {
        }
        if (shapes_changed || scene_changed || m_output_changed)
        {
            PreparePipelines(&scene);
        }

        if (!m_shadow_pass)
        {
            InitPasses(scene);
        }

        if (shapes_changed || scene_changed || m_output_changed || textures_changed)
        {
            BuildCommandBuffers();
            m_gbuffer_pass->BuildCommandBuffer(scene);
            m_shadow_pass->BuildCommandBuffer(scene);

            InitRte(&scene);
        }

        if (shapes_changed || m_output_changed || textures_changed)
        {
            //BuildDeferredCommandBuffer(&scene);
        }
        if (uniform_buffers_changed)
        {
            UpdateUniformBuffers(&scene);
        }



        scene.dirty_flags = VkScene::NONE;
        m_output_changed = false;

        Draw(scene);
        m_view_updated = false;

        ++m_frame_counter;
    }

    // Render single tile
    void VkRenderer::RenderTile(VkScene const& scene,
                    RadeonRays::int2 const& tile_origin,
                    RadeonRays::int2 const& tile_size)
    {

    }

    void VkRenderer::InitPasses(VkScene const& scene)
    {
        SetupDescriptorSet(&scene);

        BuildDrawCommandBuffers();

        auto vk_output = dynamic_cast<VkOutput*>(GetOutput(OutputType::kColor));
        int width = vk_output->width();
        int height = vk_output->height();
        auto device = m_vulkan_device->logicalDevice;
        VkQueue queue;
        vkGetDeviceQueue(device, m_vulkan_device->queueFamilyIndices.graphics, 0, &queue);

        TextureList& texture_list = m_resources->GetTextures();
        texture_list.addCubemap(STATIC_CRC32("EnvMap"), "../Resources/Textures/hdr/pisa_cube.ktx", VK_FORMAT_R16G16B16A16_SFLOAT);
        m_irradiance_grid = new IrradianceGrid("../Resources/", m_vulkan_device, queue, scene);

        // $tmp
        //m_cubemap_render = new CubemapRender(m_vulkan_device, queue, scene);
        //m_cubemap_prefilter = new CubemapPrefilter(GetAssetPath().c_str(), m_vulkan_device, queue, m_resources);

        //m_cubemap = m_cubemap_render->CreateCubemap(m_cubemap_render->_cubemap_face_size, VK_FORMAT_R16G16B16A16_SFLOAT);
        m_shadow_pass = new ShadowRenderPass(m_vulkan_device, m_resources);
        m_gbuffer_pass = new GBufferRenderPass(m_vulkan_device, scene, width, height);
        m_deferred_pass = new DeferredRenderPass(m_vulkan_device,
            *m_gbuffer_pass,
            *m_shadow_pass,
            *m_irradiance_grid,
            vk_output->framebuffers.draw_render_pass,
            textures.filteredTraceResults.descriptor,
            /*m_semaphores.render_complete*/ VK_NULL_HANDLE,
            m_resources);

        std::vector<VkSemaphore> dependencies = {};
        std::vector<VkPipelineStageFlags> waitStageBits = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
        m_gbuffer_pass->SetDependencies(dependencies, waitStageBits);

        m_shadow_pass->BuildCommandBuffer(scene);

        DeferredRenderPass::BuildCommandBufferInfo deferred_pass_build_info = {
            *m_gbuffer_pass
            , vk_output->framebuffers.draw_render_pass
            , vk_output->framebuffers.draw_fb
            , m_command_buffers.drawCmdBuffers
            , aoEnabled
            , giEnabled
            , false
        };
        m_deferred_pass->BuildCommandBuffer(deferred_pass_build_info);
        m_irradiance_grid->Update();

        vks::Framebuffer* g_buffer = m_gbuffer_pass->GetFrameBuffer();
        VkDescriptorImageInfo g_buffer_attachments[3] = {
            vks::initializers::descriptorImageInfo(g_buffer->sampler, g_buffer->attachments[0].view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
            vks::initializers::descriptorImageInfo(g_buffer->sampler, g_buffer->attachments[1].view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
            vks::initializers::descriptorImageInfo(g_buffer->sampler, g_buffer->attachments[2].view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) };

        std::vector<VkWriteDescriptorSet> writeDescriptorSets;
        writeDescriptorSets =
        {
            // Binding 2 : Depth buffer, normals and mesh id
            vks::initializers::writeDescriptorSet(
                m_descriptor_sets.bilateralFilter,
                VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                2,
                &g_buffer_attachments[0]),
        };

        vkUpdateDescriptorSets(device, writeDescriptorSets.size(), writeDescriptorSets.data(), 0, NULL);
    }

    void VkRenderer::SetRandomSeed(std::uint32_t seed)
    {
        
    }

    void VkRenderer::SetOutput(OutputType type, Output* output)
    {
        Renderer<VkScene>::SetOutput(type, output);

        m_output_changed = true;
    }
    void VkRenderer::Draw(VkScene const& scene)
    {
        auto& device = m_vulkan_device->logicalDevice;
        
        VkQueue graphics_queue;
        vkGetDeviceQueue(device, m_vulkan_device->queueFamilyIndices.graphics, 0, &graphics_queue);

        VkQueue compute_queue;
        vkGetDeviceQueue(device, m_vulkan_device->queueFamilyIndices.compute, 0, &compute_queue);

        VkSubmitInfo submit_info = vks::initializers::submitInfo();
#if ENABLE_PROFILER        
        {
            m_profiler_view.Render(m_profiler);
            buildCommandBuffers();

            DeferredRenderPass::BuildCommandBufferInfo deferred_pass_build_info = {
                *m_gbuffer_pass
                , renderPass
                , VulkanExampleBase::frameBuffers[currentBuffer]
                , drawCmdBuffers[currentBuffer]
                , aoEnabled
                , giEnabled
                , debugViewEnabled
            };

            m_deferred_pass->BuildCommandBuffer(deferred_pass_build_info);
        }
#endif

        m_gbuffer_pass->Execute(graphics_queue);
        m_shadow_pass->Execute(graphics_queue);

        VkPipelineStageFlags stageFlags;

        VkSubmitInfo submitInfo = vks::initializers::submitInfo();

        if (m_view_updated)
        {
            // Clear
            {
                submitInfo.pWaitSemaphores = nullptr;
                submitInfo.waitSemaphoreCount = 0;
                stageFlags = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
                submitInfo.pWaitDstStageMask = &stageFlags;
                submitInfo.pSignalSemaphores = nullptr;
                submitInfo.signalSemaphoreCount = 0;
                submitInfo.commandBufferCount = 1;
                submitInfo.pCommandBuffers = &m_command_buffers.rtClear;
                VK_CHECK_RESULT(vkQueueSubmit(compute_queue, 1, &submitInfo, VK_NULL_HANDLE));
            }
        }

        if (giEnabled)
        {
            // GI Ray generation
            VkSemaphore semaphore = m_gbuffer_pass->GetSyncPrimitive();
            submitInfo.pWaitSemaphores = &semaphore;
            submitInfo.waitSemaphoreCount = 1;
            stageFlags = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            submitInfo.pWaitDstStageMask = &stageFlags;
            submitInfo.pSignalSemaphores = &m_semaphores.gi_complete;
            submitInfo.signalSemaphoreCount = 1;
            submitInfo.commandBufferCount = 9;
            submitInfo.pCommandBuffers = &m_command_buffers.gi[0];
            VK_CHECK_RESULT(vkQueueSubmit(compute_queue, 1, &submitInfo, VK_NULL_HANDLE));
        }

        if (aoEnabled)
        {
            // AO Ray generation
            {
                VkSemaphore semaphore = m_gbuffer_pass->GetSyncPrimitive();

                submitInfo.pWaitSemaphores = &semaphore;
                submitInfo.waitSemaphoreCount = giEnabled ? 0 : 1;
                stageFlags = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
                submitInfo.pWaitDstStageMask = &stageFlags;
                submitInfo.pSignalSemaphores = &m_semaphores.ao_complete;
                submitInfo.signalSemaphoreCount = 1;
                submitInfo.commandBufferCount = 3;
                submitInfo.pCommandBuffers = &m_command_buffers.ao[0];
                VK_CHECK_RESULT(vkQueueSubmit(compute_queue, 1, &submitInfo, VK_NULL_HANDLE));
            }
        }

        // Filter rays
        if (aoEnabled || giEnabled)
        {
            static std::vector<VkSemaphore> waitSemaphores;

            waitSemaphores.clear();
            if (aoEnabled) waitSemaphores.push_back(m_semaphores.ao_complete);
            if (giEnabled) waitSemaphores.push_back(m_semaphores.gi_complete);

            submitInfo.pWaitSemaphores = &waitSemaphores[0];
            submitInfo.waitSemaphoreCount = waitSemaphores.size();

            VkPipelineStageFlags stageFlags[] = {
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT
            };

            submitInfo.pWaitDstStageMask = &stageFlags[0];
            submitInfo.pSignalSemaphores = &m_semaphores.bilateral_filter_complete;
            submitInfo.signalSemaphoreCount = 1;
            submitInfo.pCommandBuffers = &m_command_buffers.bilateralFilter;
            submitInfo.commandBufferCount = 1;
            VK_CHECK_RESULT(vkQueueSubmit(compute_queue, 1, &submitInfo, VK_NULL_HANDLE));
        }

        {
            std::vector<VkSemaphore> waitSemaphores = { m_shadow_pass->GetSyncPrimitive() };
            std::vector<VkPipelineStageFlags> stageFlags = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT };

            if (aoEnabled || giEnabled) waitSemaphores.push_back(m_semaphores.bilateral_filter_complete);

            m_deferred_pass->SetDependencies(waitSemaphores, stageFlags);
            m_deferred_pass->Execute(graphics_queue, m_command_buffers.drawCmdBuffers);
        }

        BuffersList& buffers_list = scene.resources->GetBuffersList();
        vks::Buffer sh9_buffer = buffers_list.Get(g_irradiance_grid_buffer_name);

        //std::vector<VkSemaphore> semaphores;
        //_cubemap_render->RenderSceneToCubemap(*scene, camera.position, _cubemap, semaphores);
        //_cubemap_prefilter->GenerateIrradianceSH9Coefficients(_cubemap, sh9_buffer, 0, semaphores);

        VK_CHECK_RESULT(vkQueueWaitIdle(graphics_queue));
    }

    void VkRenderer::RenderScene(VkScene const* scene, VkCommandBuffer cmdBuffer, bool shadow)
    {
        VkDeviceSize offsets[1] = { 0 };

        // Render from global buffer using index offsets
        vkCmdBindVertexBuffers(cmdBuffer, VERTEX_BUFFER_BIND_ID, 1, &scene->vertex_buffer.buffer, offsets);
        vkCmdBindIndexBuffer(cmdBuffer, scene->index_buffer.buffer, 0, VK_INDEX_TYPE_UINT32);
        PushConsts pushConsts;
        for (auto mesh : scene->meshes)
        {
            pushConsts.baseDiffuse = mesh.material->base_diffuse;
            pushConsts.baseRoughness = mesh.material->base_roughness;
            pushConsts.baseMetallic = mesh.material->base_metallic;

            uint32_t offset = pushConsts.meshID[0] * static_cast<uint32_t>(scene->transform_alignment);
            vkCmdBindDescriptorSets(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, scene->pipelineLayout, 0, 1, &mesh.descriptor_set, 1, &offset);
            vkCmdPushConstants(cmdBuffer, scene->pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConsts), &pushConsts);
            //vkCmdDrawIndexed(cmdBuffer, mesh.indexCount, 1, 0, mesh.indexBase, 0);
            vkCmdDrawIndexed(cmdBuffer, mesh.index_count, 1, mesh.index_base, 0, 0);

            pushConsts.meshID[0]++;
        }
    }
    void VkRenderer::PreparePipelines(VkScene const* scene)
    {
        auto& device = m_vulkan_device->logicalDevice;
        auto vk_output = dynamic_cast<VkOutput*>(GetOutput(OutputType::kColor));
        auto const& framebuffers = vk_output->framebuffers;

        // Pipeline for RT clear
        {
            VkComputePipelineCreateInfo computePipelineCreateInfo = vks::initializers::computePipelineCreateInfo(m_pipeline_layouts.rtClear, 0);
            computePipelineCreateInfo.stage = LoadShader(GetAssetPath() + "shaders/rt_clear.comp.spv", VK_SHADER_STAGE_COMPUTE_BIT);

            VK_CHECK_RESULT(vkCreateComputePipelines(device, m_pipeline_cache, 1, &computePipelineCreateInfo, nullptr, &m_pipelines.rtClear));
        }

        // Pipeline for filter
        {
            VkComputePipelineCreateInfo computePipelineCreateInfo = vks::initializers::computePipelineCreateInfo(m_pipeline_layouts.bilateralFilter, 0);
            computePipelineCreateInfo.stage = LoadShader(GetAssetPath() + "shaders/biltateral_filter.comp.spv", VK_SHADER_STAGE_COMPUTE_BIT);

            VK_CHECK_RESULT(vkCreateComputePipelines(device, m_pipeline_cache, 1, &computePipelineCreateInfo, nullptr, &m_pipelines.bilateralFilter));
        }

        // Pipeline for texture repack
        {
            VkComputePipelineCreateInfo computePipelineCreateInfo = vks::initializers::computePipelineCreateInfo(m_pipeline_layouts.textureRepack, 0);
            computePipelineCreateInfo.stage = LoadShader(GetAssetPath() + "shaders/texture_repack.comp.spv", VK_SHADER_STAGE_COMPUTE_BIT);

            VK_CHECK_RESULT(vkCreateComputePipelines(device, m_pipeline_cache, 1, &computePipelineCreateInfo, nullptr, &m_pipelines.textureRepack));
        }
    }

    VkPipelineShaderStageCreateInfo VkRenderer::LoadShader(std::string fileName, VkShaderStageFlagBits stage)
    {
        auto& device = m_vulkan_device->logicalDevice;

        VkPipelineShaderStageCreateInfo shaderStage = {};
        shaderStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shaderStage.stage = stage;
#if defined(VK_USE_PLATFORM_ANDROID_KHR)
        shaderStage.module = vks::tools::loadShader(androidApp->activity->assetManager, fileName.c_str(), device);
#else
        shaderStage.module = vks::tools::loadShader(fileName.c_str(), device);
#endif
        shaderStage.pName = "main"; // todo : make param
        assert(shaderStage.module != VK_NULL_HANDLE);
        m_shader_modules.push_back(shaderStage.module);
        return shaderStage;
    }

    void VkRenderer::CreatePipelineCache()
    {
        auto& device = m_vulkan_device->logicalDevice;

        VkPipelineCacheCreateInfo pipelineCacheCreateInfo = {};
        pipelineCacheCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
        VK_CHECK_RESULT(vkCreatePipelineCache(device, &pipelineCacheCreateInfo, nullptr, &m_pipeline_cache));
    }

    void VkRenderer::SetupDescriptorSetLayout()
    {
        auto& device = m_vulkan_device->logicalDevice;
        {
            // Filter pipeline layout
            {
                std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings = {
                    // Binding 0 : GI storage image
                    vks::initializers::descriptorSetLayoutBinding(
                        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                        VK_SHADER_STAGE_COMPUTE_BIT,
                        0),
                    // Binding 1 : SampleCount
                    vks::initializers::descriptorSetLayoutBinding(
                        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                        VK_SHADER_STAGE_COMPUTE_BIT,
                        1),
                    // Binding 2 : Depth, normals, mesh id
                    vks::initializers::descriptorSetLayoutBinding(
                        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                        VK_SHADER_STAGE_COMPUTE_BIT,
                        2),
                    // Binding 3 : Uniform buffer
                    vks::initializers::descriptorSetLayoutBinding(
                        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                        VK_SHADER_STAGE_COMPUTE_BIT,
                        3),
                    // Binding 4 : GI filter result
                    vks::initializers::descriptorSetLayoutBinding(
                        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                        VK_SHADER_STAGE_COMPUTE_BIT,
                        4)
                };

                VkDescriptorSetLayoutCreateInfo descriptorLayout =
                    vks::initializers::descriptorSetLayoutCreateInfo(
                        setLayoutBindings.data(),
                        setLayoutBindings.size());

                VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorLayout, nullptr, &m_descriptor_set_layouts.bilateralFilter));

                VkPipelineLayoutCreateInfo pPipelineLayoutCreateInfo =
                    vks::initializers::pipelineLayoutCreateInfo(
                        &m_descriptor_set_layouts.bilateralFilter,
                        1);

                VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pPipelineLayoutCreateInfo, nullptr, &m_pipeline_layouts.bilateralFilter));
            }

            // Texture repack pipeline layout
            {
                std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings = {
                    // Binding 0 : Texture to remap
                    vks::initializers::descriptorSetLayoutBinding(
                        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                        VK_SHADER_STAGE_COMPUTE_BIT,
                        0),
                    // Binding 1 :Texture descriptors buffer
                    vks::initializers::descriptorSetLayoutBinding(
                        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                        VK_SHADER_STAGE_COMPUTE_BIT,
                        1),
                    // Binding 2 : buffer 1D
                    vks::initializers::descriptorSetLayoutBinding(
                        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                        VK_SHADER_STAGE_COMPUTE_BIT,
                        2),
                };

                VkDescriptorSetLayoutCreateInfo descriptorLayout =
                    vks::initializers::descriptorSetLayoutCreateInfo(
                        setLayoutBindings.data(),
                        setLayoutBindings.size());

                VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorLayout, nullptr, &m_descriptor_set_layouts.textureRepack));

                VkPipelineLayoutCreateInfo pPipelineLayoutCreateInfo =
                    vks::initializers::pipelineLayoutCreateInfo(
                        &m_descriptor_set_layouts.textureRepack,
                        1);

                VkPushConstantRange pushConstantRange = vks::initializers::pushConstantRange(VK_SHADER_STAGE_COMPUTE_BIT, 2 * sizeof(int), 0);
                pPipelineLayoutCreateInfo.pushConstantRangeCount = 1;
                pPipelineLayoutCreateInfo.pPushConstantRanges = &pushConstantRange;

                VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pPipelineLayoutCreateInfo, nullptr, &m_pipeline_layouts.textureRepack));
            }

            // Clear RT results layout
            {
                std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings = {
                    // Binding 0 : AO storage image
                    vks::initializers::descriptorSetLayoutBinding(
                        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                        VK_SHADER_STAGE_COMPUTE_BIT,
                        0),
                    // Binding 1 : SampleCount image
                    vks::initializers::descriptorSetLayoutBinding(
                        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                        VK_SHADER_STAGE_COMPUTE_BIT,
                        1)
                };

                VkDescriptorSetLayoutCreateInfo descriptorLayout =
                    vks::initializers::descriptorSetLayoutCreateInfo(
                        setLayoutBindings.data(),
                        setLayoutBindings.size());

                VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorLayout, nullptr, &m_descriptor_set_layouts.rtClear));

                VkPipelineLayoutCreateInfo pPipelineLayoutCreateInfo =
                    vks::initializers::pipelineLayoutCreateInfo(
                        &m_descriptor_set_layouts.rtClear,
                        1);

                VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pPipelineLayoutCreateInfo, nullptr, &m_pipeline_layouts.rtClear));
            }
        }
    }
    
    void VkRenderer::SetupDescriptorSet(VkScene const* scene)
    {
        auto& device = m_vulkan_device->logicalDevice;
        auto vk_output = dynamic_cast<VkOutput*>(GetOutput(OutputType::kColor));
        auto const& framebuffers = vk_output->framebuffers;
        int width = framebuffers.deferred->width;
        int height = framebuffers.deferred->height;

        textures.filteredTraceResults.destroy();
        textures.sampleCounters.destroy();
        textures.traceResults.destroy();

        PrepareTextureTarget(&textures.sampleCounters, width, height, VK_FORMAT_R16G16_UINT);
        PrepareTextureTarget(&textures.traceResults, width, height, VK_FORMAT_R16G16B16A16_SFLOAT);
        PrepareTextureTarget(&textures.filteredTraceResults, width, height, VK_FORMAT_R16G16B16A16_SFLOAT);

        std::vector<VkWriteDescriptorSet> writeDescriptorSets;

        // Bilateral filter descriptor set
        {
            VkDescriptorSetAllocateInfo allocInfo =
                vks::initializers::descriptorSetAllocateInfo(
                    descriptorPool,
                    &m_descriptor_set_layouts.bilateralFilter,
                    1);

            VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &m_descriptor_sets.bilateralFilter));

            writeDescriptorSets =
            {
                // Binding 0 : GI
                vks::initializers::writeDescriptorSet(
                    m_descriptor_sets.bilateralFilter,
                    VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                    0,
                    &textures.traceResults.descriptor),
                // Binding 1 : SampleCount
                vks::initializers::writeDescriptorSet(
                    m_descriptor_sets.bilateralFilter,
                    VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                    1,
                    &textures.sampleCounters.descriptor),
                // Binding 3 : Uniform buffer
                vks::initializers::writeDescriptorSet(
                    m_descriptor_sets.bilateralFilter,
                    VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                    3,
                    &m_uniform_buffers.fsLights.descriptor),
                // Binding 4 : Filtered buffer
                vks::initializers::writeDescriptorSet(
                    m_descriptor_sets.bilateralFilter,
                    VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                    4,
                    &textures.filteredTraceResults.descriptor)
            };

            vkUpdateDescriptorSets(device, writeDescriptorSets.size(), writeDescriptorSets.data(), 0, NULL);
        }

        // Texture repack filter descriptor set
        {
            VkDescriptorSetAllocateInfo allocInfo =
                vks::initializers::descriptorSetAllocateInfo(
                    descriptorPool,
                    &m_descriptor_set_layouts.textureRepack,
                    1);

            VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &m_descriptor_sets.textureRepack));

            writeDescriptorSets =
            {
                // Binding 1 :Texture descriptors buffer
                vks::initializers::writeDescriptorSet(
                    m_descriptor_sets.textureRepack,
                    VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                    1,
                    &m_buffers.textures.descriptor),
                // Binding 2 : Texture data
                vks::initializers::writeDescriptorSet(
                    m_descriptor_sets.textureRepack,
                    VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                    2,
                    &m_buffers.textureData.descriptor)
            };

            vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, NULL);
        }

        // Clear RT results descriptor set
        {
            VkDescriptorSetAllocateInfo allocInfo =
                vks::initializers::descriptorSetAllocateInfo(
                    descriptorPool,
                    &m_descriptor_set_layouts.rtClear,
                    1);

            VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &m_descriptor_sets.rtClear));

            writeDescriptorSets =
            {
                // Binding 0 : Storage AO
                vks::initializers::writeDescriptorSet(
                    m_descriptor_sets.rtClear,
                    VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                    0,
                    &textures.traceResults.descriptor),
                vks::initializers::writeDescriptorSet(
                    m_descriptor_sets.rtClear,
                    VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                    1,
                    &textures.sampleCounters.descriptor)
            };

            vkUpdateDescriptorSets(device, writeDescriptorSets.size(), writeDescriptorSets.data(), 0, NULL);
        }
    }
    
    void VkRenderer::PrepareUniformBuffers()
    {
        auto vulkanDevice = m_vulkan_device;

        // Fullscreen vertex shader
        VK_CHECK_RESULT(vulkanDevice->createBuffer(
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            &m_uniform_buffers.vsFullScreen,
            sizeof(uboVS)));

        // Deferred vertex shader
        VK_CHECK_RESULT(vulkanDevice->createBuffer(
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            &m_uniform_buffers.vsOffscreen,
            sizeof(uboOffscreenVS)));

        // Deferred fragment shader
        VK_CHECK_RESULT(vulkanDevice->createBuffer(
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            &m_uniform_buffers.fsLights,
            sizeof(uboFragmentLights)));

        // Deferred vertex shader
        VK_CHECK_RESULT(vulkanDevice->createBuffer(
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            &m_uniform_buffers.vsSceneToCube,
            sizeof(VkScene::CubeViewInfo)));

        // Map persistent
        VK_CHECK_RESULT(m_uniform_buffers.vsFullScreen.map());
        VK_CHECK_RESULT(m_uniform_buffers.vsOffscreen.map());
        VK_CHECK_RESULT(m_uniform_buffers.fsLights.map());
        VK_CHECK_RESULT(m_uniform_buffers.vsSceneToCube.map());

        BuffersList& uniformBuffersList = m_resources->GetBuffersList();
        uniformBuffersList.Set(STATIC_CRC32("Lights"), m_uniform_buffers.fsLights);
        uniformBuffersList.Set(STATIC_CRC32("FullscreenQuad"), m_uniform_buffers.vsFullScreen);
        uniformBuffersList.Set(STATIC_CRC32("Scene"), m_uniform_buffers.vsOffscreen);
        uniformBuffersList.Set(STATIC_CRC32("SceneToCube"), m_uniform_buffers.vsSceneToCube);
    }
    void VkRenderer::UpdateUniformBuffers(VkScene const* scene)
    {
        // Update
        UpdateUniformBufferDeferredMatrices(scene);
        UpdateUniformBuffersScreen();
        UpdateUniformBufferDeferredLights(scene);
    }


    void VkRenderer::SetupDescriptorPool()
    {
        auto& device = m_vulkan_device->logicalDevice;

        std::vector<VkDescriptorPoolSize> poolSizes =
        {
            vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 32), //todo: separate set layouts
            vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 64),
            vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 32),
            vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 32)
        };

        VkDescriptorPoolCreateInfo descriptorPoolInfo =
            vks::initializers::descriptorPoolCreateInfo(
                static_cast<uint32_t>(poolSizes.size()),
                poolSizes.data(),
                32);
        descriptorPoolInfo.flags |= VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        VK_CHECK_RESULT(vkCreateDescriptorPool(device, &descriptorPoolInfo, nullptr, &descriptorPool));
    }

    void VkRenderer::UpdateUniformBuffersScreen()
    {
        uboVS.projection = glm::ortho(0.0f, 1.0f, 0.0f, 1.0f, -1.0f, 1.0f);
        memcpy(m_uniform_buffers.vsFullScreen.mapped, &uboVS, sizeof(uboVS));
    }

    void VkRenderer::UpdateUniformBufferDeferredMatrices(VkScene const* scene)
    {
        auto vk_output = dynamic_cast<VkOutput*>(GetOutput(OutputType::kColor));
        auto const& framebuffers = vk_output->framebuffers;
        auto& camera = scene->camera;
        int width = framebuffers.deferred->width;
        int height = framebuffers.deferred->height;
        float cameraFOV = 60.0f;

        uboOffscreenVS.projection = camera.matrices.perspective;
        uboOffscreenVS.view = camera.matrices.view;
        uboOffscreenVS.prevViewProjection = camera.matrices.prevViewPerspective;
        uboOffscreenVS.params = glm::vec4(float(width), float(height), 0.0f, glm::tan(glm::radians(cameraFOV / 2.0f)));
        uboOffscreenVS.cameraPosition = glm::vec4(camera.position, m_frame_counter);
        memcpy(m_uniform_buffers.vsOffscreen.mapped, &uboOffscreenVS, sizeof(uboOffscreenVS));
    }

    void VkRenderer::UpdateUniformBufferDeferredLights(VkScene const* scene)
    {
        auto& camera = scene->camera;

        float zNear = 0.2f;
        float zFar = 500.0f;
        float lightFOV = 100.0f;
        
        glm::vec4 cameraPosition = glm::vec4(camera.position, 0.0f) * glm::vec4(-1.0f, -1.0f, -1.0f, 1.0f);

        uboFragmentLights.view = camera.matrices.view;
        uboFragmentLights.params = uboOffscreenVS.params;
        uboFragmentLights.invView = glm::inverse(camera.matrices.view);
        uboFragmentLights.invProj = glm::inverse(camera.matrices.perspective);

        for (uint32_t i = 0; i < LIGHT_COUNT, i < scene->lights.size(); i++)
        {
            // mvp from light's pov (for shadows)
            glm::mat4 shadowProj = glm::perspective(glm::radians(lightFOV), 1.0f, zNear, zFar);
            glm::mat4 shadowView = glm::lookAt(glm::vec3(uboFragmentLights.lights[i].position), glm::vec3(uboFragmentLights.lights[i].target), glm::vec3(0.0f, 1.0f, 0.0f));

            glm::mat4 mvp = shadowProj * shadowView;
            uboFragmentLights.lights[i].viewMatrix = mvp;
            uboFragmentLights.lights[i] = scene->lights[i];
        }

        if (m_view_updated)
            uboFragmentLights.viewPos.w = 0;

        float frameCount = uboFragmentLights.viewPos.w;
        uboFragmentLights.viewPos = glm::vec4(camera.position, frameCount) * glm::vec4(-1.0f, 1.0f, -1.0f, 1.0f);
        uboFragmentLights.viewPos.w += 1.0f;

        memcpy(m_uniform_buffers.fsLights.mapped, &uboFragmentLights, sizeof(uboFragmentLights));
    }

    // Prepare a texture target that is used to store compute shader calculations
    void VkRenderer::PrepareTextureTarget(vks::Texture *tex, uint32_t width, uint32_t height, VkFormat format)
    {
        auto& physicalDevice = m_vulkan_device->physicalDevice;
        auto& device = m_vulkan_device->logicalDevice;
        VkFormatProperties formatProperties;

        // Get device properties for the requested texture format
        vkGetPhysicalDeviceFormatProperties(physicalDevice, format, &formatProperties);
        // Check if requested image format supports image storage operations
        assert(formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT);

        // Prepare blit target texture
        tex->width = width;
        tex->height = height;

        VkImageCreateInfo imageCreateInfo = vks::initializers::imageCreateInfo();
        imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
        imageCreateInfo.format = format;
        imageCreateInfo.extent = { width, height, 1 };
        imageCreateInfo.mipLevels = 1;
        imageCreateInfo.arrayLayers = 1;
        imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        // Image will be sampled in the fragment shader and used as storage target in the compute shader
        imageCreateInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
        imageCreateInfo.flags = 0;
        // Sharing mode exclusive means that ownership of the image does not need to be explicitly transferred between the compute and graphics queue
        imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VkMemoryAllocateInfo memAllocInfo = vks::initializers::memoryAllocateInfo();
        VkMemoryRequirements memReqs;

        VK_CHECK_RESULT(vkCreateImage(device, &imageCreateInfo, nullptr, &tex->image));

        vkGetImageMemoryRequirements(device, tex->image, &memReqs);
        memAllocInfo.allocationSize = memReqs.size;
        memAllocInfo.memoryTypeIndex = m_vulkan_device->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        VK_CHECK_RESULT(vkAllocateMemory(device, &memAllocInfo, nullptr, &tex->deviceMemory));
        VK_CHECK_RESULT(vkBindImageMemory(device, tex->image, tex->deviceMemory, 0));

        VkCommandBuffer layoutCmd = m_vulkan_device->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);

        tex->imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        vks::tools::setImageLayout(
            layoutCmd, tex->image,
            VK_IMAGE_ASPECT_COLOR_BIT,
            VK_IMAGE_LAYOUT_UNDEFINED,
            tex->imageLayout);

        VkQueue queue;
        vkGetDeviceQueue(device, m_vulkan_device->queueFamilyIndices.graphics, 0, &queue);
        m_vulkan_device->flushCommandBuffer(layoutCmd, queue, true);

        // Create sampler
        VkSamplerCreateInfo sampler = vks::initializers::samplerCreateInfo();
        sampler.magFilter = VK_FILTER_LINEAR;
        sampler.minFilter = VK_FILTER_LINEAR;
        sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        sampler.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        sampler.addressModeV = sampler.addressModeU;
        sampler.addressModeW = sampler.addressModeU;
        sampler.mipLodBias = 0.0f;
        sampler.maxAnisotropy = 1.0f;
        sampler.compareOp = VK_COMPARE_OP_NEVER;
        sampler.minLod = 0.0f;
        sampler.maxLod = 0.0f;
        sampler.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
        VK_CHECK_RESULT(vkCreateSampler(device, &sampler, nullptr, &tex->sampler));

        // Create image view
        VkImageViewCreateInfo view = vks::initializers::imageViewCreateInfo();
        view.image = VK_NULL_HANDLE;
        view.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view.format = format;
        view.components = { VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_A };
        view.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        view.image = tex->image;
        VK_CHECK_RESULT(vkCreateImageView(device, &view, nullptr, &tex->view));

        // Initialize a descriptor for later use
        tex->descriptor.imageLayout = tex->imageLayout;
        tex->descriptor.imageView = tex->view;
        tex->descriptor.sampler = tex->sampler;
        tex->device = m_vulkan_device;
    }

    void VkRenderer::PrepareQuadBuffers()
    {
        auto& device = m_vulkan_device->logicalDevice;

        struct Vertex {
            float pos[3];
            float uv[2];
            float col[3];
            float normal[3];
            float tangent[3];
        };

        std::vector<Vertex> vertexBuffer;

        vertexBuffer.push_back({ { 1.0f, 1.0f, 0.0f },{ 1.0f, 1.0f },{ 1.0f, 1.0f, 1.0f },{ 0.0f, 0.0f, 0.0f } });
        vertexBuffer.push_back({ { 0.0f, 1.0f, 0.0f },{ 0.0f, 1.0f },{ 1.0f, 1.0f, 1.0f },{ 0.0f, 0.0f, 0.0f } });
        vertexBuffer.push_back({ { 0.0f, 0.0f, 0.0f },{ 0.0f, 0.0f },{ 1.0f, 1.0f, 1.0f },{ 0.0f, 0.0f, 0.0f } });
        vertexBuffer.push_back({ { 1.0f, 0.0f, 0.0f },{ 1.0f, 0.0f },{ 1.0f, 1.0f, 1.0f },{ 0.0f, 0.0f, 0.0f } });

        VK_CHECK_RESULT(m_vulkan_device->createBuffer(
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            vertexBuffer.size() * sizeof(Vertex),
            &models.quad.vertices.buffer,
            &models.quad.vertices.memory,
            vertexBuffer.data()));

        // Setup indices
        std::vector<uint32_t> indexBuffer = { 0,1,2, 2,3,0 };
        for (uint32_t i = 0; i < 3; ++i)
        {
            uint32_t indices[6] = { 0,1,2, 2,3,0 };
            for (auto index : indices)
            {
                indexBuffer.push_back(i * 4 + index);
            }
        }
        models.quad.indexCount = static_cast<uint32_t>(indexBuffer.size());

        VK_CHECK_RESULT(m_vulkan_device->createBuffer(
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            indexBuffer.size() * sizeof(uint32_t),
            &models.quad.indices.buffer,
            &models.quad.indices.memory,
            indexBuffer.data()));

        models.quad.device = device;
    }

    void VkRenderer::PrepareTextureBuffers(VkScene const* scene)
    {
        m_buffers.textures.device = m_vulkan_device->logicalDevice;
        m_buffers.textureData.device = m_vulkan_device->logicalDevice;
        
        m_buffers.textures.destroy();
        m_buffers.textureData.destroy();

        const TextureList& tex_list = scene->resources->GetTextures();
        const uint32_t numTextures = (uint32_t)tex_list.GetNumResources();
        const uint32_t texDescBufferSize = sizeof(Texture) * numTextures;

        VK_CHECK_RESULT(m_vulkan_device->createBuffer(
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            &m_buffers.textures,
            texDescBufferSize));

        uint32_t texDataBufferSize = 0;
        const int numMipsToSkip = 0;

        for (size_t i = 0; i < scene->materials.size(); i++)
        {
            vks::Texture tex = scene->materials[i].diffuse;

            // All texture must have power of 2 size: check
            assert(tex.width && (!(tex.width & (tex.width - 1))));
            assert(tex.height && (!(tex.height & (tex.height - 1))));

            uint32_t width = tex.width >> numMipsToSkip;
            uint32_t height = tex.height >> numMipsToSkip;

            texDataBufferSize += width * height * sizeof(std::uint32_t);
        }

        VK_CHECK_RESULT(m_vulkan_device->createBuffer(
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            &m_buffers.textureData,
            texDataBufferSize));
    }

    void VkRenderer::PrepareRayBuffers()
    {
        auto& device = m_vulkan_device->logicalDevice;
        auto vk_output = dynamic_cast<VkOutput*>(GetOutput(OutputType::kColor));
        auto const& framebuffers = vk_output->framebuffers;
        int width = vk_output->width();
        int height = vk_output->height();

        const uint32_t numRays = width * height;
        const uint32_t rayBufferSize = numRays * sizeof(Ray);
        const uint32_t hitBufferSize = numRays * sizeof(Hit);

        m_buffers.raysStaging.device = device;
        m_buffers.raysLocal.device = device;
        m_buffers.hitsStaging.device = device;
        m_buffers.hitsLocal.device = device;
        
        m_buffers.raysStaging.destroy();
        m_buffers.raysLocal.destroy();
        m_buffers.hitsStaging.destroy();
        m_buffers.hitsLocal.destroy();
        
        VK_CHECK_RESULT(m_vulkan_device->createBuffer(
            VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
            &m_buffers.raysStaging,
            rayBufferSize));

        VK_CHECK_RESULT(m_vulkan_device->createBuffer(
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            &m_buffers.raysLocal,
            rayBufferSize));

        VK_CHECK_RESULT(m_vulkan_device->createBuffer(
            VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
            &m_buffers.hitsStaging,
            hitBufferSize));

        VK_CHECK_RESULT(m_vulkan_device->createBuffer(
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            &m_buffers.hitsLocal,
            hitBufferSize));
    }
    
    void VkRenderer::SetupVertexDescriptions()
    {
        // Vertex layout for the models
        vks::VertexLayout vertexLayout = vks::VertexLayout({
            vks::VERTEX_COMPONENT_POSITION,
            vks::VERTEX_COMPONENT_UV,
            vks::VERTEX_COMPONENT_COLOR,
            vks::VERTEX_COMPONENT_NORMAL,
            vks::VERTEX_COMPONENT_TANGENT,
        });

        // Binding description
        vertices.bindingDescriptions.resize(1);
        vertices.bindingDescriptions[0] =
            vks::initializers::vertexInputBindingDescription(
                VERTEX_BUFFER_BIND_ID,
                vertexLayout.stride(),
                VK_VERTEX_INPUT_RATE_VERTEX);

        // Attribute descriptions
        vertices.attributeDescriptions.resize(5);
        // Location 0: Position
        vertices.attributeDescriptions[0] =
            vks::initializers::vertexInputAttributeDescription(
                VERTEX_BUFFER_BIND_ID,
                0,
                VK_FORMAT_R32G32B32_SFLOAT,
                0);
        // Location 1: Texture coordinates
        vertices.attributeDescriptions[1] =
            vks::initializers::vertexInputAttributeDescription(
                VERTEX_BUFFER_BIND_ID,
                1,
                VK_FORMAT_R32G32_SFLOAT,
                sizeof(float) * 3);
        // Location 2: Color
        vertices.attributeDescriptions[2] =
            vks::initializers::vertexInputAttributeDescription(
                VERTEX_BUFFER_BIND_ID,
                2,
                VK_FORMAT_R32G32B32_SFLOAT,
                sizeof(float) * 5);
        // Location 3: Normal
        vertices.attributeDescriptions[3] =
            vks::initializers::vertexInputAttributeDescription(
                VERTEX_BUFFER_BIND_ID,
                3,
                VK_FORMAT_R32G32B32_SFLOAT,
                sizeof(float) * 8);
        // Location 4: Tangent
        vertices.attributeDescriptions[4] =
            vks::initializers::vertexInputAttributeDescription(
                VERTEX_BUFFER_BIND_ID,
                4,
                VK_FORMAT_R32G32B32_SFLOAT,
                sizeof(float) * 11);

        vertices.inputState = vks::initializers::pipelineVertexInputStateCreateInfo();
        vertices.inputState.vertexBindingDescriptionCount = static_cast<uint32_t>(vertices.bindingDescriptions.size());
        vertices.inputState.pVertexBindingDescriptions = vertices.bindingDescriptions.data();
        vertices.inputState.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertices.attributeDescriptions.size());
        vertices.inputState.pVertexAttributeDescriptions = vertices.attributeDescriptions.data();
    }

    void VkRenderer::BuildCommandBuffers()
    {
        auto& device = m_vulkan_device->logicalDevice;
        auto vk_output = dynamic_cast<VkOutput*>(GetOutput(OutputType::kColor));
        auto const& framebuffers = vk_output->framebuffers;
        int width = vk_output->width();
        int height = vk_output->height();

        // Build clear command buffer
        {
            VkCommandBufferAllocateInfo cmdBufAllocateInfo =
                vks::initializers::commandBufferAllocateInfo(
                    m_vulkan_device->computeCommandPool,
                    VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                    1);

            if (m_command_buffers.rtClear == VK_NULL_HANDLE)
            {
                VK_CHECK_RESULT(vkAllocateCommandBuffers(device, &cmdBufAllocateInfo, &m_command_buffers.rtClear));
            }

            VkCommandBufferBeginInfo cmdBufInfo = vks::initializers::commandBufferBeginInfo();
            VK_CHECK_RESULT(vkBeginCommandBuffer(m_command_buffers.rtClear, &cmdBufInfo));

            vkCmdBindPipeline(m_command_buffers.rtClear, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipelines.rtClear);
            vkCmdBindDescriptorSets(m_command_buffers.rtClear, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipeline_layouts.rtClear, 0, 1, &m_descriptor_sets.rtClear, 0, 0);

            uint32_t groupSize = 16;
            uint32_t groupCountX = (width + groupSize - 1) / groupSize;
            uint32_t groupCountY = (height + groupSize - 1) / groupSize;

            vkCmdDispatch(m_command_buffers.rtClear, groupCountX, groupCountY, 1);

            vkEndCommandBuffer(m_command_buffers.rtClear);
        }

        // Build GI filter cmd buffer
        {
            VkCommandBufferAllocateInfo cmdBufAllocateInfo =
                vks::initializers::commandBufferAllocateInfo(
                    m_vulkan_device->computeCommandPool,
                    VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                    1);

            if (m_command_buffers.bilateralFilter == VK_NULL_HANDLE)
            {
                VK_CHECK_RESULT(vkAllocateCommandBuffers(device, &cmdBufAllocateInfo, &m_command_buffers.bilateralFilter));
            }

            uint32_t groupSize = 16;
            uint32_t groupCountX = (width + groupSize - 1) / groupSize;
            uint32_t groupCountY = (height + groupSize - 1) / groupSize;

            // Build command buffer for ao resolve
            VkCommandBufferBeginInfo cmdBufInfo = vks::initializers::commandBufferBeginInfo();
            VK_CHECK_RESULT(vkBeginCommandBuffer(m_command_buffers.bilateralFilter, &cmdBufInfo));

            m_profiler->WriteTimestamp(m_command_buffers.bilateralFilter, m_filter.first);

            vkCmdBindPipeline(m_command_buffers.bilateralFilter, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipelines.bilateralFilter);
            vkCmdBindDescriptorSets(m_command_buffers.bilateralFilter, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipeline_layouts.bilateralFilter, 0, 1, &m_descriptor_sets.bilateralFilter, 0, 0);

            vkCmdDispatch(m_command_buffers.bilateralFilter, groupCountX, groupCountY, 1);

            m_profiler->WriteTimestamp(m_command_buffers.bilateralFilter, m_filter.second);

            vkEndCommandBuffer(m_command_buffers.bilateralFilter);
        }

        // Build texture repack cmd buffer
        if (m_command_buffers.textureRepack == VK_NULL_HANDLE)
        {
            VkCommandBufferAllocateInfo cmdBufAllocateInfo =
                vks::initializers::commandBufferAllocateInfo(
                    m_vulkan_device->computeCommandPool,
                    VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                    1);

            VK_CHECK_RESULT(vkAllocateCommandBuffers(device, &cmdBufAllocateInfo, &m_command_buffers.textureRepack));
        }
    }

    void VkRenderer::BuildDrawCommandBuffers()
    {
        auto& device = m_vulkan_device->logicalDevice;
        auto vk_output = dynamic_cast<VkOutput*>(GetOutput(OutputType::kColor));
        auto const& framebuffers = vk_output->framebuffers;
        int width = vk_output->width();
        int height = vk_output->height();

        if (!m_command_buffers.drawCmdBuffers)
        {
            VkCommandBufferAllocateInfo cmdBufAllocateInfo =
                vks::initializers::commandBufferAllocateInfo(
                    m_vulkan_device->commandPool,
                    VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                    1);

            VK_CHECK_RESULT(vkAllocateCommandBuffers(device, &cmdBufAllocateInfo, &m_command_buffers.drawCmdBuffers));
        }

        //VkCommandBufferBeginInfo cmdBufInfo = vks::initializers::commandBufferBeginInfo();

        //VkClearValue clearValues[2];
        //clearValues[0].color = { { 0.0f, 0.0f, 0.0f, 0.0f } };
        //clearValues[1].depthStencil = { 1.0f, 0 };

        //VkRenderPassBeginInfo renderPassBeginInfo = vks::initializers::renderPassBeginInfo();
        //renderPassBeginInfo.renderPass = framebuffers.draw_render_pass;
        //renderPassBeginInfo.renderArea.offset.x = 0;
        //renderPassBeginInfo.renderArea.offset.y = 0;
        //renderPassBeginInfo.renderArea.extent.width = width;
        //renderPassBeginInfo.renderArea.extent.height = height;
        //renderPassBeginInfo.clearValueCount = 2;
        //renderPassBeginInfo.pClearValues = clearValues;

        //auto& drawCmdBuffers = m_command_buffers.drawCmdBuffers;
        //bool aoEnabled = true;
        //bool giEnabled = true;

        //{
        //    // Set target frame buffer
        //    renderPassBeginInfo.framebuffer = framebuffers.draw_fb;

        //    VK_CHECK_RESULT(vkBeginCommandBuffer(m_command_buffers.drawCmdBuffers, &cmdBufInfo));

        //    vkCmdBeginRenderPass(drawCmdBuffers, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

        //    VkViewport viewport = vks::initializers::viewport((float)width, (float)height, 0.0f, 1.0f);
        //    vkCmdSetViewport(drawCmdBuffers, 0, 1, &viewport);

        //    VkRect2D scissor = vks::initializers::rect2D(width, height, 0, 0);
        //    vkCmdSetScissor(drawCmdBuffers, 0, 1, &scissor);

        //    VkDeviceSize offsets[1] = { 0 };
        //    vkCmdBindDescriptorSets(drawCmdBuffers, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline_layouts.deferred, 0, 1, &m_descriptor_sets.deferred, 0, NULL);

        //    int drawMode[] = { giEnabled ? 1 : 0, aoEnabled ? 1 : 0 };
        //    vkCmdPushConstants(drawCmdBuffers, m_pipeline_layouts.deferred, VK_SHADER_STAGE_FRAGMENT_BIT, 0, 2 * sizeof(int), drawMode);

        //    // Final composition as full screen quad
        //    vkCmdBindPipeline(drawCmdBuffers, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelines.deferred);
        //    vkCmdBindVertexBuffers(drawCmdBuffers, VERTEX_BUFFER_BIND_ID, 1, &models.quad.vertices.buffer, offsets);
        //    vkCmdBindIndexBuffer(drawCmdBuffers, models.quad.indices.buffer, 0, VK_INDEX_TYPE_UINT32);
        //    vkCmdDrawIndexed(drawCmdBuffers, 6, 1, 0, 0, 0);

            //if (debugViewEnabled)
            //{
            //    vkCmdBindDescriptorSets(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline_layouts.deferred, 0, 1, &m_descriptor_sets.debug, 0, NULL);
            //    vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.debug);
            //    vkCmdDrawIndexed(drawCmdBuffers[i], 6, numDebugImages, 0, 0, 0);
            //}

            //vkCmdEndRenderPass(drawCmdBuffers);

            //VK_CHECK_RESULT(vkEndCommandBuffer(drawCmdBuffers));
        //}
    }

    void VkRenderer::InitRte(VkScene const* scene)
    {
        auto device = m_vulkan_device->logicalDevice;
        auto output = GetOutput(Baikal::OutputType::kColor);
        int height = output->height();
        int width = output->width();

        if (m_rte_instance)
        {
            rteShutdownInstance(m_rte_instance);
            m_rte_instance = nullptr;
        }
        rteInitInstance(m_vulkan_device->logicalDevice, m_vulkan_device->physicalDevice, m_vulkan_device->computeCommandPool, *m_rr_instance, RTE_EFFECT_AO, &m_rte_instance);


        rte_scene rteScene;
        rteScene.camera = m_uniform_buffers.fsLights.descriptor;

        VkDescriptorBufferInfo shapeBufferDescriptor = {
            scene->raytrace_shape_buffer.buffer,
            0,
            scene->raytrace_shape_buffer.size
        };

        VkDescriptorBufferInfo materialBufferDescriptor = {
            scene->raytrace_material_buffer.buffer,
            0,
            scene->raytrace_material_buffer.size
        };

        VkDescriptorBufferInfo indexBufferDescriptor = {
            scene->index_buffer.buffer,
            0,
            scene->index_buffer.size
        };

        VkDescriptorBufferInfo vertexBufferDescriptor = {
            scene->vertex_buffer.buffer,
            0,
            scene->vertex_buffer.size
        };

        VkDescriptorBufferInfo lightsBufferDescriptor = {
            scene->raytrace_lights_buffer.buffer,
            0,
            scene->raytrace_lights_buffer.size
        };

        rteScene.shapes = shapeBufferDescriptor;
        rteScene.vertices = vertexBufferDescriptor;
        rteScene.indices = indexBufferDescriptor;
        rteScene.materials = materialBufferDescriptor;
        rteScene.textures = m_buffers.textureData.descriptor;
        rteScene.textureDescs = m_buffers.textures.descriptor;
        rteScene.lights = lightsBufferDescriptor;
        rteScene.numLights = 3;

        VkDescriptorImageInfo shadow_buffer_attachments[3] = {
            vks::initializers::descriptorImageInfo(m_shadow_pass->GetFrameBuffer(0)->sampler, m_shadow_pass->GetFrameBuffer(0)->attachments[0].view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
            vks::initializers::descriptorImageInfo(m_shadow_pass->GetFrameBuffer(1)->sampler, m_shadow_pass->GetFrameBuffer(1)->attachments[0].view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
            vks::initializers::descriptorImageInfo(m_shadow_pass->GetFrameBuffer(2)->sampler, m_shadow_pass->GetFrameBuffer(2)->attachments[0].view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) };

        rteScene.shadowMap0 = shadow_buffer_attachments[0];
        rteScene.shadowMap1 = shadow_buffer_attachments[1];
        rteScene.shadowMap2 = shadow_buffer_attachments[2];

        rteSetScene(m_rte_instance, &rteScene);

        vks::Framebuffer* g_buffer = m_gbuffer_pass->GetFrameBuffer();

        VkDescriptorImageInfo g_buffer_attachments[3] = {
            vks::initializers::descriptorImageInfo(g_buffer->sampler, g_buffer->attachments[0].view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
            vks::initializers::descriptorImageInfo(g_buffer->sampler, g_buffer->attachments[1].view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
            vks::initializers::descriptorImageInfo(g_buffer->sampler, g_buffer->attachments[2].view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) };

        rte_gbuffer rteGBuffer;
        rteGBuffer.depthAndNormal = g_buffer_attachments[0];
        rteGBuffer.albedo = g_buffer_attachments[1];
        rteGBuffer.pbrInputs = g_buffer_attachments[2];

        rteSetGBuffer(m_rte_instance, &rteGBuffer);

        rte_output rteOutput;
        rteOutput.image = textures.traceResults.descriptor;
        rteOutput.sampleCounter = textures.sampleCounters.descriptor;
        rteOutput.width = width;
        rteOutput.height = height;
        rteSetOutput(m_rte_instance, &rteOutput);

        VkCommandBuffer cmdBuffers[2] = { nullptr, nullptr };
        rteCommit(m_rte_instance, RTE_EFFECT_AO, &cmdBuffers[0]);
        rteCommit(m_rte_instance, RTE_EFFECT_INDIRECT_GLOSSY, &cmdBuffers[1]);

        VkQueue computeQueue;
        vkGetDeviceQueue(device, m_vulkan_device->queueFamilyIndices.compute, 0, &computeQueue);

        // Gbuffer rendering
        VkSubmitInfo submitInfo = vks::initializers::submitInfo();
        submitInfo.pWaitSemaphores = nullptr;
        submitInfo.waitSemaphoreCount = 0;
        VkPipelineStageFlags stageFlags = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        submitInfo.pWaitDstStageMask = &stageFlags;
        submitInfo.pSignalSemaphores = nullptr;
        submitInfo.signalSemaphoreCount = 0;
        submitInfo.pCommandBuffers = cmdBuffers;
        submitInfo.commandBufferCount = 2;
        VK_CHECK_RESULT(vkQueueSubmit(computeQueue, 1, &submitInfo, VK_NULL_HANDLE));
        vkQueueWaitIdle(computeQueue);

        vkFreeCommandBuffers(device, m_vulkan_device->computeCommandPool, 2, cmdBuffers);

        rteCalculate(m_rte_instance, RTE_EFFECT_AO, &m_command_buffers.ao[0], 3, nullptr);
        rteCalculate(m_rte_instance, RTE_EFFECT_INDIRECT_GLOSSY, &m_command_buffers.gi[0], 17, nullptr);
    }
}
