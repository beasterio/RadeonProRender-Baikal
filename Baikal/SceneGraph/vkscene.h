#pragma once

#include "math/float3.h"
#include "SceneGraph/scene1.h"
#include "radeonrays.h"
#include "Vulkan/raytrace.h"
#include "SceneGraph/Collector/collector.h"

#include <vulkan/vulkan.h>
#include "Vulkan/VulkanTexture.hpp"


namespace Baikal
{
    using namespace RadeonRays;

    struct SceneMaterial
    {
        std::string name;
        vks::Texture diffuse;
        vks::Texture roughness;
        vks::Texture metallic;
        vks::Texture bump;

        bool hasAlpha = false;
        bool hasBump = false;
        bool hasRoughness = false;
        bool hasMetaliness = false;

        RadeonRays::float3 baseDiffuse = RadeonRays::float3(-1.0f, -1.0f, -1.0f, -1.0f);
        RadeonRays::float3 baseRoughness = RadeonRays::float3(-1.0f, -1.0f, -1.0f, -1.0f);
        RadeonRays::float3 baseMetallic = RadeonRays::float3(-1.0f, -1.0f, -1.0f, -1.0f);

        VkPipeline pipeline;
    };

    struct SceneMesh
    {
        uint32_t indexCount;
        uint32_t indexBase;

        SceneMaterial *material;
    };

    struct Vertex
    {
        glm::vec3 pos;
        glm::vec2 uv;
        glm::vec3 color;
        glm::vec3 normal;
        glm::vec3 tangent;
    };

    struct RaytraceVertex
    {
        glm::vec3 pos; float pad;
        glm::vec3 normal; float pad0;
        glm::vec2 uv; float pad1[2];
    };

    struct VkScene
    {

        std::unique_ptr<Bundle> material_bundle;
        std::unique_ptr<Bundle> volume_bundle;
        std::unique_ptr<Bundle> texture_bundle;


        std::vector<SceneMaterial> materials;
        std::vector<SceneMesh> meshes;
        std::vector<Raytrace::Shape> raytrace_shapes;
        std::vector<Raytrace::Material> raytrace_materials;

        std::vector<std::vector<RadeonRays::float3> > scene_vertices;
        std::vector<std::vector<uint32_t> > scene_indices;

        vks::Buffer vertex_buffer;
        vks::Buffer index_buffer;
        vks::Buffer raytrace_shape_buffer;
        vks::Buffer raytrace_material_buffer;
        vks::Buffer raytrace_RNG_buffer;
        vks::Buffer raytrace_vertex_buffer;

        std::vector<Vertex> vertices;
        std::vector<RaytraceVertex> raytrace_vertices;
        std::vector<uint32_t> indices;
        uint32_t index_base = 0;
    };
}
