#pragma once

#include "math/float3.h"
#include "SceneGraph/scene1.h"
#include "radeonrays.h"
#include "Vulkan/raytrace.h"
#include "SceneGraph/Collector/collector.h"

#include <vulkan/vulkan.h>
#include "Vulkan/VulkanTexture.hpp"

#include "Vulkan/camera.hpp"
#include "Vulkan/resources/resource_manager.h"

namespace Baikal
{
    using namespace RadeonRays;

    template <typename T>
    class VulkanResourceList
    {
    public:
        vks::VulkanDevice &device;
        std::unordered_map<std::string, T> resources;
        VulkanResourceList(vks::VulkanDevice &dev) : device(dev) {};
        const T get(std::string name)
        {
            return resources[name];
        }
        T *getPtr(std::string name)
        {
            return &resources[name];
        }
        bool present(std::string name)
        {
            return resources.find(name) != resources.end();
        }
    };

    struct SceneMaterial
    {
        std::string name;
        vks::Texture diffuse;
        vks::Texture roughness;
        vks::Texture metallic;
        vks::Texture bump;

        bool has_alpha = false;
        bool has_bump = false;
        bool has_roughness = false;
        bool has_metaliness = false;

        RadeonRays::float3 base_diffuse = RadeonRays::float3(-1.0f, -1.0f, -1.0f, -1.0f);
        RadeonRays::float3 base_roughness = RadeonRays::float3(-1.0f, -1.0f, -1.0f, -1.0f);
        RadeonRays::float3 base_metallic = RadeonRays::float3(-1.0f, -1.0f, -1.0f, -1.0f);

        VkPipeline pipeline;
    };

    struct SceneMesh
    {
        uint32_t index_count;
        uint32_t index_base;

        // Better move to material and share among meshes with same material
        VkDescriptorSet descriptor_set;

        SceneMaterial *material;
    };

    struct Vertex
    {
        glm::vec4 posUVx;
        glm::vec4 normalUVy;
        glm::vec4 tangent;
    };

    struct RaytraceVertex
    {
        glm::vec3 pos; float pad;
        glm::vec3 normal; float pad0;
        glm::vec2 uv; float pad1[2];
    };
    
    struct VkLight
    {
        glm::vec4 position;
        glm::vec4 target;
        glm::vec4 color;
        glm::mat4 viewMatrix;
    };

    struct PushConsts
    {
        int meshID[4] = { 0 };
        RadeonRays::float3 baseDiffuse = RadeonRays::float3(-1.0f, -1.0f, -1.0f, 1.0f);
        RadeonRays::float3 baseRoughness = RadeonRays::float3(-1.0f, -1.0f, -1.0f, 1.0f);
        RadeonRays::float3 baseMetallic = RadeonRays::float3(-1.0f, -1.0f, -1.0f, 1.0f);
    };


    struct VkScene
    {

        std::unique_ptr<Bundle> material_bundle;
        std::unique_ptr<Bundle> volume_bundle;
        std::unique_ptr<Bundle> texture_bundle;


        std::vector<SceneMaterial> materials;
        std::vector<SceneMesh> meshes;
        std::vector<VkLight> lights;
        std::vector<Raytrace::Shape> raytrace_shapes;
        std::vector<Raytrace::Material> raytrace_materials;
        std::vector<std::vector<RadeonRays::float3> > scene_vertices;
        std::vector<std::vector<uint32_t> > scene_indices;

        vks::Buffer vertex_buffer;
        vks::Buffer index_buffer;

        vks::Buffer raytrace_shape_buffer;
        vks::Buffer raytrace_material_buffer;
        vks::Buffer raytrace_RNG_buffer;
        vks::Buffer raytrace_lights_buffer;

        vks::Buffer mesh_transform_buf;
        size_t transform_alignment;

        std::vector<Vertex> vertices;
        std::vector<RaytraceVertex> raytrace_vertices;
        std::vector<uint32_t> indices;

        VkPipelineLayout pipelineLayout;
        VkDescriptorPool descriptorPool;
        VkDescriptorSetLayout descriptorSetLayout;

        VkDescriptorSetLayout cubemapDescriptorSetLayout;
        VkPipelineLayout cubemapPipelineLayout;

        uint32_t index_base = 0;

        VkCamera camera;

        ResourceManager* resources;
        vks::VulkanDevice* vulkan_device;

        void InitResources(vks::VulkanDevice& device, VkQueue queue, const std::string& path)
        {
            asset_path = path;
            vulkan_device = &device;
        }

        void Clear()
        {

        }

        enum SceneRenderFlags {
            SCENE_RENDER_ALL = 0,
            SCENE_SKIP_ALPHA_OBJECTS,
            SCENE_DEPTH_ONLY,
            SCENE_FORWARD_PASS
        };

        struct CubeViewInfo {
            glm::mat4 projection;
            glm::mat4 view[6];
            glm::vec4 cam_position;
        };

        struct BBox {
            glm::vec4 _min;
            glm::vec4 _max;
        };

        BBox bbox = { glm::vec4(FLT_MAX), glm::vec4(-FLT_MAX) };
        mutable PushConsts pushConsts;

        void WriteToCmdBuffer(VkCommandBuffer cmd_buf, SceneRenderFlags flags = SCENE_RENDER_ALL) const
        {
            BuffersList& buffers_list = resources->GetBuffersList();

            VkDeviceSize offsets[1] = { 0 };

            vkCmdBindVertexBuffers(cmd_buf, 0, 1, &vertex_buffer.buffer, offsets);
            vkCmdBindIndexBuffer(cmd_buf, index_buffer.buffer, 0, VK_INDEX_TYPE_UINT32);

            pushConsts.meshID[0] = 0;
            uint32_t id = 0;
            for (auto mesh : meshes) {
                if (mesh.material->has_alpha && (flags & SCENE_SKIP_ALPHA_OBJECTS))
                    continue;

                if ((flags == SCENE_RENDER_ALL) || (flags == SCENE_FORWARD_PASS))
                {
                    pushConsts.baseDiffuse = mesh.material->base_diffuse;
                    pushConsts.baseRoughness = mesh.material->base_roughness;
                    pushConsts.baseMetallic = mesh.material->base_metallic;

                    uint32_t offset = pushConsts.meshID[0] * static_cast<uint32_t>(transform_alignment);
                    if ((flags == SCENE_RENDER_ALL)) {
                        vkCmdBindDescriptorSets(cmd_buf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &mesh.descriptor_set, 1, &offset);
                        vkCmdPushConstants(cmd_buf, pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConsts), &pushConsts);
                    }
                    else {
                        vkCmdBindDescriptorSets(cmd_buf, VK_PIPELINE_BIND_POINT_GRAPHICS, cubemapPipelineLayout, 0, 1, &mesh.descriptor_set, 1, &offset);
                        vkCmdPushConstants(cmd_buf, cubemapPipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(glm::vec4), sizeof(PushConsts), &pushConsts);
                    }
                }

                vkCmdDrawIndexed(cmd_buf, mesh.index_count, 1, mesh.index_base, 0, 0);

                pushConsts.meshID[0]++;
                //id = pushConsts.meshID[0] * vulkan_device->properties.limits.minUniformBufferOffsetAlignment;
                id = 0;
            }
        }

        std::string asset_path;

        enum DirtyFlags
        {
            NONE = 0,
            CAMERA = 1,
            SHAPES = 1 << 1,
            SHAPE_PROPERTIES = 1 << 2,
            LIGHTS = 1 << 3,
            MATERIALS = 1 << 4,
            TEXTURES = 1 << 5,
            CURRENT_SCENE = 1 << 6,
            VOLUMES = 1 << 7,
            SCENE_ATTRIBUTES = 1 << 8,
        };

        //used to know scene is changed since last render call
        mutable int dirty_flags = DirtyFlags::NONE;
    };
}
