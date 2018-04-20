#include "vk_pipeline_manager.h"

namespace vkw
{
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
    
    GraphicsPipeline PipelineManager::CreateGraphicsPipeline(Shader& vs_shader, Shader& ps_shader)
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
