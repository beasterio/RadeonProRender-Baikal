#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

#define LIGHT_COUNT 3

layout (location = 0) in vec3 inPos;

struct Light {
    vec4 position;
    vec4 target;
    vec4 color;
    mat4 viewMatrix;
};

layout (binding = 0) uniform UBO 
{
    vec4 viewPos;
    mat4 view;
    mat4 invView;
    mat4 invProj;
    vec4 params;
    Light lights[LIGHT_COUNT];
} ubo;

layout(push_constant) uniform PushConsts {
    int lightIdx;
} pushConsts;

out gl_PerVertex 
{
    vec4 gl_Position;   
};

 
void main()
{
    gl_Position =  ubo.lights[pushConsts.lightIdx].viewMatrix * vec4(inPos, 1.0);
}