#include "Controllers/vk_scene_controller.h"
#include <chrono>
#include <memory>
#include <stack>
#include <vector>
#include <array>
#include <random>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

using namespace RadeonRays;

namespace Baikal
{
    static constexpr uint32_t kRNGBufferSize = 256u * 256u * sizeof(uint32_t);

    static std::size_t align16(std::size_t value)
    {
        return (value + 0xF) / 0x10 * 0x10;
    }
     
    VkSceneController::VkSceneController(vks::VulkanDevice* device, rr_instance& instance)
        : m_instance(instance)
        , m_vulkan_device(device)
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
        int mesh_num = scene.GetNumShapes();
        out.scene_vertices.resize(mesh_num);
        out.scene_indices.resize(mesh_num);

        out.meshes.resize(mesh_num);
        out.raytrace_shapes.resize(mesh_num);

        int startIdx = 0;
        auto mesh_iter = scene.CreateShapeIterator();
        for (int i = 0; mesh_iter->IsValid(); mesh_iter->Next(), ++i)
        {
            Baikal::Mesh::Ptr mesh = mesh_iter->ItemAs<Baikal::Mesh>();
            auto& raytraceShape = out.raytrace_shapes[i];
            raytraceShape.materialIndex = mat_collector.GetItemIndex(mesh->GetMaterial());
            raytraceShape.numTriangles = mesh->GetNumIndices() / 3;

            std::vector<RadeonRays::float3>& meshPositions = out.scene_vertices[i];
            std::vector<uint32_t>& meshIndices = out.scene_indices[i];

            meshPositions.resize(mesh->GetNumVertices());
            meshIndices.resize(mesh->GetNumIndices());

            //std::cout << "Mesh \"" << aMesh->mName.C_Str() << "\"" << std::endl;
            //std::cout << "Material: \"" << materials[numLoadedMaterials + aMesh->mMaterialIndex].name << "\"" << std::endl;
            //std::cout << "Faces: " << aMesh->mNumFaces << std::endl;

            out.meshes[i].indexBase = out.index_base;

            // Vertices
            std::vector<Vertex> vertices;
            vertices.resize(mesh->GetNumVertices());

            bool hasUV = true;
            bool hasTangent = false;

            uint32_t vertexBase = static_cast<uint32_t>(out.vertices.size());

            for (uint32_t i = 0; i < mesh->GetNumVertices(); i++)
            {
                vertices[i].pos = glm::make_vec3(&mesh->GetVertices()[i].x);// *0.5f;
                vertices[i].pos.y = -vertices[i].pos.y;
                vertices[i].uv = (hasUV) ? glm::make_vec2(&mesh->GetUVs()[i].x) : glm::vec2(0.0f);
                vertices[i].normal = glm::make_vec3(&mesh->GetNormals()[i].x);
                vertices[i].normal.y = -vertices[i].normal.y;
                vertices[i].color = glm::vec3(1.0f); // todo : take from material
                //TODO: fix tangent
                //vertices[i].tangent = (hasTangent) ? glm::make_vec3(&aMesh->mTangents[i].x) : glm::vec3(0.0f, 1.0f, 0.0f);
                vertices[i].tangent = glm::vec3(0.0f, 1.0f, 0.0f);
                meshPositions[i] = RadeonRays::float3(vertices[i].pos.x, vertices[i].pos.y, vertices[i].pos.z);
                out.vertices.push_back(vertices[i]);

                RaytraceVertex rtv;
                rtv.pos = vertices[i].pos;
                rtv.normal = vertices[i].normal;
                rtv.uv = vertices[i].uv;
                out.raytrace_vertices.push_back(rtv);
            }

            // Indices
            std::uint32_t const* indices = mesh->GetIndices();
            out.meshes[i].indexCount = mesh->GetNumIndices();
            raytraceShape.indexOffset = static_cast<std::uint32_t>(out.indices.size());
            int num_faces = mesh->GetNumIndices() / 3;
            for (uint32_t i = 0; i < num_faces; i++)
            {
                out.indices.push_back(indices[i * 3] + vertexBase);
                out.indices.push_back(indices[i * 3 + 1] + vertexBase);
                out.indices.push_back(indices[i * 3 + 2] + vertexBase);

                meshIndices[i * 3] = indices[i * 3];
                meshIndices[i * 3 + 1] = indices[i * 3 + 1];
                meshIndices[i * 3 + 2] = indices[i * 3 + 2];
                out.index_base += 3;
            }

            rr_shape rrmesh = nullptr;

            //auto status = rrCreateTriangleMesh(
            //    m_instance,
            //    &meshPositions[0].x,
            //    (std::uint32_t)meshPositions.size(),
            //    sizeof(RadeonRays::float3),
            //    (std::uint32_t*)(&meshIndices[0]),
            //    (std::uint32_t)sizeof(uint32_t),
            //    (std::uint32_t)(meshIndices.size() / 3),
            //    i,
            //    &rrmesh);

            //rrAttachShape(m_instance, rrmesh);
        }

        auto& device = m_vulkan_device->logicalDevice;
        VkQueue graphics_queue;
        vkGetDeviceQueue(device, m_vulkan_device->queueFamilyIndices.graphics, 0, &graphics_queue);
        VkCommandBuffer copy_cmd = m_vulkan_device->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, graphics_queue, false);
        AllocateOnGPU(scene, copy_cmd, out);
        vkFreeCommandBuffers(device, 1, &copy_cmd);

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
        int num_materials = mat_collector.GetNumItems();
        out.materials.resize(num_materials);
        out.raytrace_materials.resize(num_materials);

        int startIdx = 0;

        for (int i = startIdx; i < startIdx + num_materials; i++)
        {
            int matIdx = i - startIdx;

            out.materials[i] = {};
            auto& raytrace_material = out.raytrace_materials[i];

            raytrace_material.diffuse[0] = 1.0f;
            raytrace_material.diffuse[1] = 1.0f;
            raytrace_material.diffuse[2] = 1.0f;

            //out.materials[i].pipeline = resources.pipelines->get("scene.solid");

            //out.materials[i].diffuse = resources.textures->get("dummy.diffuse");
            //out.materials[i].roughness = resources.textures->get("dummy.specular");
            //out.materials[i].metallic = resources.textures->get("dialectric.metallic");
            //out.materials[i].bump = resources.textures->get("dummy.bump");

            out.materials[i].hasBump = false;
            out.materials[i].hasAlpha = false;
            out.materials[i].hasMetaliness = false;
            out.materials[i].hasRoughness = false;
        }

        auto mesh_it = scene.CreateShapeIterator();
        for (int i = 0; mesh_it->IsValid(); mesh_it->Next(), ++i)
        {
            Baikal::Mesh::Ptr mesh = mesh_it->ItemAs<Baikal::Mesh>();
            out.meshes[i].material = &out.materials[mat_collector.GetItemIndex(mesh->GetMaterial())];
        }
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


    void VkSceneController::AllocateOnGPU(Scene1 const& scene, VkCommandBuffer copy_cmd, VkScene& out) const
    {
        auto& device = m_vulkan_device->logicalDevice;
        VkQueue graphics_queue;
        vkGetDeviceQueue(device, m_vulkan_device->queueFamilyIndices.graphics, 0, &graphics_queue);

        size_t vertexDataSize = out.vertices.size() * sizeof(Vertex);
        size_t indexDataSize = out.indices.size() * sizeof(uint32_t);
        auto shapeBufferSize = static_cast<uint32_t>(out.raytrace_shapes.size() * sizeof(Raytrace::Shape));
        auto materialBufferSize = static_cast<uint32_t>(out.raytrace_materials.size() * sizeof(Raytrace::Material));
        auto raytraceVertexDataSize = out.vertices.size() * sizeof(Vertex);

        void *data;

        struct
        {
            struct {
                VkBuffer buffer;
            } vBuffer;
            struct {
                VkBuffer buffer;
            } iBuffer;
            struct {
                VkBuffer buffer;
            } raytraceShapeBuffer;
            struct {
                VkBuffer buffer;
            } raytraceMaterialBuffer;
            struct {
                VkBuffer buffer;
            } raytraceRNGBuffer;
            struct {
                VkBuffer buffer;
            } raytraceVertexBuffer;
        } staging;

        // Generate vertex buffer
        VkBufferCreateInfo vBufferInfo;

        // Staging buffer
        vBufferInfo = vks::initializers::bufferCreateInfo(VK_BUFFER_USAGE_TRANSFER_SRC_BIT, vertexDataSize);
        VK_CHECK_RESULT(vkCreateBuffer(device, VK_MEMORY_CPU_TO_GPU, &vBufferInfo, &staging.vBuffer.buffer));
        //vkGetBufferMemoryRequirements(device, staging.vBuffer.buffer, &memReqs);
        //memAlloc.allocationSize = memReqs.size;
        //memAlloc.memoryTypeIndex = getMemTypeIndex(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
        //VK_CHECK_RESULT(vkAllocateMemory(device, &memAlloc, nullptr, &staging.vBuffer.memory));
        //VK_CHECK_RESULT(vkMapMemory(device, staging.vBuffer.memory, 0, VK_WHOLE_SIZE, 0, &data));
        VK_CHECK_RESULT(vkMapBuffer(device, staging.vBuffer.buffer, 0, VK_WHOLE_SIZE, &data));
        memcpy(data, out.vertices.data(), vertexDataSize);
        vkUnmapBuffer(device, staging.vBuffer.buffer);
        //vkUnmapMemory(device, staging.vBuffer.memory);
        //VK_CHECK_RESULT(vkBindBufferMemory(device, staging.vBuffer.buffer, staging.vBuffer.memory, 0));

        // Target
        vBufferInfo = vks::initializers::bufferCreateInfo(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, vertexDataSize);
        VK_CHECK_RESULT(vkCreateBuffer(device, VK_MEMORY_CPU_TO_GPU, &vBufferInfo, &out.vertex_buffer.buffer));
        //vkGetBufferMemoryRequirements(device, vertexBuffer.buffer, &memReqs);
        //memAlloc.allocationSize = memReqs.size;
        //memAlloc.memoryTypeIndex = getMemTypeIndex(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        //VK_CHECK_RESULT(vkAllocateMemory(device, &memAlloc, nullptr, &vertexBuffer.memory));
        //VK_CHECK_RESULT(vkBindBufferMemory(device, vertexBuffer.buffer, vertexBuffer.memory, 0));
        out.vertex_buffer.size = vertexDataSize;

        // Generate index buffer
        VkBufferCreateInfo iBufferInfo;

        // Staging buffer
        iBufferInfo = vks::initializers::bufferCreateInfo(VK_BUFFER_USAGE_TRANSFER_SRC_BIT, indexDataSize);
        VK_CHECK_RESULT(vkCreateBuffer(device, VK_MEMORY_CPU_TO_GPU, &iBufferInfo, &staging.iBuffer.buffer));
        //vkGetBufferMemoryRequirements(device, staging.iBuffer.buffer, &memReqs);
        //memAlloc.allocationSize = memReqs.size;
        //memAlloc.memoryTypeIndex = getMemTypeIndex(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
        //VK_CHECK_RESULT(vkAllocateMemory(device, &memAlloc, nullptr, &staging.iBuffer.memory));
        //VK_CHECK_RESULT(vkMapMemory(device, staging.iBuffer.memory, 0, VK_WHOLE_SIZE, 0, &data));
        VK_CHECK_RESULT(vkMapBuffer(device, staging.iBuffer.buffer, 0, VK_WHOLE_SIZE, &data));
        memcpy(data, out.indices.data(), indexDataSize);
        vkUnmapBuffer(device, staging.iBuffer.buffer);
        //vkUnmapMemory(device, staging.iBuffer.memory);
        //VK_CHECK_RESULT(vkBindBufferMemory(device, staging.iBuffer.buffer, staging.iBuffer.memory, 0));

        // Target
        iBufferInfo = vks::initializers::bufferCreateInfo(VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, indexDataSize);
        VK_CHECK_RESULT(vkCreateBuffer(device, VK_MEMORY_CPU_TO_GPU, &iBufferInfo, &out.index_buffer.buffer));
        //vkGetBufferMemoryRequirements(device, indexBuffer.buffer, &memReqs);
        //memAlloc.allocationSize = memReqs.size;
        //memAlloc.memoryTypeIndex = getMemTypeIndex(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        //VK_CHECK_RESULT(vkAllocateMemory(device, &memAlloc, nullptr, &indexBuffer.memory));
        //VK_CHECK_RESULT(vkBindBufferMemory(device, indexBuffer.buffer, indexBuffer.memory, 0));
        out.index_buffer.size = indexDataSize;

        // Generate raytrace shape buffer
        VkBufferCreateInfo raytraceShapeBufferInfo;

        // Staging buffer
        raytraceShapeBufferInfo = vks::initializers::bufferCreateInfo(VK_BUFFER_USAGE_TRANSFER_SRC_BIT, shapeBufferSize);
        VK_CHECK_RESULT(vkCreateBuffer(device, VK_MEMORY_CPU_TO_GPU, &raytraceShapeBufferInfo, &staging.raytraceShapeBuffer.buffer));
        //vkGetBufferMemoryRequirements(device, staging.raytraceShapeBuffer.buffer, &memReqs);
        //memAlloc.allocationSize = memReqs.size;
        //memAlloc.memoryTypeIndex = getMemTypeIndex(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
        //VK_CHECK_RESULT(vkAllocateMemory(device, &memAlloc, nullptr, &staging.raytraceShapeBuffer.memory));
        //VK_CHECK_RESULT(vkMapMemory(device, staging.raytraceShapeBuffer.memory, 0, VK_WHOLE_SIZE, 0, &data));
        VK_CHECK_RESULT(vkMapBuffer(device, staging.raytraceShapeBuffer.buffer, 0, VK_WHOLE_SIZE, &data));
        memcpy(data, out.raytrace_shapes.data(), shapeBufferSize);
        vkUnmapBuffer(device, staging.raytraceShapeBuffer.buffer);
        //vkUnmapMemory(device, staging.raytraceShapeBuffer.memory);
        //VK_CHECK_RESULT(vkBindBufferMemory(device, staging.raytraceShapeBuffer.buffer, staging.raytraceShapeBuffer.memory, 0));

        // Target
        raytraceShapeBufferInfo = vks::initializers::bufferCreateInfo(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, shapeBufferSize);
        VK_CHECK_RESULT(vkCreateBuffer(device, VK_MEMORY_CPU_TO_GPU, &raytraceShapeBufferInfo, &out.raytrace_shape_buffer.buffer));
        //vkGetBufferMemoryRequirements(device, raytraceShapeBuffer.buffer, &memReqs);
        //memAlloc.allocationSize = memReqs.size;
        //memAlloc.memoryTypeIndex = getMemTypeIndex(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        //VK_CHECK_RESULT(vkAllocateMemory(device, &memAlloc, nullptr, &raytraceShapeBuffer.memory));
        //VK_CHECK_RESULT(vkBindBufferMemory(device, raytraceShapeBuffer.buffer, raytraceShapeBuffer.memory, 0));
        out.raytrace_shape_buffer.size = shapeBufferSize;

        // Generate raytrace shape buffer
        VkBufferCreateInfo raytraceMaterialBufferInfo;

        // Staging buffer
        raytraceMaterialBufferInfo = vks::initializers::bufferCreateInfo(VK_BUFFER_USAGE_TRANSFER_SRC_BIT, materialBufferSize);
        VK_CHECK_RESULT(vkCreateBuffer(device, VK_MEMORY_CPU_TO_GPU,&raytraceMaterialBufferInfo, &staging.raytraceMaterialBuffer.buffer));
        //vkGetBufferMemoryRequirements(device, staging.raytraceMaterialBuffer.buffer, &memReqs);
        //memAlloc.allocationSize = memReqs.size;
        //memAlloc.memoryTypeIndex = getMemTypeIndex(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
        //VK_CHECK_RESULT(vkAllocateMemory(device, &memAlloc, nullptr, &staging.raytraceMaterialBuffer.memory));
        //VK_CHECK_RESULT(vkMapMemory(device, staging.raytraceMaterialBuffer.memory, 0, VK_WHOLE_SIZE, 0, &data));
        VK_CHECK_RESULT(vkMapBuffer(device, staging.raytraceMaterialBuffer.buffer, 0, VK_WHOLE_SIZE, &data));
        memcpy(data, out.raytrace_materials.data(), materialBufferSize);
        vkUnmapBuffer(device, staging.raytraceMaterialBuffer.buffer);
        //vkUnmapMemory(device, staging.raytraceMaterialBuffer.memory);
        //VK_CHECK_RESULT(vkBindBufferMemory(device, staging.raytraceMaterialBuffer.buffer, staging.raytraceMaterialBuffer.memory, 0));

        // Target
        raytraceMaterialBufferInfo = vks::initializers::bufferCreateInfo(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, materialBufferSize);
        VK_CHECK_RESULT(vkCreateBuffer(device, VK_MEMORY_CPU_TO_GPU, &raytraceMaterialBufferInfo, &out.raytrace_material_buffer.buffer));
        //vkGetBufferMemoryRequirements(device, raytraceMaterialBuffer.buffer, &memReqs);
        //memAlloc.allocationSize = memReqs.size;
        //memAlloc.memoryTypeIndex = getMemTypeIndex(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        //VK_CHECK_RESULT(vkAllocateMemory(device, &memAlloc, nullptr, &raytraceMaterialBuffer.memory));
        //VK_CHECK_RESULT(vkBindBufferMemory(device, raytraceMaterialBuffer.buffer, raytraceMaterialBuffer.memory, 0));
        out.raytrace_material_buffer.size = materialBufferSize;


        // Generate raytrace shape buffer
        VkBufferCreateInfo raytraceRNGBufferInfo;

        // Staging buffer
        raytraceRNGBufferInfo = vks::initializers::bufferCreateInfo(VK_BUFFER_USAGE_TRANSFER_SRC_BIT, kRNGBufferSize);
        VK_CHECK_RESULT(vkCreateBuffer(device, VK_MEMORY_CPU_TO_GPU, &raytraceRNGBufferInfo, &staging.raytraceRNGBuffer.buffer));
        //vkGetBufferMemoryRequirements(device, staging.raytraceRNGBuffer.buffer, &memReqs);
        //memAlloc.allocationSize = memReqs.size;
        //memAlloc.memoryTypeIndex = getMemTypeIndex(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
        //VK_CHECK_RESULT(vkAllocateMemory(device, &memAlloc, nullptr, &staging.raytraceRNGBuffer.memory));
        //VK_CHECK_RESULT(vkMapMemory(device, staging.raytraceRNGBuffer.memory, 0, VK_WHOLE_SIZE, 0, &data));
        VK_CHECK_RESULT(vkMapBuffer(device, staging.raytraceRNGBuffer.buffer, 0, VK_WHOLE_SIZE, &data));
        out.raytrace_RNG_buffer.size = kRNGBufferSize;

        // Generate random data into the buffer
        {
            auto iter = reinterpret_cast<uint32_t*>(data);
            std::random_device random_device;
            std::mt19937 rng(random_device());
            std::uniform_int_distribution<> distribution(1u, 0x7fffffffu);
            std::generate(iter, iter + kRNGBufferSize / sizeof(uint32_t), [&distribution, &rng] { return distribution(rng); });
        }

        vkUnmapBuffer(device, staging.raytraceRNGBuffer.buffer);
        //vkUnmapMemory(device, staging.raytraceRNGBuffer.memory);
        //VK_CHECK_RESULT(vkBindBufferMemory(device, staging.raytraceRNGBuffer.buffer, staging.raytraceRNGBuffer.memory, 0));

        // Target
        raytraceRNGBufferInfo = vks::initializers::bufferCreateInfo(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, kRNGBufferSize);
        VK_CHECK_RESULT(vkCreateBuffer(device, VK_MEMORY_CPU_TO_GPU, &raytraceRNGBufferInfo, &out.raytrace_RNG_buffer.buffer));
        //vkGetBufferMemoryRequirements(device, raytraceRNGBuffer.buffer, &memReqs);
        //memAlloc.allocationSize = memReqs.size;
        //memAlloc.memoryTypeIndex = getMemTypeIndex(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        //VK_CHECK_RESULT(vkAllocateMemory(device, &memAlloc, nullptr, &raytraceRNGBuffer.memory));
        //VK_CHECK_RESULT(vkBindBufferMemory(device, raytraceRNGBuffer.buffer, raytraceRNGBuffer.memory, 0));

        // Staging buffer
        auto raytraceVertexBufferInfo = vks::initializers::bufferCreateInfo(VK_BUFFER_USAGE_TRANSFER_SRC_BIT, raytraceVertexDataSize);
        VK_CHECK_RESULT(vkCreateBuffer(device, VK_MEMORY_CPU_TO_GPU, &raytraceVertexBufferInfo, &staging.raytraceVertexBuffer.buffer));
        //vkGetBufferMemoryRequirements(device, staging.raytraceVertexBuffer.buffer, &memReqs);
        //memAlloc.allocationSize = memReqs.size;
        //memAlloc.memoryTypeIndex = getMemTypeIndex(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
        //VK_CHECK_RESULT(vkAllocateMemory(device, &memAlloc, nullptr, &staging.raytraceVertexBuffer.memory));
        //VK_CHECK_RESULT(vkMapMemory(device, staging.raytraceVertexBuffer.memory, 0, VK_WHOLE_SIZE, 0, &data));
        VK_CHECK_RESULT(vkMapBuffer(device, staging.raytraceVertexBuffer.buffer, 0, VK_WHOLE_SIZE, &data));
        memcpy(data, out.raytrace_vertices.data(), raytraceVertexDataSize);
        vkUnmapBuffer(device, staging.raytraceVertexBuffer.buffer);
        //vkUnmapMemory(device, staging.raytraceVertexBuffer.memory);
        //VK_CHECK_RESULT(vkBindBufferMemory(device, staging.raytraceVertexBuffer.buffer, staging.raytraceVertexBuffer.memory, 0));

        // Target
        raytraceVertexBufferInfo = vks::initializers::bufferCreateInfo(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, raytraceVertexDataSize);
        VK_CHECK_RESULT(vkCreateBuffer(device, VK_MEMORY_CPU_TO_GPU, &raytraceVertexBufferInfo, &out.raytrace_vertex_buffer.buffer));
        //vkGetBufferMemoryRequirements(device, raytraceVertexBuffer.buffer, &memReqs);
        //memAlloc.allocationSize = memReqs.size;
        //memAlloc.memoryTypeIndex = getMemTypeIndex(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        //VK_CHECK_RESULT(vkAllocateMemory(device, &memAlloc, nullptr, &raytraceVertexBuffer.memory));
        //VK_CHECK_RESULT(vkBindBufferMemory(device, raytraceVertexBuffer.buffer, raytraceVertexBuffer.memory, 0));
        out.raytrace_vertex_buffer.size = raytraceVertexDataSize;

        // Copy
        //VkCommandBufferBeginInfo cmdBufInfo = vks::initializers::commandBufferBeginInfo();
        VkCommandBufferUsageFlags flag = 0x0;
        VK_CHECK_RESULT(vkBeginCommandBuffer(copy_cmd, flag));

        VkBufferCopy copyRegion = {};

        copyRegion.size = vertexDataSize;
        vkCmdCopyBuffer(
            copy_cmd,
            staging.vBuffer.buffer,
            out.vertex_buffer.buffer,
            1,
            &copyRegion);

        copyRegion.size = indexDataSize;
        vkCmdCopyBuffer(
            copy_cmd,
            staging.iBuffer.buffer,
            out.index_buffer.buffer,
            1,
            &copyRegion);

        copyRegion.size = shapeBufferSize;
        vkCmdCopyBuffer(
            copy_cmd,
            staging.raytraceShapeBuffer.buffer,
            out.raytrace_shape_buffer.buffer,
            1,
            &copyRegion);

        copyRegion.size = materialBufferSize;
        vkCmdCopyBuffer(
            copy_cmd,
            staging.raytraceMaterialBuffer.buffer,
            out.raytrace_material_buffer.buffer,
            1,
            &copyRegion);

        copyRegion.size = kRNGBufferSize;
        vkCmdCopyBuffer(
            copy_cmd,
            staging.raytraceRNGBuffer.buffer,
            out.raytrace_RNG_buffer.buffer,
            1,
            &copyRegion);

        copyRegion.size = raytraceVertexDataSize;
        vkCmdCopyBuffer(
            copy_cmd,
            staging.raytraceVertexBuffer.buffer,
            out.raytrace_vertex_buffer.buffer,
            1,
            &copyRegion);

        VK_CHECK_RESULT(vkEndCommandBuffer(copy_cmd));

        VkSubmitInfo submitInfo = {};
        //submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &copy_cmd;

        VK_CHECK_RESULT(vkQueueSubmit(graphics_queue, 1, &submitInfo, VK_NULL_HANDLE));
        VK_CHECK_RESULT(vkQueueWaitIdle(graphics_queue));

        vkDestroyBuffer(device, staging.vBuffer.buffer);
        vkDestroyBuffer(device, staging.iBuffer.buffer);
        vkDestroyBuffer(device, staging.raytraceShapeBuffer.buffer);
        vkDestroyBuffer(device, staging.raytraceMaterialBuffer.buffer);
        vkDestroyBuffer(device, staging.raytraceRNGBuffer.buffer);

        // Generate descriptor sets for all meshes
        // todo : think about a nicer solution, better suited per material?

        // Decriptor pool
        //std::vector<VkDescriptorPoolSize> poolSizes;
        //poolSizes.push_back(vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, static_cast<uint32_t>(meshes.size())));
        //poolSizes.push_back(vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, static_cast<uint32_t>(meshes.size() * 4)));
        //poolSizes.push_back(vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, static_cast<uint32_t>(10)));

        //VkDescriptorPoolCreateInfo descriptorPoolInfo =
        //    vks::initializers::descriptorPoolCreateInfo(
        //        static_cast<uint32_t>(poolSizes.size()),
        //        poolSizes.data(),
        //        static_cast<uint32_t>(meshes.size()));

        //VK_CHECK_RESULT(vkCreateDescriptorPool(device, &descriptorPoolInfo, nullptr, &descriptorPool));

        // Shared descriptor set layout
        //std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings;
        //// Binding 0: UBO
        //setLayoutBindings.push_back(vks::initializers::descriptorSetLayoutBinding(
        //    VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        //    VK_SHADER_STAGE_VERTEX_BIT,
        //    0));
        //// Binding 1: Diffuse map
        //setLayoutBindings.push_back(vks::initializers::descriptorSetLayoutBinding(
        //    VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        //    VK_SHADER_STAGE_FRAGMENT_BIT,
        //    1));
        //// Binding 2: Roughness map
        //setLayoutBindings.push_back(vks::initializers::descriptorSetLayoutBinding(
        //    VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        //    VK_SHADER_STAGE_FRAGMENT_BIT,
        //    2));
        //// Binding 3: Bump map
        //setLayoutBindings.push_back(vks::initializers::descriptorSetLayoutBinding(
        //    VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        //    VK_SHADER_STAGE_FRAGMENT_BIT,
        //    3));
        //// Binding 4: Metallic map
        //setLayoutBindings.push_back(vks::initializers::descriptorSetLayoutBinding(
        //    VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        //    VK_SHADER_STAGE_FRAGMENT_BIT,
        //    4));

        //VkDescriptorSetLayoutCreateInfo descriptorLayout =
        //    vks::initializers::descriptorSetLayoutCreateInfo(
        //        setLayoutBindings.data(),
        //        static_cast<uint32_t>(setLayoutBindings.size()));

        //VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorLayout, nullptr, &descriptorSetLayout));

        //VkPipelineLayoutCreateInfo pPipelineLayoutCreateInfo =
        //    vks::initializers::pipelineLayoutCreateInfo(
        //        &descriptorSetLayout,
        //        1);

        //VkPushConstantRange pushConstantRange = vks::initializers::pushConstantRange(VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(pushConsts), 0);
        //pPipelineLayoutCreateInfo.pushConstantRangeCount = 1;
        //pPipelineLayoutCreateInfo.pPushConstantRanges = &pushConstantRange;

        //VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pPipelineLayoutCreateInfo, nullptr, &pipelineLayout));

        // Descriptor sets
        //for (uint32_t i = 0; i < meshes.size(); i++)
        //{
        //    // Descriptor set
        //    VkDescriptorSetAllocateInfo allocInfo =
        //        vks::initializers::descriptorSetAllocateInfo(
        //            descriptorPool,
        //            &descriptorSetLayout,
        //            1);

        //    // Background

        //    VkResult r = vkAllocateDescriptorSets(device, &allocInfo, &meshes[i].descriptorSet);

        //    VK_CHECK_RESULT(r);

        //    std::vector<VkWriteDescriptorSet> writeDescriptorSets;

        //    // Binding 0 : Vertex shader uniform buffer
        //    writeDescriptorSets.push_back(vks::initializers::writeDescriptorSet(
        //        meshes[i].descriptorSet,
        //        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        //        0,
        //        &defaultUBO->descriptor));
        //    // Image bindings
        //    // Binding 0: Color map
        //    writeDescriptorSets.push_back(vks::initializers::writeDescriptorSet(
        //        meshes[i].descriptorSet,
        //        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        //        1,
        //        &meshes[i].material->diffuse.descriptor));
        //    // Binding 1: Roughness
        //    writeDescriptorSets.push_back(vks::initializers::writeDescriptorSet(
        //        meshes[i].descriptorSet,
        //        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        //        2,
        //        &meshes[i].material->roughness.descriptor));
        //    // Binding 2: Normal
        //    writeDescriptorSets.push_back(vks::initializers::writeDescriptorSet(
        //        meshes[i].descriptorSet,
        //        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        //        3,
        //        &meshes[i].material->bump.descriptor));
        //    // Binding 3: Metallic
        //    writeDescriptorSets.push_back(vks::initializers::writeDescriptorSet(
        //        meshes[i].descriptorSet,
        //        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        //        4,
        //        &meshes[i].material->metallic.descriptor));

        //    vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, NULL);
        //}
    }
}
