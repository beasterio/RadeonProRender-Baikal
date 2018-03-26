#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout (location = 0) in vec4 inPos;
layout (location = 1) in vec4 inNormal;
layout (location = 2) in vec4 inTangent;

layout (binding = 0) uniform UBO 
{
    mat4 projection;
    mat4 view[6];
    vec4 camera_position;
} ubo;

layout (location = 0) out vec3 outNormal;
layout (location = 1) out vec2 outUV;
layout (location = 2) out vec3 outWorldPos;
layout (location = 3) out vec3 outTangent;
layout (location = 4) out vec2 outMotion;
layout (location = 5) out vec4 outCameraPosition;

out gl_PerVertex
{
    vec4 gl_Position;
};

layout(push_constant) uniform PushVSConsts {
    int face;
    int padding[3];
} pushVSConsts;

void main() 
{
    int face = pushVSConsts.face;
    vec3 cameraPosition = ubo.camera_position.xyz;

    vec4 position_ps = ubo.projection * ubo.view[face] * vec4(inPos.xyz - cameraPosition, 1.0f);

    outUV = vec2(inPos.a, inNormal.a);
    outUV.t = 1.0 - outUV.t;

    outWorldPos = inPos.xyz;
    outNormal = normalize(inNormal.xyz);	
    outTangent = normalize(inTangent.xyz);
    
    outCameraPosition = ubo.camera_position;

    gl_Position = position_ps;
}