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

#include "rteffects.h"
#include "utils/static_hash.h"

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

    void GetBaikalTextureData(Texture::Ptr tex, std::string* out_name, VkFormat* out_format, int* w, int* h, void const** out_data, VkDeviceSize* out_data_size)
    {
        *out_name = tex->GetName() + "_" + std::to_string((std::uintptr_t)tex.get());
        *w = tex->GetSize().x;
        *h = tex->GetSize().y;
        *out_data = tex->GetData();
        *out_data_size = (VkDeviceSize)tex->GetSizeInBytes();
        switch (tex->GetFormat())
        {
        case Texture::Format::kRgba16:
            *out_format = VK_FORMAT_R16G16B16A16_UNORM;
            break;
        case Texture::Format::kRgba32:
            *out_format = VK_FORMAT_R32G32B32A32_UINT;
            break;
        case Texture::Format::kRgba8:
            *out_format = VK_FORMAT_B8G8R8A8_UNORM;
            break;
        default:
            throw std::runtime_error("Error: unexpected Baikal::Texture format.");
        }
    }

    vks::Texture TranslateMaterialInput(vks::VulkanDevice* device, const std::string& input_name, Baikal::Material::Ptr mat, ResourceManager* res)
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
                std::string name;
                VkFormat format;
                int w, h;
                void const* data;
                VkDeviceSize data_size;
                GetBaikalTextureData(tex, &name, &format, &w, &h, &data, &data_size);
                TextureList& tex_list = res->GetTextures();
                auto name_hash = STATIC_CRC32(name.c_str());
                if (!tex_list.Present(name_hash))
                {
                    tex_list.addTexture2D(name_hash, data, data_size, format, w, h, device, queue);
                }
                result = tex_list.Get(name_hash);
                break;
            }
            case Baikal::Material::InputType::kFloat4:
            {
                RadeonRays::float4 color = input_value.float_value;// color /= 255.f;
                color = { color.z, color.y, color.x, 1.f };
                std::string name = mat->GetName() + "_" + input_name + "_" + "kFloat4"  + std::to_string(color.x)
                                                                                        + std::to_string(color.y)
                                                                                        + std::to_string(color.z);
                TextureList& tex_list = res->GetTextures();
                auto name_hash = STATIC_CRC32(name.c_str());
                if (!tex_list.Present(name_hash))
                {
                    int w = 1;
                    int h = 1;
                    VkFormat format = VK_FORMAT_R32G32B32A32_SFLOAT;
                    tex_list.addTexture2D(name_hash, &color.x, sizeof(color), format, w, h, device, queue);
                }
                result = tex_list.Get(name_hash);
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
            TextureList& tex_list = res->GetTextures();
            auto name_hash = STATIC_CRC32(name.c_str());
            if (!tex_list.Present(name_hash))
            {
                RadeonRays::float4 color = { 0.f, 0.f, 1.f, 1.f };
                int w = 1;
                int h = 1;
                VkFormat format = VK_FORMAT_R32G32B32A32_SFLOAT;
                tex_list.addTexture2D(name_hash, &color.x, sizeof(color), format, w, h, device, queue);
            }
            result = tex_list.Get(name_hash);
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
        : m_instance(&instance)
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
        out.dirty_flags |= VkScene::DirtyFlags::SHAPES;
        auto& device = m_vulkan_device->logicalDevice;

        //cleanup RR data
        if (!m_rr_meshes.empty())
        {
            //rrDetachAllShapes(m_instance);
            //for (auto m : m_rr_meshes)
            //{
            //    rrDeleteShape(m_instance, m);
            //}
            m_rr_meshes.clear();

            //TODO: remove this.
            //rr-next don't support geometry change after commit,
            //so recreating instance instead
            rrShutdownInstance(*m_instance);
            rrInitInstance(m_vulkan_device->logicalDevice, m_vulkan_device->physicalDevice, m_vulkan_device->computeCommandPool, m_instance);
        }

        int mesh_num = scene.GetNumShapes();
        out.scene_vertices.resize(mesh_num);
        out.scene_indices.resize(mesh_num);

        out.meshes.resize(mesh_num);
        out.raytrace_shapes.resize(mesh_num);

        VkQueue graphics_queue;
        vkGetDeviceQueue(device, m_vulkan_device->queueFamilyIndices.graphics, 0, &graphics_queue);

        //prepare ubo with transform matrices
        {
            out.mesh_transform_buf.destroy();

            size_t min_alignment = m_vulkan_device->properties.limits.minUniformBufferOffsetAlignment;
            out.transform_alignment = sizeof(glm::mat4);
            if (min_alignment > 0) {
                out.transform_alignment = (out.transform_alignment + min_alignment - 1) & ~(min_alignment - 1);
            }

            size_t bufferSize = out.meshes.size() * out.transform_alignment;

            // Uniform buffer object with per-object matrices
            VK_CHECK_RESULT(m_vulkan_device->createBuffer(
                VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                &out.mesh_transform_buf,
                bufferSize));

            // Map persistent
            VK_CHECK_RESULT(out.mesh_transform_buf.map());
        }

        int startIdx = 0;
        auto mesh_iter = scene.CreateShapeIterator();
        for (int i = 0; mesh_iter->IsValid(); mesh_iter->Next(), ++i)
        {
            Baikal::Shape::Ptr shape = mesh_iter->ItemAs<Baikal::Shape>();

            Baikal::Mesh const* mesh = dynamic_cast<Baikal::Mesh*>(shape.get());

            //handle instance case
            Baikal::Instance const* inst = dynamic_cast<Baikal::Instance*>(shape.get());
            if (inst)
            {
                mesh = dynamic_cast<Baikal::Mesh*>(inst->GetBaseShape().get());
                continue;
            }
            const RadeonRays::matrix transform = mesh->GetTransform().transpose();

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

            out.meshes[i].index_base = out.index_base;

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
            out.meshes[i].index_count = mesh->GetNumIndices();
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
                *m_instance,
                &meshPositions[0].x,
                (std::uint32_t)meshPositions.size(),
                sizeof(RadeonRays::float3),
                (std::uint32_t*)(&meshIndices[0]),
                (std::uint32_t)sizeof(uint32_t),
                (std::uint32_t)(meshIndices.size() / 3),
                i,
                &rrmesh);

            //setup transform
            glm::mat4* modelMat = (glm::mat4*)(((uint64_t)out.mesh_transform_buf.mapped + (i * out.transform_alignment)));
            memcpy(modelMat, &transform.m00, sizeof(transform));
            rrShapeSetTransform(*m_instance, rrmesh, &transform.m00);

            rrAttachShape(*m_instance, rrmesh);
            m_rr_meshes.push_back(rrmesh);
        }

        // Flush to make changes visible to the host 
        VkMappedMemoryRange memoryRange = vks::initializers::mappedMemoryRange();
        memoryRange.memory = out.mesh_transform_buf.memory;
        memoryRange.size = out.mesh_transform_buf.size;
        vkFlushMappedMemoryRanges(device, 1, &memoryRange);

        VkCommandBuffer copy_cmd = m_vulkan_device->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, false);
        AllocateOnGPU(scene, copy_cmd, out);
        vkFreeCommandBuffers(device, m_vulkan_device->commandPool, 1, &copy_cmd);

        {
            // Commit geometry to RR
            VkCommandBuffer rrCommitCmdBuffer;
            rrCommit(*m_instance, &rrCommitCmdBuffer);

            VkQueue computeQueue;
            vkGetDeviceQueue(device, m_vulkan_device->queueFamilyIndices.compute, 0, &computeQueue);

            VkSubmitInfo submitInfo = vks::initializers::submitInfo();

            submitInfo.commandBufferCount = 1;
            submitInfo.pCommandBuffers = &rrCommitCmdBuffer;
            VK_CHECK_RESULT(vkQueueSubmit(computeQueue, 1, &submitInfo, VK_NULL_HANDLE));
            vkQueueWaitIdle(computeQueue);

        }
        out.scene_vertices.clear();
        out.scene_indices.clear();

    }

    // Update transform data only
    void VkSceneController::UpdateShapeProperties(Scene1 const& scene, Collector& mat_collector, Collector& tex_collector, VkScene& out) const
    {
        out.dirty_flags |= VkScene::DirtyFlags::SHAPE_PROPERTIES;

    }

    // Update lights data only.
    void VkSceneController::UpdateLights(Scene1 const& scene, Collector& mat_collector, Collector& tex_collector, VkScene& out) const
    {
        out.dirty_flags |= VkScene::DirtyFlags::LIGHTS;

        out.lights.clear();
        auto it = scene.CreateLightIterator();
        for (; it->IsValid(); it->Next())
        {
            Baikal::Light::Ptr baikal_light = it->ItemAs<Baikal::Light>();

            VkLight l;
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
        out.dirty_flags |= VkScene::DirtyFlags::MATERIALS;

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

            TextureList& tex_list = out.resources->GetTextures();
            //out.materials[i].pipeline = out.resources.pipelines->get("scene.solid");
            out.materials[i].diffuse = tex_list.Get(STATIC_CRC32("dummy.diffuse"));
            out.materials[i].roughness = tex_list.Get(STATIC_CRC32("dummy.specular"));
            out.materials[i].metallic = tex_list.Get(STATIC_CRC32("dialectric.metallic"));
            out.materials[i].bump = tex_list.Get(STATIC_CRC32("dummy.bump"));

            out.materials[i].has_bump = false;
            out.materials[i].has_alpha = false;
            out.materials[i].has_metaliness = false;
            out.materials[i].has_roughness = false;
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
                out.meshes[i].material->has_bump = true;
            }

            if (MaterialInputValid("roughness", baikal_mat))
            {
                out.meshes[i].material->roughness = TranslateMaterialInput(m_vulkan_device, "roughness", baikal_mat, out.resources);
                out.meshes[i].material->has_roughness = true;
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
        VkQueue queue;
        vkGetDeviceQueue(m_vulkan_device->logicalDevice, m_vulkan_device->queueFamilyIndices.graphics, 0, &queue);

        auto it = tex_collector.CreateIterator();
        for (; it->IsValid(); it->Next())
        {
            Baikal::Texture::Ptr tex = it->ItemAs<Baikal::Texture>();
            if (tex->IsDirty())
            {
                out.dirty_flags |= VkScene::DirtyFlags::TEXTURES;

                std::string name;
                VkFormat format;
                int w, h;
                void const* data;
                VkDeviceSize data_size;
                GetBaikalTextureData(tex, &name, &format, &w, &h, &data, &data_size);

                TextureList& tex_list = out.resources->GetTextures();
                //add texture if its missing.
                auto name_hash = STATIC_CRC32(name.c_str());
                if (!tex_list.Present(name_hash))
                {
                    tex_list.addTexture2D(name_hash, data, data_size, format, w, h, m_vulkan_device, queue);
                }
                //get vk texture and change it data
                else
                {
                    vks::Texture vk_tex = tex_list.Get(name_hash);
                    vks::Texture2D* vk_tex2d = static_cast<vks::Texture2D*>(&vk_tex);
                    vk_tex2d->fromBuffer(data, data_size, format, w, h, m_vulkan_device, queue);
                }

                tex->SetDirty(false);
            }
        }

        if (out.dirty_flags & VkScene::DirtyFlags::TEXTURES)
        {
            CreateDescriptorSets(out);
        }
    }

    // Get default material
    Material::Ptr VkSceneController::GetDefaultMaterial() const
    {
        return nullptr;
    }

    // If m_current_scene changes
    void VkSceneController::UpdateCurrentScene(Scene1 const& scene, VkScene& out) const
    {
        out.dirty_flags |= VkScene::DirtyFlags::CURRENT_SCENE;

    }

    // Update volume materials
    void VkSceneController::UpdateVolumes(Scene1 const& scene, Collector& volume_collector, VkScene& out) const
    {
        out.dirty_flags |= VkScene::DirtyFlags::VOLUMES;

    }

    // If scene attributes changed
    void VkSceneController::UpdateSceneAttributes(Scene1 const& scene, Collector& tex_collector, VkScene& out) const
    {
        out.dirty_flags |= VkScene::DirtyFlags::SCENE_ATTRIBUTES;

    }


    void VkSceneController::AllocateOnGPU(Scene1 const& scene, VkCommandBuffer copy_cmd, VkScene& out) const
    {
        DeallocateOnGPU(out);

        auto& device = m_vulkan_device->logicalDevice;
        VkQueue queue;
        vkGetDeviceQueue(device, m_vulkan_device->queueFamilyIndices.graphics, 0, &queue);

        size_t vertexDataSize = out.vertices.size() * sizeof(Vertex);
        size_t indexDataSize = out.indices.size() * sizeof(uint32_t);
        auto shapeBufferSize = static_cast<uint32_t>(out.raytrace_shapes.size() * sizeof(Raytrace::Shape));
        auto materialBufferSize = static_cast<uint32_t>(out.raytrace_materials.size() * sizeof(Raytrace::Material));

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
            } raytraceMaterialBuffer;
            struct {
                VkDeviceMemory memory;
                VkBuffer buffer;
            } raytraceRNGBuffer;
            struct {
                VkDeviceMemory memory;
                VkBuffer buffer;
            } raytraceLightsBuffer;
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
        out.index_buffer.size = memReqs.size;

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
        out.raytrace_shape_buffer.size = memReqs.size;

        // Generate raytrace shape buffer
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
        out.raytrace_material_buffer.size = memReqs.size;


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

        // Raytrace light
        VkBufferCreateInfo raytraceLightsBufferInfo;

        float height = 35.0f;
        auto dir1 = glm::normalize(-glm::vec3(23.8847504f, 16.0555954f, 5.01268339f) + glm::vec3(23.0653629f, 15.9814873f, 4.44425392f));
        auto dir2 = glm::normalize(-glm::vec3(0.0f, height, 0.0f) + glm::vec3(-1.0f, height, 0.0f));
        auto dir3 = glm::normalize(-glm::vec3(24.2759151f, 17.7952175f, -4.77304792f) + glm::vec3(23.3718319f, 17.5660172f, -4.41235495f));

        rte_light lights[4];
        lights[0].position[0] = 23.8847504f;
        lights[0].position[1] = 16.0555954f;
        lights[0].position[2] = 5.01268339f;
        lights[0].type = RTE_LIGHT_SPOT;
        lights[0].intensity[0] = 150000.0f;
        lights[0].intensity[1] = 150000.0f;
        lights[0].intensity[2] = 150000.0f;
        lights[0].intensity[3] = 1.0f;
        lights[0].direction[0] = dir1.x;
        lights[0].direction[1] = dir1.y;
        lights[0].direction[2] = dir1.z;
        lights[0].spotAngles[0] = 0.9f;
        lights[0].spotAngles[1] = 0.96f;

        lights[1].position[0] = 0.0f;
        lights[1].position[1] = height;
        lights[1].position[2] = 0.0f;
        lights[1].type = RTE_LIGHT_SPOT;
        lights[1].intensity[0] = 13000.0f;
        lights[1].intensity[1] = 13000.0f;
        lights[1].intensity[2] = 13000.0f;
        lights[1].intensity[3] = 1.0f;
        lights[1].direction[0] = dir2.x;
        lights[1].direction[1] = dir2.y;
        lights[1].direction[2] = dir2.z;
        lights[1].spotAngles[0] = 0.9f;
        lights[1].spotAngles[1] = 0.96f;

        lights[2].position[0] = 24.2759151f;
        lights[2].position[1] = 17.7952175f;
        lights[2].position[2] = -4.77304792f;
        lights[2].type = RTE_LIGHT_SPOT;
        lights[2].intensity[0] = 15000.0f;
        lights[2].intensity[1] = 15000.0f;
        lights[2].intensity[2] = 15000.0f;
        lights[2].intensity[3] = 1.0f;
        lights[2].direction[0] = dir3.x;
        lights[2].direction[1] = dir3.y;
        lights[2].direction[2] = dir3.z;
        lights[2].spotAngles[0] = 0.9f;
        lights[2].spotAngles[1] = 0.96f;

        lights[3].position[0] = 24.2759151f;
        lights[3].position[1] = -17.7952175f;
        lights[3].position[2] = -4.77304792f;
        lights[3].type = RTE_LIGHT_AREA;
        lights[3].intensity[0] = 100.0f;
        lights[3].intensity[1] = 80.0f;
        lights[3].intensity[2] = 70.0f;
        lights[3].intensity[3] = 1.0f;
        lights[3].direction[0] = dir3.x;
        lights[3].direction[1] = dir3.y;
        lights[3].direction[2] = dir3.z;
        lights[3].spotAngles[0] = 0.3f;
        lights[3].spotAngles[1] = 0.6f;
        lights[3].areaLightShapeIndex = 0;

        auto numLights = sizeof(lights) / sizeof(rte_light);

        // Staging buffer
        raytraceLightsBufferInfo = vks::initializers::bufferCreateInfo(VK_BUFFER_USAGE_TRANSFER_SRC_BIT, numLights * sizeof(rte_light));
        VK_CHECK_RESULT(vkCreateBuffer(device, &raytraceLightsBufferInfo, nullptr, &staging.raytraceLightsBuffer.buffer));
        vkGetBufferMemoryRequirements(device, staging.raytraceLightsBuffer.buffer, &memReqs);
        memAlloc.allocationSize = memReqs.size;
        memAlloc.memoryTypeIndex = GetMemTypeIndex(m_vulkan_device->physicalDevice, memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
        VK_CHECK_RESULT(vkAllocateMemory(device, &memAlloc, nullptr, &staging.raytraceLightsBuffer.memory));
        VK_CHECK_RESULT(vkMapMemory(device, staging.raytraceLightsBuffer.memory, 0, VK_WHOLE_SIZE, 0, &data));
        memcpy(data, lights, numLights * sizeof(rte_light));
        vkUnmapMemory(device, staging.raytraceLightsBuffer.memory);
        VK_CHECK_RESULT(vkBindBufferMemory(device, staging.raytraceLightsBuffer.buffer, staging.raytraceLightsBuffer.memory, 0));

        // Target
        raytraceLightsBufferInfo = vks::initializers::bufferCreateInfo(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, numLights * sizeof(rte_light));
        VK_CHECK_RESULT(vkCreateBuffer(device, &raytraceLightsBufferInfo, nullptr, &out.raytrace_lights_buffer.buffer));
        vkGetBufferMemoryRequirements(device, out.raytrace_lights_buffer.buffer, &memReqs);
        memAlloc.allocationSize = memReqs.size;
        memAlloc.memoryTypeIndex = GetMemTypeIndex(m_vulkan_device->physicalDevice, memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        VK_CHECK_RESULT(vkAllocateMemory(device, &memAlloc, nullptr, &out.raytrace_lights_buffer.memory));
        VK_CHECK_RESULT(vkBindBufferMemory(device, out.raytrace_lights_buffer.buffer, out.raytrace_lights_buffer.memory, 0));
        out.raytrace_lights_buffer.size = numLights * sizeof(rte_light);


        // Copyx
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

        copyRegion.size = numLights * sizeof(rte_light);
        vkCmdCopyBuffer(
            copy_cmd,
            staging.raytraceLightsBuffer.buffer,
            out.raytrace_lights_buffer.buffer,
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
        vkDestroyBuffer(device, staging.raytraceMaterialBuffer.buffer, nullptr);
        vkFreeMemory(device, staging.raytraceMaterialBuffer.memory, nullptr);
        vkDestroyBuffer(device, staging.raytraceRNGBuffer.buffer, nullptr);
        vkFreeMemory(device, staging.raytraceRNGBuffer.memory, nullptr);

        // Decriptor pool
        std::vector<VkDescriptorPoolSize> poolSizes;
        poolSizes.push_back(vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, static_cast<uint32_t>(out.meshes.size() * 15)));
        poolSizes.push_back(vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, static_cast<uint32_t>(out.meshes.size() * 15)));
        poolSizes.push_back(vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, static_cast<uint32_t>(10)));

        VkDescriptorPoolCreateInfo descriptorPoolInfo =
            vks::initializers::descriptorPoolCreateInfo(
                static_cast<uint32_t>(poolSizes.size()),
                poolSizes.data(),
                static_cast<uint32_t>(out.meshes.size() * 8));

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

        // Create resources for forward pass (cubemap)

        // Binding 5-7: Shadows
        setLayoutBindings.push_back(vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 5));
        setLayoutBindings.push_back(vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 6));
        setLayoutBindings.push_back(vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 7));
        // Binding 8: Lights
        setLayoutBindings.push_back(vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT, 8));
        // Binding 9: EnvMap
        setLayoutBindings.push_back(vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 9));

        VkDescriptorSetLayoutCreateInfo cubemapDescriptorLayout = vks::initializers::descriptorSetLayoutCreateInfo(setLayoutBindings.data(), static_cast<uint32_t>(setLayoutBindings.size()));

        VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &cubemapDescriptorLayout, nullptr, &out.cubemapDescriptorSetLayout));
        pPipelineLayoutCreateInfo = vks::initializers::pipelineLayoutCreateInfo(&out.cubemapDescriptorSetLayout, 1);

        VkPushConstantRange pushConstantRanges[2] = {
            vks::initializers::pushConstantRange(VK_SHADER_STAGE_VERTEX_BIT, 4 * sizeof(int), 0),
            vks::initializers::pushConstantRange(VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(PushConsts), 4 * sizeof(int))
        };

        pPipelineLayoutCreateInfo.pushConstantRangeCount = 2;
        pPipelineLayoutCreateInfo.pPushConstantRanges = &pushConstantRanges[0];

        VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pPipelineLayoutCreateInfo, nullptr, &out.cubemapPipelineLayout));


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

            VkResult r = vkAllocateDescriptorSets(device, &allocInfo, &out.meshes[i].descriptor_set);

            VK_CHECK_RESULT(r);

            std::vector<VkWriteDescriptorSet> writeDescriptorSets;

            BuffersList& uniformBuffersList = out.resources->GetBuffersList();
            const vks::Buffer& sceneBuffer = uniformBuffersList.Get(STATIC_CRC32("Scene"));

            // Binding 0 : Vertex shader uniform buffer
            writeDescriptorSets.push_back(vks::initializers::writeDescriptorSet(
                out.meshes[i].descriptor_set,
                VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                0,
                const_cast<VkDescriptorBufferInfo*>(&sceneBuffer.descriptor)));
            // Image bindings
            // Binding 0: Color map
            writeDescriptorSets.push_back(vks::initializers::writeDescriptorSet(
                out.meshes[i].descriptor_set,
                VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                1,
                &out.meshes[i].material->diffuse.descriptor));
            // Binding 1: Roughness
            writeDescriptorSets.push_back(vks::initializers::writeDescriptorSet(
                out.meshes[i].descriptor_set,
                VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                2,
                &out.meshes[i].material->roughness.descriptor));
            // Binding 2: Normal
            writeDescriptorSets.push_back(vks::initializers::writeDescriptorSet(
                out.meshes[i].descriptor_set,
                VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                3,
                &out.meshes[i].material->bump.descriptor));
            // Binding 3: Metallic
            writeDescriptorSets.push_back(vks::initializers::writeDescriptorSet(
                out.meshes[i].descriptor_set,
                VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                4,
                &out.meshes[i].material->metallic.descriptor));

            vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, NULL);
        }
    }

    void VkSceneController::DeallocateOnGPU(VkScene& out) const
    {
        out.index_buffer.device = m_vulkan_device->logicalDevice;
        out.raytrace_shape_buffer.device = m_vulkan_device->logicalDevice;
        out.raytrace_material_buffer.device = m_vulkan_device->logicalDevice;
        out.raytrace_RNG_buffer.device = m_vulkan_device->logicalDevice;

        out.index_buffer.destroy();
        out.raytrace_shape_buffer.destroy();
        out.raytrace_material_buffer.destroy();
        out.raytrace_RNG_buffer.destroy();
    }

    void VkSceneController::CreateDescriptorSets(VkScene& out) const
    {
        auto& device = m_vulkan_device->logicalDevice;
        // Generate descriptor sets for all meshes
        // todo : think about a nicer solution, better suited per material?

        // Descriptor pool
        std::vector<VkDescriptorPoolSize> poolSizes;
        poolSizes.push_back(vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, static_cast<uint32_t>(out.meshes.size())));
        poolSizes.push_back(vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, out.meshes.size()));
        poolSizes.push_back(vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, static_cast<uint32_t>(out.meshes.size() * 4)));
        poolSizes.push_back(vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, static_cast<uint32_t>(10)));

        VkDescriptorPoolCreateInfo descriptorPoolInfo =
            vks::initializers::descriptorPoolCreateInfo(
                static_cast<uint32_t>(poolSizes.size()),
                poolSizes.data(),
                static_cast<uint32_t>(out.meshes.size()));
        if (out.descriptorPool)
        {
            vkDestroyDescriptorPool(device, out.descriptorPool, nullptr);
        }
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
        // Binding 5: transform matrices
        setLayoutBindings.push_back(vks::initializers::descriptorSetLayoutBinding(
            VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
            VK_SHADER_STAGE_VERTEX_BIT,
            5));

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

            VkResult r = vkAllocateDescriptorSets(device, &allocInfo, &out.meshes[i].descriptor_set);

            VK_CHECK_RESULT(r);

            std::vector<VkWriteDescriptorSet> writeDescriptorSets;

            // Binding 0 : Vertex shader uniform buffer
            writeDescriptorSets.push_back(vks::initializers::writeDescriptorSet(
                out.meshes[i].descriptor_set,
                VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                0,
                &m_defaultUBO->descriptor));
            // Image bindings
            // Binding 1: Color map
            writeDescriptorSets.push_back(vks::initializers::writeDescriptorSet(
                out.meshes[i].descriptor_set,
                VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                1,
                &out.meshes[i].material->diffuse.descriptor));
            // Binding 2: Roughness
            writeDescriptorSets.push_back(vks::initializers::writeDescriptorSet(
                out.meshes[i].descriptor_set,
                VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                2,
                &out.meshes[i].material->roughness.descriptor));
            // Binding 3: Normal
            writeDescriptorSets.push_back(vks::initializers::writeDescriptorSet(
                out.meshes[i].descriptor_set,
                VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                3,
                &out.meshes[i].material->bump.descriptor));
            // Binding 4: Metallic
            writeDescriptorSets.push_back(vks::initializers::writeDescriptorSet(
                out.meshes[i].descriptor_set,
                VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                4,
                &out.meshes[i].material->metallic.descriptor));
            // Binding 5 : Shape shader uniform buffer
            writeDescriptorSets.push_back(vks::initializers::writeDescriptorSet(
                out.meshes[i].descriptor_set,
                VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
                5,
                &out.mesh_transform_buf.descriptor));

            vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, NULL);
        }
    }
}
