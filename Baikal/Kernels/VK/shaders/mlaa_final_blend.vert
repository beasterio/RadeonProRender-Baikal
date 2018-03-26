#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout (location = 0) in vec4 inPosUvx;
layout (location = 1) in vec4 inNormalUVy;

layout (binding = 0) uniform UBO 
{
    mat4 projection;
    mat4 modelview;
} ubo;

layout (location = 0) out vec2 outUV;

out gl_PerVertex
{
    vec4 gl_Position;
};

void main() 
{
    outUV = vec2(inPosUvx.a, inNormalUVy.a);
    gl_Position = ubo.projection * ubo.modelview * vec4(inPosUvx.xyz, 1.0);
}