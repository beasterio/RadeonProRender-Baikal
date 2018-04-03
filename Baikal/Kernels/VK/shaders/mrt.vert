#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout (location = 0) in vec4 inPos;
layout (location = 1) in vec4 inNormal;
layout (location = 2) in vec4 inTangent;

layout (binding = 0) uniform UBO 
{
    mat4 projection;
    mat4 view;
    mat4 prevViewProjection;
    vec4 params; // x, y - width, height, w - tan(fov/2)
    vec4 cameraPosition;
} ubo;

layout (binding = 10) uniform DynamicUBO 
{
    mat4 model; 
} dynamicUbo;

layout (location = 0) out vec3 outNormal;
layout (location = 1) out vec2 outUV;
layout (location = 2) out vec3 outWorldPos;
layout (location = 3) out vec3 outTangent;
layout (location = 4) out vec2 outMotion;
layout (location = 5) out vec4 outProjPos;

out gl_PerVertex
{
    vec4 gl_Position;
};

void main() 
{
    vec4 position_ps = ubo.projection * ubo.view * dynamicUbo.model * vec4(inPos.xyz, 1.0f);
    //vec4 position_ps = ubo.projection * ubo.view * vec4(inPos.xyz, 1.0f);
    //vec4 position_ps = ubo.projection * ubo.view * dynamicUbo.model * vec4(inNormal.x, inNormal.y,inNormal.z, 1.0f);
    vec3 position_cs = position_ps.xyz / position_ps.w;
    vec2 position_ss = position_cs.xy * vec2(0.5f, -0.5f) + vec2(0.5f, 0.5f);
    
    //vec4 prev_position_ps = ubo.prevViewProjection * dynamicUbo.model * vec4(inPos.xyz, 1.0f);
    //vec4 prev_position_ps = ubo.prevViewProjection * vec4(inPos.xyz, 1.0f);
    vec4 prev_position_ps = ubo.prevViewProjection * dynamicUbo.model * vec4(inPos.xyz, 1.0f);
    vec2 prev_position_cs = prev_position_ps.xy / prev_position_ps.w;
    vec2 prev_position_ss = prev_position_cs * vec2(0.5f, -0.5f) + vec2(0.5f, 0.5f);

    outMotion = prev_position_ss - position_ss;
    
    outUV = vec2(inPos.a, inNormal.a);
    outUV.t = 1.0 - outUV.t;

    // Vertex position in world space
    //outWorldPos = vec3(ubo.view * dynamicUbo.model * vec4(inPos.xyz, 1.0f));
    //outWorldPos = vec3(ubo.view * vec4(inPos.xyz, 1.0f));
    outWorldPos = vec3(ubo.view * dynamicUbo.model * vec4(inPos.xyz, 1.0f));

    // Normal in world space
    //mat3 mNormal = mat3(ubo.view * dynamicUbo.model);
    //mat3 mNormal = mat3(ubo.view);
    mat3 mNormal = mat3(ubo.view * dynamicUbo.model);
    outNormal = mNormal * normalize(inNormal.xyz);	
    outTangent = mNormal * normalize(inTangent.xyz);
    
    outProjPos = position_ps;

    gl_Position = position_ps;
}