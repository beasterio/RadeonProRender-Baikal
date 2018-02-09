#pragma once

#include "math/float3.h"
#include "SceneGraph/scene1.h"
#include "radeonrays.h"
#include "Vulkan/raytrace.h"
#include "SceneGraph/Collector/collector.h"

#include <vulkan/vulkan.h>
#include "Vulkan/VulkanTexture.hpp"

#include "Vulkan/camera.hpp"

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

    class PipelineLayoutList : public VulkanResourceList<VkPipelineLayout>
    {
    public:
        PipelineLayoutList(vks::VulkanDevice &dev) : VulkanResourceList(dev) {};

        ~PipelineLayoutList()
        {
            for (auto& pipelineLayout : resources)
            {
                vkDestroyPipelineLayout(device.logicalDevice, pipelineLayout.second, nullptr);
            }
        }

        VkPipelineLayout add(std::string name, VkPipelineLayoutCreateInfo &createInfo)
        {
            VkPipelineLayout pipelineLayout;
            VK_CHECK_RESULT(vkCreatePipelineLayout(device.logicalDevice, &createInfo, nullptr, &pipelineLayout));
            resources[name] = pipelineLayout;
            return pipelineLayout;
        }
    };

    class PipelineList : public VulkanResourceList<VkPipeline>
    {
    public:
        PipelineList(vks::VulkanDevice &dev) : VulkanResourceList(dev) {};

        ~PipelineList()
        {
            for (auto& pipeline : resources)
            {
                vkDestroyPipeline(device, pipeline.second, nullptr);
            }
        }

        VkPipeline addGraphicsPipeline(std::string name, VkGraphicsPipelineCreateInfo &pipelineCreateInfo, VkPipelineCache &pipelineCache)
        {
            VkPipeline pipeline;
            VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCreateInfo, nullptr, &pipeline));
            resources[name] = pipeline;
            return pipeline;
        }
    };

    class TextureList : public VulkanResourceList<vks::Texture>
    {
    public:
        TextureList(vks::VulkanDevice &dev, VkQueue queue) :
            VulkanResourceList(dev), queue(queue)
        { };

        ~TextureList()
        {
            for (auto& texture : resources)
            {
                texture.second.destroy();
            }
        }

        vks::Texture2D addTexture2D(std::string name, std::string filename, VkFormat format)
        {
            vks::Texture2D texture;
            texture.loadFromFile(filename, format, &device, queue);
            resources[name] = texture;
            return texture;
        }

        vks::TextureCubeMap addCubemap(std::string name, std::string filename, VkFormat format)
        {
            vks::TextureCubeMap texture;
            texture.loadFromFile(filename, format, &device, queue);
            resources[name] = texture;
            return texture;
        }

        VkQueue queue;
    };

    class DescriptorSetLayoutList : public VulkanResourceList<VkDescriptorSetLayout>
    {
    public:
        DescriptorSetLayoutList(vks::VulkanDevice &dev) : VulkanResourceList(dev) {};

        ~DescriptorSetLayoutList()
        {
            for (auto& descriptorSetLayout : resources)
            {
                vkDestroyDescriptorSetLayout(device, descriptorSetLayout.second, nullptr);
            }
        }

        VkDescriptorSetLayout add(std::string name, VkDescriptorSetLayoutCreateInfo createInfo)
        {
            VkDescriptorSetLayout descriptorSetLayout;
            VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &createInfo, nullptr, &descriptorSetLayout));
            resources[name] = descriptorSetLayout;
            return descriptorSetLayout;
        }
    };

    class DescriptorSetList : public VulkanResourceList<VkDescriptorSet>
    {
    private:
        VkDescriptorPool descriptorPool;
    public:
        DescriptorSetList(vks::VulkanDevice &dev, VkDescriptorPool pool) : VulkanResourceList(dev), descriptorPool(pool) {};

        ~DescriptorSetList()
        {
            for (auto& descriptorSet : resources)
            {
                vkFreeDescriptorSets(device, descriptorPool, 1, &descriptorSet.second);
            }
        }

        VkDescriptorSet add(std::string name, VkDescriptorSetAllocateInfo allocInfo)
        {
            VkDescriptorSet descriptorSet;
            VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &descriptorSet));
            resources[name] = descriptorSet;
            return descriptorSet;
        }
    };

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

        // Better move to material and share among meshes with same material
        VkDescriptorSet descriptorSet;

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

        VkPipelineLayout pipelineLayout;
        VkDescriptorPool descriptorPool;
        VkDescriptorSetLayout descriptorSetLayout;

        uint32_t index_base = 0;

        VkCamera camera;

        struct Resources
        {
            PipelineLayoutList* pipelineLayouts = nullptr;
            PipelineList *pipelines = nullptr;
            DescriptorSetLayoutList *descriptorSetLayouts = nullptr;
            DescriptorSetList * descriptorSets = nullptr;
            TextureList *textures = nullptr;
        } resources;
        void InitResources(vks::VulkanDevice& device, VkQueue queue, const std::string& path)
        {
            //should be called once
            if (resources.pipelineLayouts)
            {
                return;
            }
            resources.pipelineLayouts = new PipelineLayoutList(device);
            resources.pipelines = new PipelineList(device);
            resources.descriptorSetLayouts = new DescriptorSetLayoutList(device);
            resources.descriptorSets = new DescriptorSetList(device, descriptorPool);
            resources.textures = new TextureList(device, queue);

            asset_path = path;

            // Add dummy textures for objects without texture
            resources.textures->addTexture2D("dummy.diffuse", asset_path + "Textures/dummy.dds", VK_FORMAT_BC2_UNORM_BLOCK);
            resources.textures->addTexture2D("dummy.specular", asset_path + "Textures/dummy_specular.dds", VK_FORMAT_BC2_UNORM_BLOCK);
            resources.textures->addTexture2D("dummy.bump", asset_path + "Textures/dummy_ddn.dds", VK_FORMAT_BC2_UNORM_BLOCK);
            resources.textures->addTexture2D("dialectric.metallic", asset_path + "Textures/Dielectric_metallic_TGA_BC2_1.DDS", VK_FORMAT_BC2_UNORM_BLOCK);
        }

        void Clear()
        {
            delete resources.descriptorSets;
            delete resources.descriptorSetLayouts;
            delete resources.pipelineLayouts;
            delete resources.pipelines;
            delete resources.textures;
        }
        std::string asset_path;
    };
}
