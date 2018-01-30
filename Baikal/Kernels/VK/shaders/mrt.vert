#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout (location = 0) in vec4 inPos;
layout (location = 1) in vec2 inUV;
layout (location = 2) in vec3 inColor;
layout (location = 3) in vec3 inNormal;
layout (location = 4) in vec3 inTangent;

layout (binding = 0) uniform UBO 
{
    mat4 projection;
    mat4 view;
    mat4 prevViewProjection;
    vec4 params; // x, y - width, height, w - tan(fov/2)
    vec4 cameraPosition;
} ubo;

layout (location = 0) out vec3 outNormal;
layout (location = 1) out vec2 outUV;
layout (location = 2) out vec3 outColor;
layout (location = 3) out vec3 outWorldPos;
layout (location = 4) out vec3 outTangent;
layout (location = 5) out vec2 outMotion;
layout (location = 6) out vec4 outProjPos;

out gl_PerVertex
{
    vec4 gl_Position;
};

void main() 
{
    vec4 position_ps = ubo.projection * ubo.view * inPos;
    vec3 position_cs = position_ps.xyz / position_ps.w;
    vec2 position_ss = position_cs.xy * vec2(0.5f, -0.5f) + vec2(0.5f, 0.5f);
    
    vec4 prev_position_ps = ubo.prevViewProjection * inPos;
    vec2 prev_position_cs = prev_position_ps.xy / prev_position_ps.w;
    vec2 prev_position_ss = prev_position_cs * vec2(0.5f, -0.5f) + vec2(0.5f, 0.5f);

    outMotion = prev_position_ss - position_ss;
    
    outUV = inUV;
    outUV.t = 1.0 - outUV.t;

    // Vertex position in world space
    outWorldPos = vec3(ubo.view * inPos);

    // Normal in world space
    mat3 mNormal = mat3(ubo.view);
    outNormal = mNormal * normalize(inNormal);	
    outTangent = mNormal * normalize(inTangent);
    
    // Currently just vertex color
    outColor = inColor;
    outProjPos = position_ps;

    gl_Position = position_ps;
}