#include "vk_shader_manager.h"
#include <fstream>
#include <vector>
#include <algorithm>
#include <iterator>

namespace vkw
{
    VkFormat ShaderManager::BaseTypeToVkFormat(spirv_cross::SPIRType type)
    {
        VkFormat format = VK_FORMAT_UNDEFINED;
        
        if (type.basetype == spirv_cross::SPIRType::Float)
        {
            switch(type.vecsize)
            {
                case 1 :
                    format = VK_FORMAT_R32_SFLOAT;
                    break;
                case 2:
                    format = VK_FORMAT_R32G32_SFLOAT;
                    break;
                case 3:
                    format = VK_FORMAT_R32G32B32_SFLOAT;
                    break;
                case 4:
                    format = VK_FORMAT_R32G32B32A32_SFLOAT;
                    break;
            }
        }
        
        if (type.basetype == spirv_cross::SPIRType::Int)
        {
            switch(type.vecsize)
            {
                case 1 :
                    format = VK_FORMAT_R32_SINT;
                    break;
                case 2:
                    format = VK_FORMAT_R32G32_SINT;
                    break;
                case 3:
                    format = VK_FORMAT_R32G32B32_SINT;
                    break;
                case 4:
                    format = VK_FORMAT_R32G32B32A32_SINT;
                    break;
            }
        }
        
        if (type.basetype == spirv_cross::SPIRType::UInt)
        {
            switch(type.vecsize)
            {
                case 1 :
                    format = VK_FORMAT_R32_UINT;
                    break;
                case 2:
                    format = VK_FORMAT_R32G32_UINT;
                    break;
                case 3:
                    format = VK_FORMAT_R32G32B32_UINT;
                    break;
                case 4:
                    format = VK_FORMAT_R32G32B32A32_UINT;
                    break;
            }
        }
        
        if (type.basetype == spirv_cross::SPIRType::Half)
        {
            switch(type.vecsize)
            {
                case 1 :
                    format = VK_FORMAT_R16_SFLOAT;
                    break;
                case 2:
                    format = VK_FORMAT_R16G16_SFLOAT;
                    break;
                case 3:
                    format = VK_FORMAT_R16G16B16_SFLOAT;
                    break;
                case 4:
                    format = VK_FORMAT_R16G16B16A16_SFLOAT;
                    break;
            }
        }
        
        return format;
    };
    
    std::uint32_t ShaderManager::GetNumDescriptorSets(spirv_cross::CompilerGLSL& glsl)
    {
        // The SPIR-V is now parsed, and we can perform reflection on it.
        spirv_cross::ShaderResources resources = glsl.get_shader_resources();
        
        auto num_sets = 0u;
        
        for (auto& resource: resources.storage_buffers)
        {
            auto set = glsl.get_decoration(resource.id, spv::DecorationDescriptorSet);
            num_sets = std::max(num_sets, set + 1);
        }
        
        for (auto& resource: resources.storage_images)
        {
            auto set = glsl.get_decoration(resource.id, spv::DecorationDescriptorSet);
            num_sets = std::max(num_sets, set + 1);
        }
        
        for (auto& resource: resources.sampled_images)
        {
            auto set = glsl.get_decoration(resource.id, spv::DecorationDescriptorSet);
            num_sets = std::max(num_sets, set + 1);
        }
        
        for (auto& resource: resources.separate_images)
        {
            auto set = glsl.get_decoration(resource.id, spv::DecorationDescriptorSet);
            num_sets = std::max(num_sets, set + 1);
        }
        
        for (auto& resource: resources.separate_samplers)
        {
            auto set = glsl.get_decoration(resource.id, spv::DecorationDescriptorSet);
            num_sets = std::max(num_sets, set + 1);
        }
        
        for (auto& resource: resources.uniform_buffers)
        {
            auto set = glsl.get_decoration(resource.id, spv::DecorationDescriptorSet);
            num_sets = std::max(num_sets, set + 1);
        }
        
        return num_sets;
    }
    
    void ShaderManager::PopulateBindings(spirv_cross::CompilerGLSL& glsl,
                                         int set_id,
                                         VkShaderStageFlags stage_flags,
                                         std::vector<VkDescriptorSetLayoutBinding>& bindings)
    {
        // The SPIR-V is now parsed, and we can perform reflection on it.
        spirv_cross::ShaderResources resources = glsl.get_shader_resources();
        
        for (auto& resource: resources.storage_buffers)
        {
            auto set = glsl.get_decoration(resource.id, spv::DecorationDescriptorSet);
            
            if (set_id == set)
            {
                VkDescriptorSetLayoutBinding binding;
                binding.binding = glsl.get_decoration(resource.id, spv::DecorationBinding);
                binding.descriptorCount = 1u;
                binding.pImmutableSamplers = nullptr;
                binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                binding.stageFlags = stage_flags;
                bindings.push_back(binding);
            }
        }
        
        for (auto& resource: resources.storage_images)
        {
            auto set = glsl.get_decoration(resource.id, spv::DecorationDescriptorSet);
            
            if (set_id == set)
            {
                VkDescriptorSetLayoutBinding binding;
                binding.binding = glsl.get_decoration(resource.id, spv::DecorationBinding);
                binding.descriptorCount = 1u;
                binding.pImmutableSamplers = nullptr;
                binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
                binding.stageFlags = stage_flags;
                bindings.push_back(binding);
            }
        }
        
        for (auto& resource: resources.sampled_images)
        {
            auto set = glsl.get_decoration(resource.id, spv::DecorationDescriptorSet);
            
            if (set_id == set)
            {
                VkDescriptorSetLayoutBinding binding;
                binding.binding = glsl.get_decoration(resource.id, spv::DecorationBinding);
                binding.descriptorCount = 1u;
                binding.pImmutableSamplers = nullptr;
                binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                binding.stageFlags = stage_flags;
                bindings.push_back(binding);
            }
        }
        
        for (auto& resource: resources.separate_images)
        {
            auto set = glsl.get_decoration(resource.id, spv::DecorationDescriptorSet);
            
            if (set_id == set)
            {
                VkDescriptorSetLayoutBinding binding;
                binding.binding = glsl.get_decoration(resource.id, spv::DecorationBinding);
                binding.descriptorCount = 1u;
                binding.pImmutableSamplers = nullptr;
                binding.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
                binding.stageFlags = stage_flags;
                bindings.push_back(binding);
            }
        }
        
        for (auto& resource: resources.separate_samplers)
        {
            auto set = glsl.get_decoration(resource.id, spv::DecorationDescriptorSet);
            
            if (set_id == set)
            {
                VkDescriptorSetLayoutBinding binding;
                binding.binding = glsl.get_decoration(resource.id, spv::DecorationBinding);
                binding.descriptorCount = 1u;
                binding.pImmutableSamplers = nullptr;
                binding.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
                binding.stageFlags = stage_flags;
                bindings.push_back(binding);
            }
        }
        
        for (auto& resource: resources.uniform_buffers)
        {
            auto set = glsl.get_decoration(resource.id, spv::DecorationDescriptorSet);
            
            if (set_id == set)
            {
                VkDescriptorSetLayoutBinding binding;
                binding.binding = glsl.get_decoration(resource.id, spv::DecorationBinding);
                binding.descriptorCount = 1u;
                binding.pImmutableSamplers = nullptr;
                binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                binding.stageFlags = stage_flags;
                bindings.push_back(binding);
            }
        }
    }
    
    void
    ShaderManager::CreateShaderModule(VkShaderStageFlags binding_stage_flags,
                                      std::vector<std::uint32_t> const& bytecode,
                                      Shader& shader)
    {
        spirv_cross::CompilerGLSL glsl(bytecode);
        
        auto num_descriptor_sets = GetNumDescriptorSets(glsl);
        
        if (num_descriptor_sets > 1)
        {
            throw std::runtime_error("VkShaderManager: only one descriptor set is currently supported per shader");
        }
        
        VkDescriptorSetLayout raw_layout = nullptr;
        
        for (auto i = 0; i < num_descriptor_sets; ++i)
        {
            std::vector<VkDescriptorSetLayoutBinding> bindings;
            PopulateBindings(glsl, i, binding_stage_flags, bindings);
            
            VkDescriptorSetLayoutCreateInfo layout_create_info;
            layout_create_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
            layout_create_info.pNext = nullptr;
            layout_create_info.flags = 0;
            layout_create_info.bindingCount = (std::uint32_t)bindings.size();
            layout_create_info.pBindings = bindings.data();
            
            vkCreateDescriptorSetLayout(device_, &layout_create_info, nullptr, &raw_layout);
            
            shader.bindings.clear();
            for (auto& b: bindings)
            {
                for (auto i = 0; i < b.descriptorCount; ++i)
                {
                    Shader::Binding binding;
                    binding.type = b.descriptorType;
                    binding.ptr = nullptr;
                    shader.bindings[b.binding + i] = binding;
                }
            }
            
            shader.device = device_;
        }
        
        shader.layout = VkScopedObject<VkDescriptorSetLayout>(raw_layout,
                                                              [this](VkDescriptorSetLayout layout)
                                                              {
                                                                      vkDestroyDescriptorSetLayout(device_,
                                                                                                   layout,
                                                                                                   nullptr);
                                                                  
                                                              });
        
        spirv_cross::ShaderResources resources = glsl.get_shader_resources();
        
        shader.push_constant_ranges.clear();
        if (!resources.push_constant_buffers.empty())
        {
            auto ranges = glsl.get_active_buffer_ranges(resources.push_constant_buffers.front().id);
            
            std::transform(ranges.cbegin(),
                           ranges.cend(),
                           std::back_inserter(shader.push_constant_ranges),
                           [binding_stage_flags](spirv_cross::BufferRange const& range)
                           {
                               VkPushConstantRange push_constant_range;
                               push_constant_range.stageFlags = binding_stage_flags;
                               push_constant_range.offset = (std::uint32_t)range.offset;
                               push_constant_range.size = (std::uint32_t)range.range;
                               return push_constant_range;
                           });
        }
        
        if (binding_stage_flags == VK_SHADER_STAGE_VERTEX_BIT)
        {
            shader.vertex_attributes.resize(resources.stage_inputs.size());
            
            uint32_t offset = 0;
            
            for (auto i = 0; i < resources.stage_inputs.size(); i++)
            {
                const spirv_cross::SPIRType type = glsl.get_type(resources.stage_inputs[i].type_id);
                
                VkVertexInputAttributeDescription vertex_attribute;
                vertex_attribute.binding = 0;
                vertex_attribute.offset = offset;
                vertex_attribute.format = BaseTypeToVkFormat(type);
                vertex_attribute.location = i;
                
                offset += type.width;
                
                shader.vertex_attributes[i] = vertex_attribute;
            }
        }
        
        VkShaderModuleCreateInfo module_create_info;
        module_create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        module_create_info.pNext = nullptr;
        module_create_info.flags = 0;
        module_create_info.pCode = bytecode.data();
        module_create_info.codeSize = (std::uint32_t)bytecode.size() * sizeof(std::uint32_t);
        
        VkShaderModule module = nullptr;
        auto res = vkCreateShaderModule(device_, &module_create_info, nullptr, &module);
        
        if (res != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create shader module\n");
        }
        
        shader.module = VkScopedObject<VkShaderModule>(module,
                                                       [this](VkShaderModule module)
                                                       {
                                                           vkDestroyShaderModule(device_, module, nullptr);
                                                       });
    }
    
    void
    ShaderManager::CreateShaderModule(VkShaderStageFlags binding_stage_flags,
                                        std::string file_name,
                                        Shader& shader)
    {
        std::ifstream in(file_name, std::ios::in | std::ios::binary);
        std::vector<std::uint32_t> code;
        
        if (in)
        {
            std::streamoff beg = in.tellg();
            in.seekg(0, std::ios::end);
            std::streamoff file_size = in.tellg() - beg;
            in.seekg(0, std::ios::beg);
            code.resize(static_cast<unsigned>((file_size + sizeof(std::uint32_t) - 1) / sizeof(std::uint32_t)));
            in.read((char*)&code[0], file_size);
        }
        else
        {
            throw std::runtime_error("Cannot read the contents of a file");
        }
        
        return CreateShaderModule(binding_stage_flags, code, shader);
    }
    
    Shader ShaderManager::CreateShader(VkShaderStageFlagBits binding_stage_flags, std::string const& file_name)
    {
        Shader shader;
        
        CreateShaderModule(binding_stage_flags, file_name, shader);
        
        shader.descriptor_set = descriptor_manager_.AllocateDescriptorSet(shader.layout);
        
        return shader;
    }
    
    Shader ShaderManager::CreateShader(VkShaderStageFlagBits binding_stage_flags, std::vector<std::uint32_t> const& bytecode)
    {
        Shader shader;
        
        CreateShaderModule(binding_stage_flags, bytecode, shader);
        
        shader.descriptor_set = descriptor_manager_.AllocateDescriptorSet(shader.layout);
        
        return shader;
    }
    
    static bool IsBufferType(VkDescriptorType type)
    {
        return
        type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER ||
        type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER ||
        type == VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER ||
        type == VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER ||
        type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC ||
        type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    }
    
    static bool IsImageType(VkDescriptorType type)
    {
        return
        type == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE ||
        type == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE ||
        type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    }
    
    void Shader::SetArg(std::uint32_t idx, VkBuffer buffer)
    {
        auto iter = bindings.find(idx);
        
        if (!IsBufferType(iter->second.type))
        {
            throw std::runtime_error("Shader: Shader argument type mismatch");
        }
        
        if (iter->second.buffer != buffer)
        {
            iter->second.buffer = buffer;
            SetDirty();
        }
    }
    
    void Shader::SetArg(std::uint32_t idx, VkImage image)
    {
        auto iter = bindings.find(idx);
        
        if (IsImageType(iter->second.type))
        {
            throw std::runtime_error("Shader: Shader argument type mismatch");
        }
        
        if (iter->second.image != image)
        {
            iter->second.image = image;
            SetDirty();
        }
        
    }
    
    void Shader::SetArg(std::uint32_t idx, VkSampler sampler)
    {
        auto iter = bindings.find(idx);
        
        if (iter->second.type != VK_DESCRIPTOR_TYPE_SAMPLER)
        {
            throw std::runtime_error("Shader: Shader argument type mismatch");
        }
        
        if (iter->second.sampler != sampler)
        {
            iter->second.sampler = sampler;
            SetDirty();
        }
    }
    
    void Shader::CommitArgs()
    {
        if (!dirty)
        {
            return;
        }
        
        std::vector<VkDescriptorBufferInfo> buffers(bindings.size());
        std::vector<VkWriteDescriptorSet> write_descriptor_sets;
        for (auto& b: bindings)
        {
            if (IsBufferType(b.second.type) && b.second.buffer)
            {
                buffers.push_back(VkDescriptorBufferInfo
                                  {
                                      b.second.buffer,
                                      0u,
                                      VK_WHOLE_SIZE
                                  });
                
                
                VkWriteDescriptorSet write_descriptor_set;
                write_descriptor_set.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                write_descriptor_set.pNext = nullptr;
                write_descriptor_set.descriptorCount = 1u;
                write_descriptor_set.descriptorType = b.second.type;
                write_descriptor_set.dstBinding = b.first;
                write_descriptor_set.dstArrayElement = 0;
                write_descriptor_set.dstSet = descriptor_set;
                write_descriptor_set.pBufferInfo = &buffers.back();
                write_descriptor_set.pImageInfo = nullptr;
                write_descriptor_set.pTexelBufferView = nullptr;
                
                write_descriptor_sets.push_back(write_descriptor_set);
            }
        }
        
        vkUpdateDescriptorSets(device,
                               (std::uint32_t)write_descriptor_sets.size(),
                               write_descriptor_sets.data(),
                               0u,
                               nullptr);
        
        ClearDirty();
    }
}
