#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout (binding = 1) uniform sampler2D samplerColor;
layout (binding = 2) uniform sampler2D samplerRoughness;
layout (binding = 3) uniform sampler2D samplerNormal;
layout (binding = 4) uniform sampler2D samplerMetaliness;

layout (location = 0) in vec3 inNormal;
layout (location = 1) in vec2 inUV;
layout (location = 2) in vec3 inColor;
layout (location = 3) in vec3 inWorldPos;
layout (location = 4) in vec3 inTangent;
layout (location = 5) in vec2 inMotion;
layout (location = 6) in vec4 inProjPos;

layout (location = 0) out uvec4 outNormal;
layout (location = 1) out vec4 outAlbedo;
layout (location = 2) out vec4 outMotion;

layout (constant_id = 0) const float NEAR_PLANE = 0.1f;
layout (constant_id = 1) const float FAR_PLANE = 500.0f;
layout (constant_id = 2) const int ENABLE_DISCARD = 0;

layout(push_constant) uniform PushConsts {
    uvec4 meshID;
    vec4 baseColor;
    vec4 roughness;
    vec4 metallic;
} pushConsts;

// Spheremap Transform
vec2 EncodeNormal(vec3 n)
{
    vec2 enc = normalize(n.xy) * (sqrt(abs(-n.z*0.5+0.5)));
    enc = enc * 0.5 + 0.5;
    return enc;
}

float Encode(float roughness, float metaliness)
{
    uint v0 = uint(roughness * 255.0f);
    uint v1 = uint(metaliness * 255.0f);
    
    return uintBitsToFloat((v0 << 8) | v1);
}

vec2 EncodeFloatInto2xHalfs(float data)
{
    uint v = floatBitsToUint(data);
    uint v0 = (v & 0xFFFF);
    uint v1 = ((v >> 16) & 0xFFFF);

    return vec2(uintBitsToFloat(v0), uintBitsToFloat(v1));
}

float Decode2xHalfsIntoFloat(vec2 data)
{
    uint v0 = floatBitsToUint(data.x);
    uint v1 = floatBitsToUint(data.y);
    
    uint v = (v1 << 16) | v0;

    return uintBitsToFloat(v);
}

// depth z/w - 24 bits, mesh id - 8 bits
uvec2 EncodeDepthAndMeshID(float depth, float meshID)
{
    uint idepth = uint(depth * 16777215.0f);
    return uvec2(idepth & 0xFFFF, ((idepth & 0xFF0000) >> 8) | uint(meshID));
}

// Decode depth z/w - 24 bits, mesh id - 8 bits
vec2 DecodeDepthAndMeshID(uvec2 data)
{
    float meshID 	= uintBitsToFloat(data.y & 0xF);
    float depth 	= uintBitsToFloat((data.y >> 8) | data.x);

    return vec2(depth, meshID);
}

void main() 
{
    vec4 color = pushConsts.baseColor.x < 0.0f ? texture(samplerColor, inUV) : pushConsts.baseColor;

    color.xyz = pow(color.xyzz, vec4(2.2f)).xyz;

    vec3 N = normalize(inNormal);
    vec3 T = normalize(inTangent);
    vec3 B = cross(N, T);
    mat3 TBN = mat3(T, B, N);
    vec3 nm = texture(samplerNormal, inUV).xyz * 2.0 - vec3(1.0);
    nm = TBN * normalize(nm);
    
    if (color.a < 0.5)
    {
        discard;
    }

    // Pack
    float roughness = pushConsts.roughness.x < 0.0f ? texture(samplerRoughness, inUV).r : pushConsts.roughness.x;
    float metaliness = pushConsts.metallic.x < 0.0f ? texture(samplerMetaliness, inUV).r : pushConsts.metallic.x;
    
    // Convert to linear
    vec2 brdfData = vec2(roughness, metaliness);
    brdfData = pow(brdfData.xyyy, vec4(2.2f)).xy;

    vec2 encodedNormal = (EncodeNormal(nm) * 0.5f + 0.5f) * 65535.0f;

    outNormal = uvec4(uint(encodedNormal.x), uint(encodedNormal.y), EncodeDepthAndMeshID(inProjPos.z / inProjPos.w, pushConsts.meshID[0]));
    outAlbedo = vec4(color.rgb, 1.0f);
    outMotion = vec4(inMotion, roughness, metaliness);
}