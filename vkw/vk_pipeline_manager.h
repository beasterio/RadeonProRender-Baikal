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
#include "vk_shader_manager.h"
#include "spirv_glsl.hpp"

#include <string>
#include <vector>

namespace vkw
{
    struct ComputePipeline
    {
        VkScopedObject<VkPipeline> pipeline;
        VkScopedObject<VkPipelineLayout> layout;
    };
    
    struct GraphicsPipeline
    {
        VkScopedObject<VkPipeline> pipeline;
        VkScopedObject<VkPipelineLayout> layout;
    };
    
    class PipelineManager
    {
    public:
        PipelineManager(VkDevice device)
        : device_(device)
        {
        }
        
        // TODO: Add raster stages, depth-stencil, blend etc.
        GraphicsPipeline CreateGraphicsPipeline(Shader& vs_shader,
                                                Shader& ps_shader);
        
        ComputePipeline CreateComputePipeline(Shader& shader,
                                              std::size_t group_size_x,
                                              std::size_t group_size_y,
                                              std::size_t group_size_z);
        
    private:
        VkDevice device_;
    };
}

