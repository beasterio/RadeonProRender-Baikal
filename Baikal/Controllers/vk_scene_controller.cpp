#include "Controllers/vk_scene_controller.h"
#include <chrono>
#include <memory>
#include <stack>
#include <vector>
#include <array>

using namespace RadeonRays;

namespace Baikal
{
    static std::size_t align16(std::size_t value)
    {
        return (value + 0xF) / 0x10 * 0x10;
    }
     
    VkSceneController::VkSceneController()
    {
    }


    VkSceneController::~VkSceneController()
    {
    }

    void VkSceneController::UpdateCamera(Scene1 const& scene, Collector& mat_collector, Collector& tex_collector, VkScene& out) const
    {
    }

    // Update shape data only.
    void VkSceneController::UpdateShapes(Scene1 const& scene, Collector& mat_collector, Collector& tex_collector, Collector& vol_collector, VkScene& out) const
    {
    }

    // Update transform data only
    void VkSceneController::UpdateShapeProperties(Scene1 const& scene, Collector& mat_collector, Collector& tex_collector, VkScene& out) const
    {
    }

    // Update lights data only.
    void VkSceneController::UpdateLights(Scene1 const& scene, Collector& mat_collector, Collector& tex_collector, VkScene& out) const
    {
    }

    // Update material data.
    void VkSceneController::UpdateMaterials(Scene1 const& scene, Collector& mat_collector, Collector& tex_collector, VkScene& out) const
    {
    }

    // Update texture data only.
    void VkSceneController::UpdateTextures(Scene1 const& scene, Collector& mat_collector, Collector& tex_collector, VkScene& out) const
    {
    }

    // Get default material
    Material::Ptr VkSceneController::GetDefaultMaterial() const
    {
        return nullptr;
    }

    // If m_current_scene changes
    void VkSceneController::UpdateCurrentScene(Scene1 const& scene, VkScene& out) const
    {
    }

    // Update volume materials
    void VkSceneController::UpdateVolumes(Scene1 const& scene, Collector& volume_collector, VkScene& out) const
    {
    }

    // If scene attributes changed
    void VkSceneController::UpdateSceneAttributes(Scene1 const& scene, Collector& tex_collector, VkScene& out) const
    {
    }

}
