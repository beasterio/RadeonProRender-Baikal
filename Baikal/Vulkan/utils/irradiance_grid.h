#pragma once

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>

#include "SceneGraph/vkscene.h"

#include <vector>
#include <chrono>

#include "VulkanBuffer.hpp"

#include "cubemap_prefilter.h"
#include "cubemap_render.h"

const uint32_t g_irradiance_grid_buffer_name = STATIC_CRC32("IrradianceGrid");

class IrradianceGrid
{
public:
    const uint32_t _probe_budget = 2000;
public:
    struct Properties {
        glm::ivec3  grid_size;
        glm::ivec3  probes_count;
        glm::vec3   probe_dist;
        glm::vec3   scene_min;
    };
public:
    IrradianceGrid(const char* asset_path, vks::VulkanDevice* device, VkQueue queue, Baikal::VkScene& scene) : _scene(scene), _device(device) {
        const glm::vec3 diff = glm::floor(glm::vec3(_scene.bbox._max) - glm::vec3(_scene.bbox._min));

        const float sum = diff.x + diff.y + diff.z;
        const float alpha = diff.x / sum;
        const float beta = diff.y / sum;
        const float theta = diff.z / sum;

        const int x_probes = int(cbrt((float(_probe_budget) * alpha * alpha) / (beta * theta)));
        const int y_probes = int(cbrt((float(_probe_budget) * beta * beta) / (alpha * theta)));
        const int z_probes = int(float(_probe_budget) / (x_probes * y_probes));

        _properties.probes_count = glm::ivec3(x_probes, y_probes, z_probes);
        _properties.probe_dist = diff / (glm::vec3(_properties.probes_count) - glm::vec3(_properties.probes_count) * glm::vec3(0.1f));
         
        _properties.grid_size = glm::ivec3(
            _properties.probes_count.x * _properties.probe_dist.x,
            _properties.probes_count.y * _properties.probe_dist.y,
            _properties.probes_count.z * _properties.probe_dist.z);

        _properties.scene_min = glm::vec3(_scene.bbox._min);

        _num_probes = _properties.probes_count.x * _properties.probes_count.y * _properties.probes_count.z;

        _cubemap_prefilter = new CubemapPrefilter(asset_path, device, queue, scene.resources);
        _cubemap_render = new CubemapRender(device, queue, scene);

        VK_CHECK_RESULT(_device->createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &_sh_buffer, _num_probes * sizeof(CubemapPrefilter::SH9Color)));
        BuffersList& buffer_list = scene.resources->GetBuffersList();
        buffer_list.Set(g_irradiance_grid_buffer_name, _sh_buffer);
    }

    ~IrradianceGrid() {
        delete _cubemap_prefilter;
        delete _cubemap_render;

        _sh_buffer.destroy();
    }

    Properties GetProperties() {
        return _properties;
    }

    void Update() {
        auto start = std::chrono::high_resolution_clock::now();

        std::vector<VkSemaphore> semaphores;

        vks::TextureCubeMap cubemap = _cubemap_render->CreateCubemap(_cubemap_render->_cubemap_face_size, VK_FORMAT_R16G16B16A16_SFLOAT);

        for (int i = 0; i < _num_probes; i++) {
            _cubemap_render->RenderSceneToCubemap(_scene, ConvertIdxToPosition(i), cubemap, semaphores);
            _cubemap_prefilter->GenerateIrradianceSH9Coefficients(cubemap, _sh_buffer, i, semaphores);

            if (i % 100 == 0) {
                printf("%d/%d probes processed\n", i, _num_probes);
            }
        }

        auto end = std::chrono::high_resolution_clock::now();
        printf("Total time for SH probe generation: %f s\n", (float)std::chrono::duration<double, std::ratio<1, 1>>(end - start).count());

        cubemap.destroy();
    }

protected:
    glm::vec3 ConvertIdxToPosition(uint32_t idx) {
        uint32_t x = uint32_t(idx % _properties.probes_count.x);
        uint32_t z = uint32_t(idx % (_properties.probes_count.x * _properties.probes_count.z)) / _properties.probes_count.x;
        uint32_t y = uint32_t(idx / (_properties.probes_count.x * _properties.probes_count.z));

        return glm::vec3(_properties.scene_min) + glm::vec3(
            float(x) * _properties.probe_dist.x,
            float(y) * _properties.probe_dist.y,
            float(z) * _properties.probe_dist.z);
    }

    uint32_t ConvertPositionToIdx(glm::vec3 p) {
        p = p - glm::vec3(_properties.scene_min);
        int32_t x = int32_t(p.x / _properties.probe_dist.x);
        int32_t y = int32_t(p.y / _properties.probe_dist.y);
        int32_t z = int32_t(p.z / _properties.probe_dist.z);

        return x + z * _properties.probes_count.x + y * _properties.probes_count.x * _properties.probes_count.z;
    }
protected:
    vks::TextureCubeMap _cubemap;

    vks::VulkanDevice*  _device;

    CubemapRender*      _cubemap_render;
    CubemapPrefilter*   _cubemap_prefilter;

    vks::Buffer         _sh_buffer;

    Baikal::VkScene&              _scene;

    Properties          _properties;

    uint32_t            _num_probes;
};