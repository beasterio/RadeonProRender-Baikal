#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout (binding = 1) uniform UBO 
{
    mat4 projection;
    mat4 view[6];
    vec4 camera_position;
} ubo;

layout (binding = 2) uniform samplerCube samplerEnv;

layout (location = 0) in vec2 inUV;

layout (location = 0) out vec4 outColor;

layout(push_constant) uniform PushConsts {
    int face;
} pushConsts;

void main() 
{
    vec2 uv = (inUV.xy * 2.0f - vec2(1.0f));
    uv.x = uv.x;

    vec4 ray_dir = vec4(uv.xy, -1.0f, 0.0f) * ubo.view[pushConsts.face];
    ray_dir.y = -ray_dir.y;
    
    outColor = vec4(texture(samplerEnv, normalize(ray_dir.xyz)).rgb, 1.0f);
}