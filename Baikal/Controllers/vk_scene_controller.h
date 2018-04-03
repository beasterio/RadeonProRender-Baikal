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

/**
 \file scene_tracker.h
 \author Dmitry Kozlov
 \version 1.0
 \brief Contains SceneTracker class implementation.
 */
#pragma once

#include "scene_controller.h"
#include "SceneGraph/vkscene.h"
#include "radeonrays.h"

namespace Baikal
{
    /**
     \brief Tracks changes of a scene and serialized data into GPU memory when needed.
     
     ClwSceneController class is intended to keep track of CPU side scene changes and update all
     necessary GPU buffers. It essentially establishes a mapping between Scene class and 
     corresponding ClwScene class. It also pre-caches ClwScenes and speeds up loading for 
     already compiled scenes.
     */
    class VkSceneController : public SceneController<VkScene>
    {
    public:
        // Constructor
        VkSceneController(vks::VulkanDevice* device, rr_instance& instance, vks::Buffer* defaultUBO, ResourceManager* resources);
        // Destructor
        virtual ~VkSceneController();

    public:
        // Update camera data only.
        void UpdateCamera(Scene1 const& scene, Collector& mat_collector, Collector& tex_collector, VkScene& out) const override;
        // Update shape data only.
        void UpdateShapes(Scene1 const& scene, Collector& mat_collector, Collector& tex_collector, Collector& vol_collector, VkScene& out) const override;
        // Update transform data only
        void UpdateShapeProperties(Scene1 const& scene, Collector& mat_collector, Collector& tex_collector, VkScene& out) const override;
        // Update lights data only.
        void UpdateLights(Scene1 const& scene, Collector& mat_collector, Collector& tex_collector, VkScene& out) const override;
        // Update material data.
        void UpdateMaterials(Scene1 const& scene, Collector& mat_collector, Collector& tex_collector, VkScene& out) const override;
        // Update texture data only.
        void UpdateTextures(Scene1 const& scene, Collector& mat_collector, Collector& tex_collector, VkScene& out) const override;
        // Get default material
        Material::Ptr GetDefaultMaterial() const override;
        // If m_current_scene changes
        void UpdateCurrentScene(Scene1 const& scene, VkScene& out) const override;
        // Update volume materiuals
        void UpdateVolumes(Scene1 const& scene, Collector& volume_collector, VkScene& out) const override;
        // If scene attributes changed
        void UpdateSceneAttributes(Scene1 const& scene, Collector& tex_collector, VkScene& out) const override;
    private:

        void AllocateOnGPU(Scene1 const& scene, VkCommandBuffer copy_cmd, VkScene& out) const;
        void DeallocateOnGPU(VkScene& out) const;

        void CreateDescriptorSets(VkScene& out) const;


        vks::VulkanDevice* m_vulkan_device;
        rr_instance* m_instance;
        vks::Buffer* m_defaultUBO;
        ResourceManager* m_resources;
        mutable std::vector<rr_shape> m_rr_meshes;
    };
}
