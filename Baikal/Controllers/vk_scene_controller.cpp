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

    bool MaterialInputValid(const std::string& input_name, Baikal::Material::Ptr mat)
    {
        bool valid = false;
        Baikal::SingleBxdf* single_bxdf = dynamic_cast<Baikal::SingleBxdf*>(mat.get());
        Baikal::MultiBxdf* multi_bxdf = dynamic_cast<Baikal::MultiBxdf*>(mat.get());
        if (multi_bxdf) single_bxdf = dynamic_cast<Baikal::SingleBxdf*>(multi_bxdf->GetInputValue("base_material").mat_value.get());
        if (!single_bxdf)
        {
            return false;
        }

        auto input_value = single_bxdf->GetInputValue(input_name);
        switch (input_value.type)
        {
        case Baikal::Material::InputType::kTexture:
            valid = input_value.tex_value ? true : false;
            break;
        case Baikal::Material::InputType::kFloat4:
            valid = true;
            break;
        case Baikal::Material::InputType::kMaterial:
        case Baikal::Material::InputType::kUint:
            valid = false;
            break;
        default:
            throw std::runtime_error("Error: unexpected input type.");

        }

        return valid;
    }
    vks::Texture TranslateMaterialInput(vks::VulkanDevice* device, const std::string& input_name, Baikal::Material::Ptr mat, VkScene::Resources& res)
    {
        vks::Texture result;

        VkQueue queue;
        vkGetDeviceQueue(device->logicalDevice, device->queueFamilyIndices.graphics, 0, &queue);
        Baikal::SingleBxdf* single_bxdf = dynamic_cast<Baikal::SingleBxdf*>(mat.get());
        Baikal::MultiBxdf* multi_bxdf = dynamic_cast<Baikal::MultiBxdf*>(mat.get());
        if (multi_bxdf) single_bxdf = dynamic_cast<Baikal::SingleBxdf*>(multi_bxdf->GetInputValue("base_material").mat_value.get());

        //get diffuse tex
        if (single_bxdf)
        {
            auto input_value = single_bxdf->GetInputValue(input_name);
            switch (input_value.type)
            {
            case Baikal::Material::InputType::kTexture:
            {
                Texture::Ptr tex = input_value.tex_value;
                std::string name = input_value.tex_value->GetName();
                if (!res.textures->present(name))
                {
                    int w = tex->GetSize().x;
                    int h = tex->GetSize().y;
                    VkFormat format;
                    switch (tex->GetFormat())
                    {
                    case Texture::Format::kRgba16:
                        format = VK_FORMAT_R16G16B16A16_UNORM;
                        break;
                    case Texture::Format::kRgba32:
                        format = VK_FORMAT_R32G32B32A32_UINT;
                        break;
                    case Texture::Format::kRgba8:
                        format = VK_FORMAT_B8G8R8A8_UNORM;
                        break;
                    default:
                        throw std::runtime_error("Error: unexpected Baikal::Texture format.");
                    }
                    //res.textures->addTexture2D(name, name, VK_FORMAT_BC2_UNORM_BLOCK);
                    //format = VK_FORMAT_BC2_UNORM_BLOCK;
                    res.textures->addTexture2D(name, tex->GetData(), (VkDeviceSize)tex->GetSizeInBytes(), format, w, h, device, queue);
                }
                result = res.textures->get(name);
                break;
            }
            case Baikal::Material::InputType::kFloat4:
            {
                std::string name = mat->GetName() + "_" + input_name + "_" +"kFloat4";
                if (!res.textures->present(name))
                {
                    RadeonRays::float4 color = input_value.float_value;// color /= 255.f;
                    color = { color.z, color.y, color.x, 1.f };
                    int w = 1;
                    int h = 1;
                    VkFormat format = VK_FORMAT_R32G32B32A32_SFLOAT;
                    res.textures->addTexture2D(name, &color.x, sizeof(color), format, w, h, device, queue);
                }
                result = res.textures->get(name);
            }
            break;
            default:
                throw std::runtime_error("Error: unexpected input type.");
            }
        }
        //complicated material
        else
        {
            std::string name = mat->GetName() + "_" + input_name + "_" + "kFloat4";
            if (!res.textures->present(name))
            {
                RadeonRays::float4 color = { 0.f, 0.f, 1.f, 1.f };
                int w = 1;
                int h = 1;
                VkFormat format = VK_FORMAT_R32G32B32A32_SFLOAT;
                res.textures->addTexture2D(name, &color.x, sizeof(color), format, w, h, device, queue);
            }
            result = res.textures->get(name);
        }

        return result;
    }

    uint32_t GetMemTypeIndex(VkPhysicalDevice physicalDevice, uint32_t typeBits, VkFlags properties)
    {
        VkPhysicalDeviceMemoryProperties deviceMemProps;
        vkGetPhysicalDeviceMemoryProperties(physicalDevice, &deviceMemProps);

        for (uint32_t i = 0; i < 32; i++)
        {
            if ((typeBits & 1) == 1)
            {
                if ((deviceMemProps.memoryTypes[i].propertyFlags & properties) == properties)
                {
                    return i;
                }
            }
            typeBits >>= 1;
        }

        // todo: throw if no appropriate mem type was found
        return 0;
    }
     
    VkSceneController::VkSceneController(vks::VulkanDevice* device, rr_instance& instance, vks::Buffer* defaultUBO)
        : m_instance(instance)
        , m_vulkan_device(device)
        , m_defaultUBO(defaultUBO)
    {
    }


    VkSceneController::~VkSceneController()
    {
    }

    void VkSceneController::UpdateCamera(Scene1 const& scene, Collector& mat_collector, Collector& tex_collector, VkScene& out) const
    {
        const Baikal::PerspectiveCamera* baikal_cam = dynamic_cast<const Baikal::PerspectiveCamera*>(scene.GetCamera().get());
        RadeonRays::float3 pos = baikal_cam->GetPosition();
        RadeonRays::float3 at = baikal_cam->GetForwardVector() + pos;
        RadeonRays::float3 up = baikal_cam->GetUpVector();
        float aspect = baikal_cam->GetAspectRatio();
        //float zNear = baikal_cam->GetDepthRange().x;
        //float zFar = baikal_cam->GetDepthRange().y;
        float zNear = 0.2;
        float zFar = 10000;
        float FOV = 60.0f;
        out.camera.setPerspective(FOV, aspect, zNear, zFar);

        out.camera.matrices.view = glm::lookAt(glm::vec3{ pos.x, pos.y, pos.z },
                                                glm::vec3{ at.x, at.y, at.z },
                                                glm::vec3{ up.x, up.y, up.z });
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
                //vertices[i].pos.y = -vertices[i].pos.y;
                vertices[i].pos.y = vertices[i].pos.y;
                vertices[i].uv = (hasUV) ? glm::make_vec2(&mesh->GetUVs()[i].x) : glm::vec2(0.0f);
                vertices[i].normal = glm::make_vec3(&mesh->GetNormals()[i].x);
                //vertices[i].normal.y = -vertices[i].normal.y;
                vertices[i].normal.y = vertices[i].normal.y;
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

            auto status = rrCreateTriangleMesh(
                m_instance,
                &meshPositions[0].x,
                (std::uint32_t)meshPositions.size(),
                sizeof(RadeonRays::float3),
                (std::uint32_t*)(&meshIndices[0]),
                (std::uint32_t)sizeof(uint32_t),
                (std::uint32_t)(meshIndices.size() / 3),
                i,
                &rrmesh);

            rrAttachShape(m_instance, rrmesh);
        }

        auto& device = m_vulkan_device->logicalDevice;
        VkQueue graphics_queue;
        vkGetDeviceQueue(device, m_vulkan_device->queueFamilyIndices.graphics, 0, &graphics_queue);

        VkCommandBuffer copy_cmd = m_vulkan_device->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, false);
        AllocateOnGPU(scene, copy_cmd, out);
        vkFreeCommandBuffers(device, m_vulkan_device->commandPool, 1, &copy_cmd);

        // Commit geometry to RR
        VkCommandBuffer rrCommitCmdBuffer;
        rrCommit(m_instance, &rrCommitCmdBuffer);

        out.scene_vertices.clear();
        out.scene_indices.clear();

        VkQueue computeQueue;
        vkGetDeviceQueue(device, m_vulkan_device->queueFamilyIndices.compute, 0, &computeQueue);

        VkSubmitInfo submitInfo = vks::initializers::submitInfo();

        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &rrCommitCmdBuffer;
        VK_CHECK_RESULT(vkQueueSubmit(computeQueue, 1, &submitInfo, VK_NULL_HANDLE));
        vkQueueWaitIdle(computeQueue);

    }

    // Update transform data only
    void VkSceneController::UpdateShapeProperties(Scene1 const& scene, Collector& mat_collector, Collector& tex_collector, VkScene& out) const
    {
    }

    // Update lights data only.
    void VkSceneController::UpdateLights(Scene1 const& scene, Collector& mat_collector, Collector& tex_collector, VkScene& out) const
    {
        out.lights.clear();
        auto it = scene.CreateLightIterator();
        for (; it->IsValid(); it->Next())
        {
            VkLight l;
            Baikal::Light::Ptr baikal_light = it->ItemAs<Baikal::Light>();
            RadeonRays::float3 col = baikal_light->GetEmittedRadiance();
            RadeonRays::float3 pos = baikal_light->GetPosition();
            RadeonRays::float3 target = baikal_light->GetPosition() + baikal_light->GetDirection();

            l.color = { col.x, col.y, col.z, 1.f};
            //l.position = { pos.x, -pos.y, pos.z, 1.f };
            //l.target = { target.x, -target.y, target.z, 1.f };
            l.position = { pos.x, pos.y, pos.z, 1.f };
            l.target = { target.x, target.y, target.z, 1.f };

            out.lights.push_back(l);
        }

    }

    // Update material data.
    void VkSceneController::UpdateMaterials(Scene1 const& scene, Collector& mat_collector, Collector& tex_collector, VkScene& out) const
    {
        auto& device = m_vulkan_device->logicalDevice;
        VkQueue queue;
        vkGetDeviceQueue(device, m_vulkan_device->queueFamilyIndices.graphics, 0, &queue);

        int num_materials = mat_collector.GetNumItems();
        out.materials.resize(num_materials);
        out.raytrace_materials.resize(num_materials);

        out.InitResources(*m_vulkan_device, queue, "../Resources/");

        int startIdx = 0;

        for (int i = startIdx; i < startIdx + num_materials; i++)
        {
            int matIdx = i - startIdx;

            out.materials[i] = {};
            auto& raytrace_material = out.raytrace_materials[i];

            raytrace_material.diffuse[0] = 1.0f;
            raytrace_material.diffuse[1] = 1.0f;
            raytrace_material.diffuse[2] = 1.0f;

            out.materials[i].pipeline = out.resources.pipelines->get("scene.solid");
            out.materials[i].diffuse = out.resources.textures->get("dummy.diffuse");
            out.materials[i].roughness = out.resources.textures->get("dummy.specular");
            out.materials[i].metallic = out.resources.textures->get("dialectric.metallic");
            out.materials[i].bump = out.resources.textures->get("dummy.bump");

            out.materials[i].hasBump = false;
            out.materials[i].hasAlpha = false;
            out.materials[i].hasMetaliness = false;
            out.materials[i].hasRoughness = false;
        }

        auto mesh_it = scene.CreateShapeIterator();
        for (int i = 0; mesh_it->IsValid(); mesh_it->Next(), ++i)
        {
            Baikal::Mesh::Ptr mesh = mesh_it->ItemAs<Baikal::Mesh>();
            Baikal::Material::Ptr baikal_mat = mesh->GetMaterial();
            int mat_indx = mat_collector.GetItemIndex(baikal_mat);
            out.meshes[i].material = &out.materials[mat_indx];

            if (MaterialInputValid("albedo", baikal_mat))
            {
                out.meshes[i].material->diffuse = TranslateMaterialInput(m_vulkan_device, "albedo", baikal_mat, out.resources);
            }

            if (MaterialInputValid("bump", baikal_mat))
            {
                out.meshes[i].material->bump = TranslateMaterialInput(m_vulkan_device, "bump", baikal_mat, out.resources);
                out.meshes[i].material->hasBump = true;
            }

            if (MaterialInputValid("roughness", baikal_mat))
            {
                out.meshes[i].material->roughness = TranslateMaterialInput(m_vulkan_device, "roughness", baikal_mat, out.resources);
                out.meshes[i].material->hasRoughness = true;
            }
        }

        //allocate materials buffer
        auto materialBufferSize = static_cast<uint32_t>(num_materials * sizeof(Raytrace::Material));

        struct  
        {
            struct 
            {
                VkDeviceMemory memory;
                VkBuffer buffer;
            } raytraceMaterialBuffer;
        }staging;

        void *data;

        VkMemoryAllocateInfo memAlloc = vks::initializers::memoryAllocateInfo();
        VkMemoryRequirements memReqs;
        VkBufferCreateInfo raytraceMaterialBufferInfo;
        // Staging buffer
        raytraceMaterialBufferInfo = vks::initializers::bufferCreateInfo(VK_BUFFER_USAGE_TRANSFER_SRC_BIT, materialBufferSize);
        VK_CHECK_RESULT(vkCreateBuffer(device, &raytraceMaterialBufferInfo, nullptr, &staging.raytraceMaterialBuffer.buffer));
        vkGetBufferMemoryRequirements(device, staging.raytraceMaterialBuffer.buffer, &memReqs);
        memAlloc.allocationSize = memReqs.size;
        memAlloc.memoryTypeIndex = GetMemTypeIndex(m_vulkan_device->physicalDevice, memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
        VK_CHECK_RESULT(vkAllocateMemory(device, &memAlloc, nullptr, &staging.raytraceMaterialBuffer.memory));
        VK_CHECK_RESULT(vkMapMemory(device, staging.raytraceMaterialBuffer.memory, 0, VK_WHOLE_SIZE, 0, &data));
        memcpy(data, out.raytrace_materials.data(), materialBufferSize);
        vkUnmapMemory(device, staging.raytraceMaterialBuffer.memory);
        VK_CHECK_RESULT(vkBindBufferMemory(device, staging.raytraceMaterialBuffer.buffer, staging.raytraceMaterialBuffer.memory, 0));

        // Target
        raytraceMaterialBufferInfo = vks::initializers::bufferCreateInfo(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, materialBufferSize);
        VK_CHECK_RESULT(vkCreateBuffer(device, &raytraceMaterialBufferInfo, nullptr, &out.raytrace_material_buffer.buffer));
        vkGetBufferMemoryRequirements(device, out.raytrace_material_buffer.buffer, &memReqs);
        memAlloc.allocationSize = memReqs.size;
        memAlloc.memoryTypeIndex = GetMemTypeIndex(m_vulkan_device->physicalDevice, memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        VK_CHECK_RESULT(vkAllocateMemory(device, &memAlloc, nullptr, &out.raytrace_material_buffer.memory));
        VK_CHECK_RESULT(vkBindBufferMemory(device, out.raytrace_material_buffer.buffer, out.raytrace_material_buffer.memory, 0));
        //out.raytrace_material_buffer.size = memReqs.size;
        out.raytrace_material_buffer.size = materialBufferSize;

        // Copy
        VkCommandBuffer copy_cmd = m_vulkan_device->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, false);
        VkCommandBufferBeginInfo cmdBufInfo = vks::initializers::commandBufferBeginInfo();
        VK_CHECK_RESULT(vkBeginCommandBuffer(copy_cmd, &cmdBufInfo));
        VkBufferCopy copyRegion = {};
        copyRegion.size = materialBufferSize;
        vkCmdCopyBuffer(
            copy_cmd,
            staging.raytraceMaterialBuffer.buffer,
            out.raytrace_material_buffer.buffer,
            1,
            &copyRegion);
        VK_CHECK_RESULT(vkEndCommandBuffer(copy_cmd));
        
        VkSubmitInfo submitInfo = {};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &copy_cmd;
        VK_CHECK_RESULT(vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE));
        VK_CHECK_RESULT(vkQueueWaitIdle(queue));

        //free
        vkFreeCommandBuffers(device, m_vulkan_device->commandPool, 1, &copy_cmd);
        vkDestroyBuffer(device, staging.raytraceMaterialBuffer.buffer, nullptr);
        vkFreeMemory(device, staging.raytraceMaterialBuffer.memory, nullptr);

        CreateDescriptorSets(out);
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
        VkQueue queue;
        vkGetDeviceQueue(device, m_vulkan_device->queueFamilyIndices.graphics, 0, &queue);

        size_t vertexDataSize = out.vertices.size() * sizeof(Vertex);
        size_t indexDataSize = out.indices.size() * sizeof(uint32_t);
        auto shapeBufferSize = static_cast<uint32_t>(out.raytrace_shapes.size() * sizeof(Raytrace::Shape));
        auto raytraceVertexDataSize = out.vertices.size() * sizeof(RaytraceVertex);

        VkMemoryAllocateInfo memAlloc = vks::initializers::memoryAllocateInfo();
        VkMemoryRequirements memReqs;

        void *data;

        struct
        {
            struct {
                VkDeviceMemory memory;
                VkBuffer buffer;
            } vBuffer;
            struct {
                VkDeviceMemory memory;
                VkBuffer buffer;
            } iBuffer;
            struct {
                VkDeviceMemory memory;
                VkBuffer buffer;
            } raytraceShapeBuffer;
            struct {
                VkDeviceMemory memory;
                VkBuffer buffer;
            } raytraceRNGBuffer;
            struct {
                VkDeviceMemory memory;
                VkBuffer buffer;
            } raytraceVertexBuffer;
        } staging;

        // Generate vertex buffer
        VkBufferCreateInfo vBufferInfo;

        // Staging buffer
        vBufferInfo = vks::initializers::bufferCreateInfo(VK_BUFFER_USAGE_TRANSFER_SRC_BIT, vertexDataSize);
        VK_CHECK_RESULT(vkCreateBuffer(device, &vBufferInfo, nullptr, &staging.vBuffer.buffer));
        vkGetBufferMemoryRequirements(device, staging.vBuffer.buffer, &memReqs);
        memAlloc.allocationSize = memReqs.size;
        memAlloc.memoryTypeIndex = GetMemTypeIndex(m_vulkan_device->physicalDevice, memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
        VK_CHECK_RESULT(vkAllocateMemory(device, &memAlloc, nullptr, &staging.vBuffer.memory));
        VK_CHECK_RESULT(vkMapMemory(device, staging.vBuffer.memory, 0, VK_WHOLE_SIZE, 0, &data));
        memcpy(data, out.vertices.data(), vertexDataSize);
        vkUnmapMemory(device, staging.vBuffer.memory);
        VK_CHECK_RESULT(vkBindBufferMemory(device, staging.vBuffer.buffer, staging.vBuffer.memory, 0));

        // Target
        vBufferInfo = vks::initializers::bufferCreateInfo(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, vertexDataSize);
        VK_CHECK_RESULT(vkCreateBuffer(device, &vBufferInfo, nullptr, &out.vertex_buffer.buffer));
        vkGetBufferMemoryRequirements(device, out.vertex_buffer.buffer, &memReqs);
        memAlloc.allocationSize = memReqs.size;
        memAlloc.memoryTypeIndex = GetMemTypeIndex(m_vulkan_device->physicalDevice, memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        VK_CHECK_RESULT(vkAllocateMemory(device, &memAlloc, nullptr, &out.vertex_buffer.memory));
        VK_CHECK_RESULT(vkBindBufferMemory(device, out.vertex_buffer.buffer, out.vertex_buffer.memory, 0));
        out.vertex_buffer.size = memReqs.size;

        // Generate index buffer
        VkBufferCreateInfo iBufferInfo;

        // Staging buffer
        iBufferInfo = vks::initializers::bufferCreateInfo(VK_BUFFER_USAGE_TRANSFER_SRC_BIT, indexDataSize);
        VK_CHECK_RESULT(vkCreateBuffer(device, &iBufferInfo, nullptr, &staging.iBuffer.buffer));
        vkGetBufferMemoryRequirements(device, staging.iBuffer.buffer, &memReqs);
        memAlloc.allocationSize = memReqs.size;
        memAlloc.memoryTypeIndex = GetMemTypeIndex(m_vulkan_device->physicalDevice, memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
        VK_CHECK_RESULT(vkAllocateMemory(device, &memAlloc, nullptr, &staging.iBuffer.memory));
        VK_CHECK_RESULT(vkMapMemory(device, staging.iBuffer.memory, 0, VK_WHOLE_SIZE, 0, &data));
        memcpy(data, out.indices.data(), indexDataSize);
        vkUnmapMemory(device, staging.iBuffer.memory);
        VK_CHECK_RESULT(vkBindBufferMemory(device, staging.iBuffer.buffer, staging.iBuffer.memory, 0));

        // Target
        iBufferInfo = vks::initializers::bufferCreateInfo(VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, indexDataSize);
        VK_CHECK_RESULT(vkCreateBuffer(device, &iBufferInfo, nullptr, &out.index_buffer.buffer));
        vkGetBufferMemoryRequirements(device, out.index_buffer.buffer, &memReqs);
        memAlloc.allocationSize = memReqs.size;
        memAlloc.memoryTypeIndex = GetMemTypeIndex(m_vulkan_device->physicalDevice, memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        VK_CHECK_RESULT(vkAllocateMemory(device, &memAlloc, nullptr, &out.index_buffer.memory));
        VK_CHECK_RESULT(vkBindBufferMemory(device, out.index_buffer.buffer, out.index_buffer.memory, 0));
        //out.index_buffer.size = memReqs.size;
        out.index_buffer.size = indexDataSize;

        // Generate raytrace shape buffer
        VkBufferCreateInfo raytraceShapeBufferInfo;

        // Staging buffer
        raytraceShapeBufferInfo = vks::initializers::bufferCreateInfo(VK_BUFFER_USAGE_TRANSFER_SRC_BIT, shapeBufferSize);
        VK_CHECK_RESULT(vkCreateBuffer(device, &raytraceShapeBufferInfo, nullptr, &staging.raytraceShapeBuffer.buffer));
        vkGetBufferMemoryRequirements(device, staging.raytraceShapeBuffer.buffer, &memReqs);
        memAlloc.allocationSize = memReqs.size;
        memAlloc.memoryTypeIndex = GetMemTypeIndex(m_vulkan_device->physicalDevice, memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
        VK_CHECK_RESULT(vkAllocateMemory(device, &memAlloc, nullptr, &staging.raytraceShapeBuffer.memory));
        VK_CHECK_RESULT(vkMapMemory(device, staging.raytraceShapeBuffer.memory, 0, VK_WHOLE_SIZE, 0, &data));
        memcpy(data, out.raytrace_shapes.data(), shapeBufferSize);
        vkUnmapMemory(device, staging.raytraceShapeBuffer.memory);
        VK_CHECK_RESULT(vkBindBufferMemory(device, staging.raytraceShapeBuffer.buffer, staging.raytraceShapeBuffer.memory, 0));

        // Target
        raytraceShapeBufferInfo = vks::initializers::bufferCreateInfo(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, shapeBufferSize);
        VK_CHECK_RESULT(vkCreateBuffer(device, &raytraceShapeBufferInfo, nullptr, &out.raytrace_shape_buffer.buffer));
        vkGetBufferMemoryRequirements(device, out.raytrace_shape_buffer.buffer, &memReqs);
        memAlloc.allocationSize = memReqs.size;
        memAlloc.memoryTypeIndex = GetMemTypeIndex(m_vulkan_device->physicalDevice, memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        VK_CHECK_RESULT(vkAllocateMemory(device, &memAlloc, nullptr, &out.raytrace_shape_buffer.memory));
        VK_CHECK_RESULT(vkBindBufferMemory(device, out.raytrace_shape_buffer.buffer, out.raytrace_shape_buffer.memory, 0));
        //out.raytrace_shape_buffer.size = memReqs.size;
        out.raytrace_shape_buffer.size = shapeBufferSize;

        // Generate raytrace shape buffer
        VkBufferCreateInfo raytraceRNGBufferInfo;

        // Staging buffer
        raytraceRNGBufferInfo = vks::initializers::bufferCreateInfo(VK_BUFFER_USAGE_TRANSFER_SRC_BIT, kRNGBufferSize);
        VK_CHECK_RESULT(vkCreateBuffer(device, &raytraceRNGBufferInfo, nullptr, &staging.raytraceRNGBuffer.buffer));
        vkGetBufferMemoryRequirements(device, staging.raytraceRNGBuffer.buffer, &memReqs);
        memAlloc.allocationSize = memReqs.size;
        memAlloc.memoryTypeIndex = GetMemTypeIndex(m_vulkan_device->physicalDevice, memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
        VK_CHECK_RESULT(vkAllocateMemory(device, &memAlloc, nullptr, &staging.raytraceRNGBuffer.memory));
        VK_CHECK_RESULT(vkMapMemory(device, staging.raytraceRNGBuffer.memory, 0, VK_WHOLE_SIZE, 0, &data));
        out.raytrace_RNG_buffer.size = memReqs.size;

        // Generate random data into the buffer
        {
            auto iter = reinterpret_cast<uint32_t*>(data);
            std::random_device random_device;
            std::mt19937 rng(random_device());
            std::uniform_int_distribution<> distribution(1u, 0x7fffffffu);
            std::generate(iter, iter + kRNGBufferSize / sizeof(uint32_t), [&distribution, &rng] { return distribution(rng); });
        }

        vkUnmapMemory(device, staging.raytraceRNGBuffer.memory);
        VK_CHECK_RESULT(vkBindBufferMemory(device, staging.raytraceRNGBuffer.buffer, staging.raytraceRNGBuffer.memory, 0));

        // Target
        raytraceRNGBufferInfo = vks::initializers::bufferCreateInfo(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, kRNGBufferSize);
        VK_CHECK_RESULT(vkCreateBuffer(device, &raytraceRNGBufferInfo, nullptr, &out.raytrace_RNG_buffer.buffer));
        vkGetBufferMemoryRequirements(device, out.raytrace_RNG_buffer.buffer, &memReqs);
        memAlloc.allocationSize = memReqs.size;
        memAlloc.memoryTypeIndex = GetMemTypeIndex(m_vulkan_device->physicalDevice, memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        VK_CHECK_RESULT(vkAllocateMemory(device, &memAlloc, nullptr, &out.raytrace_RNG_buffer.memory));
        VK_CHECK_RESULT(vkBindBufferMemory(device, out.raytrace_RNG_buffer.buffer, out.raytrace_RNG_buffer.memory, 0));

        // Staging buffer
        auto raytraceVertexBufferInfo = vks::initializers::bufferCreateInfo(VK_BUFFER_USAGE_TRANSFER_SRC_BIT, raytraceVertexDataSize);
        VK_CHECK_RESULT(vkCreateBuffer(device, &raytraceVertexBufferInfo, nullptr, &staging.raytraceVertexBuffer.buffer));
        vkGetBufferMemoryRequirements(device, staging.raytraceVertexBuffer.buffer, &memReqs);
        memAlloc.allocationSize = memReqs.size;
        memAlloc.memoryTypeIndex = GetMemTypeIndex(m_vulkan_device->physicalDevice, memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
        VK_CHECK_RESULT(vkAllocateMemory(device, &memAlloc, nullptr, &staging.raytraceVertexBuffer.memory));
        VK_CHECK_RESULT(vkMapMemory(device, staging.raytraceVertexBuffer.memory, 0, VK_WHOLE_SIZE, 0, &data));
        memcpy(data, out.raytrace_vertices.data(), raytraceVertexDataSize);
        vkUnmapMemory(device, staging.raytraceVertexBuffer.memory);
        VK_CHECK_RESULT(vkBindBufferMemory(device, staging.raytraceVertexBuffer.buffer, staging.raytraceVertexBuffer.memory, 0));

        // Target
        raytraceVertexBufferInfo = vks::initializers::bufferCreateInfo(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, raytraceVertexDataSize);
        VK_CHECK_RESULT(vkCreateBuffer(device, &raytraceVertexBufferInfo, nullptr, &out.raytrace_vertex_buffer.buffer));
        vkGetBufferMemoryRequirements(device, out.raytrace_vertex_buffer.buffer, &memReqs);
        memAlloc.allocationSize = memReqs.size;
        memAlloc.memoryTypeIndex = GetMemTypeIndex(m_vulkan_device->physicalDevice, memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        VK_CHECK_RESULT(vkAllocateMemory(device, &memAlloc, nullptr, &out.raytrace_vertex_buffer.memory));
        VK_CHECK_RESULT(vkBindBufferMemory(device, out.raytrace_vertex_buffer.buffer, out.raytrace_vertex_buffer.memory, 0));
        //out.raytrace_vertex_buffer.size = memReqs.size;
        out.raytrace_vertex_buffer.size = raytraceVertexDataSize;

        // Copy
        VkCommandBufferBeginInfo cmdBufInfo = vks::initializers::commandBufferBeginInfo();
        VK_CHECK_RESULT(vkBeginCommandBuffer(copy_cmd, &cmdBufInfo));

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
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &copy_cmd;

        VK_CHECK_RESULT(vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE));
        VK_CHECK_RESULT(vkQueueWaitIdle(queue));

        vkDestroyBuffer(device, staging.vBuffer.buffer, nullptr);
        vkFreeMemory(device, staging.vBuffer.memory, nullptr);
        vkDestroyBuffer(device, staging.iBuffer.buffer, nullptr);
        vkFreeMemory(device, staging.iBuffer.memory, nullptr);
        vkDestroyBuffer(device, staging.raytraceShapeBuffer.buffer, nullptr);
        vkFreeMemory(device, staging.raytraceShapeBuffer.memory, nullptr);
        vkDestroyBuffer(device, staging.raytraceRNGBuffer.buffer, nullptr);
        vkFreeMemory(device, staging.raytraceRNGBuffer.memory, nullptr);
    }

    void VkSceneController::CreateDescriptorSets(VkScene& out) const
    {
        auto& device = m_vulkan_device->logicalDevice;
        // Generate descriptor sets for all meshes
        // todo : think about a nicer solution, better suited per material?

        // Decriptor pool
        std::vector<VkDescriptorPoolSize> poolSizes;
        poolSizes.push_back(vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, static_cast<uint32_t>(out.meshes.size())));
        poolSizes.push_back(vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, static_cast<uint32_t>(out.meshes.size() * 4)));
        poolSizes.push_back(vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, static_cast<uint32_t>(10)));

        VkDescriptorPoolCreateInfo descriptorPoolInfo =
            vks::initializers::descriptorPoolCreateInfo(
                static_cast<uint32_t>(poolSizes.size()),
                poolSizes.data(),
                static_cast<uint32_t>(out.meshes.size()));

        VK_CHECK_RESULT(vkCreateDescriptorPool(device, &descriptorPoolInfo, nullptr, &out.descriptorPool));

        // Shared descriptor set layout
        std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings;
        // Binding 0: UBO
        setLayoutBindings.push_back(vks::initializers::descriptorSetLayoutBinding(
            VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            VK_SHADER_STAGE_VERTEX_BIT,
            0));
        // Binding 1: Diffuse map
        setLayoutBindings.push_back(vks::initializers::descriptorSetLayoutBinding(
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            VK_SHADER_STAGE_FRAGMENT_BIT,
            1));
        // Binding 2: Roughness map
        setLayoutBindings.push_back(vks::initializers::descriptorSetLayoutBinding(
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            VK_SHADER_STAGE_FRAGMENT_BIT,
            2));
        // Binding 3: Bump map
        setLayoutBindings.push_back(vks::initializers::descriptorSetLayoutBinding(
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            VK_SHADER_STAGE_FRAGMENT_BIT,
            3));
        // Binding 4: Metallic map
        setLayoutBindings.push_back(vks::initializers::descriptorSetLayoutBinding(
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            VK_SHADER_STAGE_FRAGMENT_BIT,
            4));

        VkDescriptorSetLayoutCreateInfo descriptorLayout =
            vks::initializers::descriptorSetLayoutCreateInfo(
                setLayoutBindings.data(),
                static_cast<uint32_t>(setLayoutBindings.size()));

        VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorLayout, nullptr, &out.descriptorSetLayout));

        VkPipelineLayoutCreateInfo pPipelineLayoutCreateInfo =
            vks::initializers::pipelineLayoutCreateInfo(
                &out.descriptorSetLayout,
                1);

        VkPushConstantRange pushConstantRange = vks::initializers::pushConstantRange(VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(PushConsts), 0);
        pPipelineLayoutCreateInfo.pushConstantRangeCount = 1;
        pPipelineLayoutCreateInfo.pPushConstantRanges = &pushConstantRange;

        VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pPipelineLayoutCreateInfo, nullptr, &out.pipelineLayout));

        // Descriptor sets
        for (uint32_t i = 0; i < out.meshes.size(); i++)
        {
            // Descriptor set
            VkDescriptorSetAllocateInfo allocInfo =
                vks::initializers::descriptorSetAllocateInfo(
                    out.descriptorPool,
                    &out.descriptorSetLayout,
                    1);

            // Background

            VkResult r = vkAllocateDescriptorSets(device, &allocInfo, &out.meshes[i].descriptorSet);

            VK_CHECK_RESULT(r);

            std::vector<VkWriteDescriptorSet> writeDescriptorSets;

            // Binding 0 : Vertex shader uniform buffer
            writeDescriptorSets.push_back(vks::initializers::writeDescriptorSet(
                out.meshes[i].descriptorSet,
                VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                0,
                &m_defaultUBO->descriptor));
            // Image bindings
            // Binding 0: Color map
            writeDescriptorSets.push_back(vks::initializers::writeDescriptorSet(
                out.meshes[i].descriptorSet,
                VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                1,
                &out.meshes[i].material->diffuse.descriptor));
            // Binding 1: Roughness
            writeDescriptorSets.push_back(vks::initializers::writeDescriptorSet(
                out.meshes[i].descriptorSet,
                VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                2,
                &out.meshes[i].material->roughness.descriptor));
            // Binding 2: Normal
            writeDescriptorSets.push_back(vks::initializers::writeDescriptorSet(
                out.meshes[i].descriptorSet,
                VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                3,
                &out.meshes[i].material->bump.descriptor));
            // Binding 3: Metallic
            writeDescriptorSets.push_back(vks::initializers::writeDescriptorSet(
                out.meshes[i].descriptorSet,
                VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                4,
                &out.meshes[i].material->metallic.descriptor));

            vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, NULL);
        }
    }
}
