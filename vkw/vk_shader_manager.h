/**********************************************************************
 Copyright (c) 2016 Advanced Micro Devices, Inc. All rights reserved.
 
 Permission is hereby granted, free of charge, to any person obtaining a copy
 of this software and associated documentation files (the "Software"), to deal
 in the Software without restriction, including without limitation the rights
 to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 copies of the Software, and to permit persons to whom the Software is
 furnished to do so, subject to the following conditions:
 
 The above copyright notice and this permission notice shall be included in
 all copies or substantial portions of the Software.
 
 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
 AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 THE SOFTWARE.
 ********************************************************************/
#pragma once
#include <vulkan/vulkan.h>
#include "vk_scoped_object.h"
#include "vk_descriptor_manager.h"
#include "spirv_glsl.hpp"

#include <string>

namespace vkw
{
    struct Shader
    {
        void SetArg(std::uint32_t idx, VkBuffer buffer);
        void SetArg(std::uint32_t idx, VkImage image);
        void SetArg(std::uint32_t idx, VkSampler sampler);
        void CommitArgs();
        void SetDirty() { dirty = true; }
        void ClearDirty() { dirty = false; }
        
        VkScopedObject<VkShaderModule> module;
        VkScopedObject<VkDescriptorSetLayout> layout;
        VkScopedObject<VkDescriptorSet> descriptor_set;
        
        std::vector<VkVertexInputAttributeDescription> vertex_attributes;
        std::vector<VkPushConstantRange> push_constant_ranges;
        
        struct Binding
        {
            union
            {
                VkBuffer buffer;
                VkImage image;
                VkSampler sampler;
                void* ptr;
            };
            
            VkDescriptorType type;
        };
        
        VkDevice device;
        std::unordered_map<std::uint32_t, Binding> bindings;
        bool dirty = true;
    };
    
    class ShaderManager
    {
    public:
        ShaderManager(VkDevice device,
                      DescriptorManager& descriptor_manager)
        : device_(device)
        , descriptor_manager_(descriptor_manager)
        {
        }
        
        Shader CreateShader(VkShaderStageFlagBits binding_stage_flags, std::string const& file_name);
        Shader CreateShader(VkShaderStageFlagBits binding_stage_flags, std::vector<std::uint32_t> const& bytecode);
        
    private:
        void CreateShaderModule(VkShaderStageFlags binding_stage_flags,
                                std::vector<std::uint32_t> const& bytecode,
                                Shader& shader);
        
        void CreateShaderModule(VkShaderStageFlags binding_stage_flags,
                                std::string file_name,
                                Shader& shader);
        
        static std::uint32_t GetNumDescriptorSets(spirv_cross::CompilerGLSL& glsl);
        static void PopulateBindings(spirv_cross::CompilerGLSL& glsl,
                                     int set_id,
                                     VkShaderStageFlags stage_flags,
                                     std::vector<VkDescriptorSetLayoutBinding>& bindings);
        
        static VkFormat BaseTypeToVkFormat(spirv_cross::SPIRType type);
        
        VkDevice device_;
        DescriptorManager& descriptor_manager_;
    };
}
