#pragma once

#include "resource_list.h"
#include "../utils/static_hash.h"

#include "VulkanInitializers.hpp"

class VertexDescriptionList : public ResourceList<VkPipelineVertexInputStateCreateInfo>
{
public:
    struct Vertex {
        glm::vec4 posUVx;
        glm::vec4 normalUVy;
        glm::vec4 tangent;
    };

public:
    VertexDescriptionList(vks::VulkanDevice& dev) : ResourceList(dev) {
      
        VkPipelineVertexInputStateCreateInfo input_state = vks::initializers::pipelineVertexInputStateCreateInfo();
        input_state.pNext = nullptr;
        input_state.vertexBindingDescriptionCount = 1;
        input_state.pVertexBindingDescriptions = _description;
        input_state.vertexAttributeDescriptionCount = 3;
        input_state.pVertexAttributeDescriptions = _attributes;

        Set(STATIC_CRC32("Vertex"), input_state);
    };

    ~VertexDescriptionList() {
    }
private:
    const uint32_t _vertex_stride = 12 * sizeof(float);
    VkVertexInputBindingDescription _description[1] = { vks::initializers::vertexInputBindingDescription(0, _vertex_stride, VK_VERTEX_INPUT_RATE_VERTEX) };
    VkVertexInputAttributeDescription _attributes[3] = {
        // Location 0: Position and texture coordinates
        vks::initializers::vertexInputAttributeDescription(0, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 0),
        // Location 1: Normal and texture coordinates
        vks::initializers::vertexInputAttributeDescription(0, 1, VK_FORMAT_R32G32B32A32_SFLOAT, sizeof(float) * 4),
        // Location 2: Tangent
        vks::initializers::vertexInputAttributeDescription(0, 2, VK_FORMAT_R32G32B32A32_SFLOAT, sizeof(float) * 8)
    };
};