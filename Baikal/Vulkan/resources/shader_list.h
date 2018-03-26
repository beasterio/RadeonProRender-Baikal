#pragma once

#include "resource_list.h"

#include "VulkanTools.h"

#include "../utils/static_hash.h"

class ShaderList : public ResourceList<VkShaderModule>
{
public:
    VkPipelineShaderStageCreateInfo Load(const char* shader_path, VkShaderStageFlagBits stage) {
        assert(_device != nullptr);

        char path[MAX_PATH];
        sprintf_s(path, "%s%s", g_asset_path, shader_path);

        VkPipelineShaderStageCreateInfo shader_stage = {};
        shader_stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shader_stage.stage = stage;
        shader_stage.module = vks::tools::loadShader(path, _device.logicalDevice);

        shader_stage.pName = "main";
        assert(shader_stage.module != VK_NULL_HANDLE);
        
        uint32_t hash_name = Utils::crc32(shader_path);
        Set(hash_name, shader_stage.module);

        return shader_stage;
    }

    ShaderList(vks::VulkanDevice& dev) : ResourceList(dev) {
    };

    ~ShaderList() {
        for (auto it : _resources) {
            vkDestroyShaderModule(_device, it.second, nullptr);
        }
    }
};