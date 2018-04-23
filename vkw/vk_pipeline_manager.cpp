#include "vk_pipeline_manager.h"

namespace vkw
{
    PipelineManager::PipelineManager(VkDevice device)
    : device_(device)
    {
        default_assembly_state_ = {};
        default_assembly_state_.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        default_assembly_state_.pNext = nullptr;
        default_assembly_state_.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        default_assembly_state_.flags = 0;
        default_assembly_state_.primitiveRestartEnable = false;
        
        default_rasterization_state_ = {};
        default_rasterization_state_.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        default_rasterization_state_.pNext = nullptr;
        default_rasterization_state_.polygonMode = VK_POLYGON_MODE_FILL;
        default_rasterization_state_.cullMode = VK_CULL_MODE_BACK_BIT;
        default_rasterization_state_.frontFace = VK_FRONT_FACE_CLOCKWISE;
        default_rasterization_state_.flags = 0;
        default_rasterization_state_.depthClampEnable = VK_FALSE;
        default_rasterization_state_.lineWidth = 1.0f;
        
        default_blend_attachment_state_ = {};
        default_blend_attachment_state_.colorWriteMask = 0xF;
        default_blend_attachment_state_.blendEnable = false;
        
        default_color_blend_state_ = {};
        default_color_blend_state_.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        default_color_blend_state_.pNext = nullptr;
        default_color_blend_state_.attachmentCount = 1;
        default_color_blend_state_.pAttachments = &default_blend_attachment_state_;
        
        default_viewport_state_ = {};
        default_viewport_state_.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        default_viewport_state_.pNext = nullptr;
        default_viewport_state_.viewportCount = 1;
        default_viewport_state_.scissorCount = 1;
        default_viewport_state_.flags = 0;
        
        default_depth_stencil_state_ = {};
        default_depth_stencil_state_.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        default_depth_stencil_state_.pNext = nullptr;
        default_depth_stencil_state_.depthTestEnable = VK_TRUE;
        default_depth_stencil_state_.depthWriteEnable = VK_TRUE;
        default_depth_stencil_state_.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
        default_depth_stencil_state_.front = default_depth_stencil_state_.back;
        default_depth_stencil_state_.back.compareOp = VK_COMPARE_OP_ALWAYS;
        
        default_multisample_state_ = {};
        default_multisample_state_.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        default_multisample_state_.pNext = nullptr;
        default_multisample_state_.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        default_multisample_state_.flags = 0;
    }
    
    ComputePipeline PipelineManager::CreateComputePipeline(Shader& shader,
                                                           std::size_t group_size_x,
                                                           std::size_t group_size_y,
                                                           std::size_t group_size_z)
    {
        ComputePipeline pipeline;
        
        VkPipelineLayoutCreateInfo layout_create_info;
        layout_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layout_create_info.pNext = nullptr;
        layout_create_info.flags = 0;
        layout_create_info.setLayoutCount = 1u;
        layout_create_info.pSetLayouts = shader.layout.GetObjectPtr();
        layout_create_info.pushConstantRangeCount = (std::uint32_t)shader.push_constant_ranges.size();
        layout_create_info.pPushConstantRanges = shader.push_constant_ranges.data();
        
        VkPipelineLayout layout = nullptr;
        auto res = vkCreatePipelineLayout(device_, &layout_create_info, nullptr, &layout);
        
        if (res != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create pipeline layout\n");
        }
        
        pipeline.layout = VkScopedObject<VkPipelineLayout>(layout,
                                                           [device = device_](VkPipelineLayout layout)
                                                           {
                                                               vkDestroyPipelineLayout(device, layout, nullptr);
                                                           });
        
        VkSpecializationMapEntry entries[] =
        {
            {0u, 0u, sizeof(std::size_t)},
            {1u, sizeof(std::size_t), sizeof(std::size_t)},
            {2u, 2 * sizeof(std::size_t), sizeof(std::size_t)}
        };
        
        std::size_t group_size[] = { group_size_x, group_size_y, group_size_z };
        VkSpecializationInfo specialization_info;
        specialization_info.mapEntryCount = 3u;
        specialization_info.dataSize = sizeof(std::size_t) * 3u;
        specialization_info.pMapEntries = entries;
        specialization_info.pData = group_size;
        
        VkComputePipelineCreateInfo pipeline_create_info;
        pipeline_create_info.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        pipeline_create_info.pNext = nullptr;
        pipeline_create_info.basePipelineIndex = -1;
        pipeline_create_info.basePipelineHandle = VK_NULL_HANDLE;
        pipeline_create_info.flags = 0;
        pipeline_create_info.layout = layout;
        pipeline_create_info.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        pipeline_create_info.stage.pNext = nullptr;
        pipeline_create_info.stage.flags = 0;
        pipeline_create_info.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        pipeline_create_info.stage.pName = "main";
        pipeline_create_info.stage.module = shader.module;
        pipeline_create_info.stage.pSpecializationInfo = &specialization_info;
        
        VkPipeline raw_pipeline = nullptr;
        res = vkCreateComputePipelines(device_, VK_NULL_HANDLE, 1u, &pipeline_create_info, nullptr, &raw_pipeline);
        
        if (res != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create compute pipeline\n");
        }
        
        pipeline.pipeline = VkScopedObject<VkPipeline>(raw_pipeline,
                                                       [device = device_](VkPipeline pipeline)
                                                       {
                                                           vkDestroyPipeline(device, pipeline, nullptr);
                                                       });
        
        return pipeline;
    }
    
    GraphicsPipeline PipelineManager::CreateGraphicsPipeline(Shader& vs_shader,
                                                             Shader& ps_shader,
                                                             VkRenderPass render_pass,
                                                             GraphicsPipelineState* create_state)
    {
        GraphicsPipeline pipeline;
        
        std::vector<VkDescriptorSetLayout> descriptor_set_layouts;
        descriptor_set_layouts.push_back(vs_shader.layout);
        descriptor_set_layouts.push_back(ps_shader.layout);
        
        std::vector<VkPushConstantRange> push_constant_ranges;
        
        for (uint32_t i = 0; i < vs_shader.push_constant_ranges.size(); i++)
            push_constant_ranges.push_back(vs_shader.push_constant_ranges[i]);
        
        for (uint32_t i = 0; i < ps_shader.push_constant_ranges.size(); i++)
            push_constant_ranges.push_back(ps_shader.push_constant_ranges[i]);
        
        VkPipelineLayoutCreateInfo layout_create_info;
        layout_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layout_create_info.pNext = nullptr;
        layout_create_info.flags = 0;
        layout_create_info.setLayoutCount = (std::uint32_t)descriptor_set_layouts.size();
        layout_create_info.pSetLayouts = descriptor_set_layouts.data();
        layout_create_info.pushConstantRangeCount = (std::uint32_t)push_constant_ranges.size();
        layout_create_info.pPushConstantRanges = push_constant_ranges.data();
        
        VkPipelineLayout layout = nullptr;
        auto res = vkCreatePipelineLayout(device_, &layout_create_info, nullptr, &layout);
        
        if (res != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create pipeline layout\n");
        }
        
        pipeline.layout = VkScopedObject<VkPipelineLayout>(layout,
                                                           [device = device_](VkPipelineLayout layout)
                                                           {
                                                               vkDestroyPipelineLayout(device, layout, nullptr);
                                                           });
        
        VkPipelineShaderStageCreateInfo vs_shader_stage_create_info;
        vs_shader_stage_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        vs_shader_stage_create_info.pNext = nullptr;
        vs_shader_stage_create_info.flags = 0;
        vs_shader_stage_create_info.stage = VK_SHADER_STAGE_VERTEX_BIT;
        vs_shader_stage_create_info.pName = "main";
        vs_shader_stage_create_info.module = vs_shader.module;
        vs_shader_stage_create_info.pSpecializationInfo = nullptr;
        
        VkPipelineShaderStageCreateInfo ps_shader_stage_create_info;
        ps_shader_stage_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        ps_shader_stage_create_info.pNext = nullptr;
        ps_shader_stage_create_info.flags = 0;
        ps_shader_stage_create_info.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        ps_shader_stage_create_info.pName = "main";
        ps_shader_stage_create_info.module = ps_shader.module;
        ps_shader_stage_create_info.pSpecializationInfo = nullptr;
        
        VkPipelineShaderStageCreateInfo stages[2] = {vs_shader_stage_create_info, ps_shader_stage_create_info};
        
        VkGraphicsPipelineCreateInfo pipeline_create_info;
        pipeline_create_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipeline_create_info.pNext = nullptr;
        pipeline_create_info.basePipelineIndex = -1;
        pipeline_create_info.basePipelineHandle = VK_NULL_HANDLE;
        pipeline_create_info.flags = 0;
        pipeline_create_info.layout = layout;
        pipeline_create_info.pStages = stages;
        pipeline_create_info.stageCount = 2;
        pipeline_create_info.renderPass = render_pass;
        pipeline_create_info.subpass = 0;
        pipeline_create_info.pTessellationState = nullptr;
        
        VkVertexInputBindingDescription vertex_input_bindings;
        vertex_input_bindings.binding = 0;
        vertex_input_bindings.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        vertex_input_bindings.stride = vs_shader.vertex_stride;
        
        VkPipelineVertexInputStateCreateInfo vertex_input_state;
        vertex_input_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertex_input_state.pNext = nullptr;
        vertex_input_state.vertexBindingDescriptionCount = 1;
        vertex_input_state.pVertexBindingDescriptions = &vertex_input_bindings;
        vertex_input_state.vertexAttributeDescriptionCount = (std::uint32_t)vs_shader.vertex_attributes.size();
        vertex_input_state.pVertexAttributeDescriptions = vs_shader.vertex_attributes.data();
        vertex_input_state.flags = 0;
        
        VkDynamicState dynamic_states[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
        
        VkPipelineDynamicStateCreateInfo dynamic_state = {};
        dynamic_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamic_state.pNext = nullptr;
        dynamic_state.flags = 0;
        dynamic_state.dynamicStateCount = 2;
        dynamic_state.pDynamicStates = dynamic_states;
        
        pipeline_create_info.pVertexInputState = &vertex_input_state;
        pipeline_create_info.pInputAssemblyState = &default_assembly_state_;
        pipeline_create_info.pRasterizationState = &default_rasterization_state_;
        pipeline_create_info.pColorBlendState = &default_color_blend_state_;
        pipeline_create_info.pViewportState = &default_viewport_state_;
        pipeline_create_info.pDepthStencilState = &default_depth_stencil_state_;
        pipeline_create_info.pMultisampleState = &default_multisample_state_;
        pipeline_create_info.pDynamicState = &dynamic_state;
        
        if (create_state)
        {
            if (create_state->assemply_state)
                pipeline_create_info.pInputAssemblyState = create_state->assemply_state;
            
            if (create_state->rasterization_state)
                pipeline_create_info.pRasterizationState  = create_state->rasterization_state;
            
            if (create_state->color_blend_state)
                pipeline_create_info.pColorBlendState = create_state->color_blend_state;
            
            if (create_state->viewport_state)
                pipeline_create_info.pViewportState = create_state->viewport_state;
            
            if (create_state->depth_stencil_state)
                pipeline_create_info.pDepthStencilState = create_state->depth_stencil_state;
            
            if (create_state->multisample_state)
                pipeline_create_info.pMultisampleState = create_state->multisample_state;
        }
        
        VkPipeline raw_pipeline = nullptr;
        res = vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1u, &pipeline_create_info, nullptr, &raw_pipeline);
        
        if (res != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create graphics pipeline\n");
        }
        
        pipeline.pipeline = VkScopedObject<VkPipeline>(raw_pipeline,
                                                       [device = device_](VkPipeline pipeline)
                                                       {
                                                           vkDestroyPipeline(device, pipeline, nullptr);
                                                       });
        
        return pipeline;
    }
    
}
