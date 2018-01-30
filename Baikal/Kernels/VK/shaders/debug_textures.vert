#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout (location = 0) in vec3 inPos;
layout (location = 1) in vec2 inUV;

layout (binding = 0) uniform UBO 
{
    mat4 projection;
    mat4 modelview;
} ubo;

layout (location = 0) out vec2 outUV;
layout (location = 1) out float instanceIdx;

out gl_PerVertex
{
    vec4 gl_Position;
};

void main() 
{
    outUV = inUV;

    vec4 tmpPos = vec4(inPos, 1.0);
    tmpPos.x += gl_InstanceIndex;
    tmpPos.xy *= vec2(1.0/4.0, 1.0/3.0);
    
    instanceIdx = gl_InstanceIndex;

    gl_Position = ubo.projection * ubo.modelview * tmpPos;
}
