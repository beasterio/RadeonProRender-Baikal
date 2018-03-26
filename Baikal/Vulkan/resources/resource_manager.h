#pragma once

#include "shader_list.h"
#include "texture_resource_list.h"
#include "pipeline_layout_resource_list.h"
#include "desc_set_layout_resource_list.h"
#include "buffers_list.h"
#include "vertex_declaration_list.h"

class ResourceManager
{
public:
    ResourceManager(vks::VulkanDevice& device, VkQueue queue) : 
        _device(device)
        , _shaders(device)
        , _pipeline_layouts(device)
        , _pipelines(device)
        , _desc_set_layouts(device)
        , _desc_sets(device)
        , _buffers(device)
        , _vertex_descriptions(device)
        , _texture_list(device, queue) {
        }
public:
    inline ShaderList& GetShaderList() { return _shaders; };
    inline PipelineList& GetPipepineList() { return _pipelines; };
    inline PipelineLayoutList& GetPipepineLayoutList() { return _pipeline_layouts; };
    inline DescriptionSetLayoutList& GetDescriptionSetLayoutList() { return _desc_set_layouts; };
    inline DescriptionSetList& GetDescriptionSetList() { return _desc_sets; };
    inline BuffersList& GetBuffersList() { return _buffers; };
    inline VertexDescriptionList& GetVertexDescriptions() { return _vertex_descriptions; };
    inline TextureList& GetTextures() { return _texture_list; };
    inline TextureList const& GetTextures() const { return _texture_list; };
private:
    ShaderList                  _shaders;
    PipelineLayoutList          _pipeline_layouts;
    PipelineList                _pipelines;
    DescriptionSetLayoutList    _desc_set_layouts;
    DescriptionSetList          _desc_sets;
    BuffersList                 _buffers;
    VertexDescriptionList       _vertex_descriptions;
    TextureList                 _texture_list;

    vks::VulkanDevice& _device;
};
