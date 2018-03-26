#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

#define LIGHT_COUNT 3
#define PI 3.14159265358979f
#define EPS 0.00000001f

layout (binding = 1) uniform sampler2D samplerColor;
layout (binding = 2) uniform sampler2D samplerRoughness;
layout (binding = 3) uniform sampler2D samplerNormal;
layout (binding = 4) uniform sampler2D samplerMetaliness;

layout (binding = 5) uniform sampler2D samplerShadowMapLight0;
layout (binding = 6) uniform sampler2D samplerShadowMapLight1;
layout (binding = 7) uniform sampler2D samplerShadowMapLight2;

struct Light 
{
    vec4 position;
    vec4 target;
    vec4 color;
    mat4 viewMatrix;
};

layout (binding = 8) uniform UBO 
{
    vec4 viewPos;
    mat4 view;
    mat4 invView;
    mat4 invProj;
    vec4 params;
    Light lights[LIGHT_COUNT];
} ubo;

layout (binding = 9) uniform samplerCube samplerEnv;

layout (location = 0) in vec3 inNormal;
layout (location = 1) in vec2 inUV;
layout (location = 2) in vec3 inWorldPos;
layout (location = 3) in vec3 inTangent;
layout (location = 4) in vec2 inMotion;
layout (location = 5) in vec4 inCameraPosition;

layout (location = 0) out vec4 outColor;

layout (constant_id = 0) const float NEAR_PLANE = 0.1f;
layout (constant_id = 1) const float FAR_PLANE = 500.0f;
layout (constant_id = 2) const int ENABLE_DISCARD = 0;

layout(push_constant) uniform PushConsts {
    layout(offset = 16) uvec4 meshID;
    vec4 baseColor;
    vec4 roughness;
    vec4 metallic;
} pushConsts;

struct BRDFInputs 
{
    vec3 albedo;
    float roughness;
    float metallic;
    float transparency;
};

float textureProj(int lightIdx, vec4 P, vec2 offset)
{
    float shadow = 1.0;
    vec4 shadowCoord = P / P.w;
    shadowCoord.st = shadowCoord.st * 0.5 + 0.5;
    
    if (shadowCoord.z > -1.0 && shadowCoord.z < 1.0) 
    {
        float dist = 0.f;

        // TODO: deferred shadows or texture array?
        switch(lightIdx)
        {
            case 0 : dist = texture(samplerShadowMapLight0, vec2(shadowCoord.st + offset)).r; break;
            case 1 : dist = texture(samplerShadowMapLight1, vec2(shadowCoord.st + offset)).r; break;
            case 2 : dist = texture(samplerShadowMapLight2, vec2(shadowCoord.st + offset)).r; break;
        };

        if (shadowCoord.w > 0.0 && dist < shadowCoord.z) 
        {
            shadow = 0.3;
        }
    }
    return shadow;
}

float filterPCF(int lightIdx, vec4 sc)
{
    ivec2 texDim = textureSize(samplerShadowMapLight0, 0).xy;
    float scale = 1.5;
    float dx = scale * 1.0 / float(texDim.x);
    float dy = scale * 1.0 / float(texDim.y);

    float shadowFactor = 0.0;
    int count = 0;
    int range = 1;
    
    for (int x = -range; x <= range; x++)
    {
        for (int y = -range; y <= range; y++)
        {
            shadowFactor += textureProj(lightIdx, sc, vec2(dx*x, dy*y));
            count++;
        }
    
    }
    return shadowFactor / count;
}

vec3 OrthoVector(vec3 n)
{
    vec3 p;

    if (abs(n.z) > 0.0)
    {
        float k = sqrt(n.y*n.y + n.z*n.z);
        p.x = 0; p.y = -n.z / k; p.z = n.y / k;
    }
    else
    {
        float k = sqrt(n.x*n.x + n.y*n.y);
        p.x = n.y / k; p.y = -n.x / k; p.z = 0;
    }

    return p;
}

float nrand(vec2 n)
{
    return fract(sin(dot(n.xy, vec2(12.9898f, 78.233f))) * 43758.5453f);
}

vec3 MapToHemisphere(vec2 s, vec3 n, float e)
{
    // Construct basis
    vec3 u = OrthoVector(n);
    vec3 v = cross(u, n);
    u = cross(n, v);

    // Calculate 2D sample
    float r1 = s.x;
    float r2 = s.y;

    // Transform to spherical coordinates
    float sinPsi = sin(2 * PI*r1);
    float cosPsi = cos(2 * PI*r1);
    float cosTheta = pow(1.0 - r2, 1.0 / (e + 1.0));
    float sinTheta = sqrt(1.0 - cosTheta * cosTheta);

    // Return the result
    return normalize(u * sinTheta * cosPsi + v * sinTheta * sinPsi + n * cosTheta);
}

vec2 hash2(float n) { return fract(sin(vec2(n, n + 1.0))*vec2(43758.5453123, 22578.1459123)); }


vec3 ReconstructVSPositionFromDepth(vec2 uv, float depth)
{
    uv = uv * vec2(2.0f, 2.0f) - vec2(1.0f, 1.0f);

    vec4 projPos = vec4(uv, depth, 1.0f);
    vec4 viewPos = ubo.invProj * projPos;

    return viewPos.xyz / viewPos.w;
}

vec3 GGX_Sample(vec2 s, float roughness, vec3 N)
{
    float a = roughness * roughness;
    float Phi = 2.0 * PI * s.x;
    float CosTheta = sqrt((1 - s.y) / (1 + (a*a - 1) * s.y));
    float SinTheta = sqrt(1 - CosTheta * CosTheta);

    vec3 H = vec3(SinTheta * cos(Phi), CosTheta, SinTheta * sin(Phi));
    vec3 U = abs(N.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    vec3 X = normalize(cross(U, N));
    vec3 Y = cross(N, X);

    return X * H.x + Y * H.z + N * H.y;
}

float GGX_D(float roughness, float NdotH)
{
    float a2 = roughness * roughness;
    float v = (NdotH * NdotH * (a2 - 1) + 1);
    return a2 / (PI * v * v);
}

float GGX_G1(vec3 N, vec3 V, float k)
{
    const float NdotV = clamp(dot(N, V), 0.0, 1.0);
    return NdotV / (NdotV * (1.0 - k) + k);
}

float GGX_G(float roughness, vec3 N, vec3 V, vec3 L)
{
    const float t = roughness + 1.0;
    const float k = t * t / 8.0;

    return GGX_G1(N, V, k) * GGX_G1(N, L, k);
}

vec3 F_SchlickR(float cosTheta, vec3 F0, float roughness)
{
	return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(1.0 - cosTheta, 5.0);
}

// Stereographic Projection
vec3 DecodeNormal (uvec2 enc)
{
    vec2 enc_n = vec2(enc.xy) / 65535.0f;
    enc_n = enc_n * 2.0f - vec2(1.0f);

    float scale = 1.7777f;
    vec3 nn = enc_n.xyy * vec3(2.0f*scale, 2.0f * scale, 0.0f) + vec3(-scale,-scale,1);
    float g = 2.0f / dot(nn.xyz,nn.xyz);
    vec3 n;
    n.xy = g*nn.xy;
    n.z = g-1;

    return n;
}

float Encode(float roughness, float metaliness)
{
    uint v0 = uint(roughness * 255.0f);
    uint v1 = uint(metaliness * 255.0f);
    
    return uintBitsToFloat((v0 << 8) | v1);
}

vec2 Decode(float data)
{
    uint v = floatBitsToUint(data);
    return vec2(float((v >> 8) & 0xFF) / 255.0f, float(v & 0xFF) / 255.0f);
}

// Decode depth z/w - 24 bits, mesh id - 8 bits
vec2 DecodeDepthAndMeshID(uvec2 data)
{
    float meshID 		= float(data.y & 0xFF);
    uint depthHighBits 	= ((data.y >> 8) & 0xFF) << 16;
    float depth 		= float(depthHighBits | data.x) / 16777215.0f;

    return vec2(depth, meshID);
}

vec3 BRDF_Diffuse(vec3 wi, vec3 wo, vec3 albedo)
{
	return albedo / PI;
}

vec3 FresnelSchlick(vec3 albedo, vec3 H, vec3 V)
{
    const float VdotH = clamp(dot(V, H), 0.0, 1.0);
    return (albedo + (1.0f - albedo) * pow(1.0 - VdotH, 5.0));
}

vec3 BRDF_Evaluate(BRDFInputs inputs,
    vec3 V,
    vec3 N,
    vec3 L)
{
    vec3 specAlbedo = mix(vec3(0.03f), inputs.albedo, inputs.metallic);

    vec3 H = normalize(L + V);
    float NdotV = clamp(dot(N, V), 0.0, 1.0);
    float NdotH = clamp(dot(N, H), 0.0, 1.0);
    float NdotL = clamp(dot(N, L), 0.0, 1.0);

    vec3 diffuse = (1.0 - inputs.metallic) * inputs.albedo.xyz / PI;

    float D = GGX_D(inputs.roughness, NdotH);
    float G = GGX_G(inputs.roughness, N, V, L);
    vec3  F = FresnelSchlick(specAlbedo, H, V);
    vec3 specular = (D * G * F) / (4.0f * NdotL * NdotV + EPS);

    return diffuse + specular;
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
    N = TBN * normalize(nm);
    
    if (color.a < 0.5) {
        discard;
    }

    // Pack
    float roughness = pushConsts.roughness.x < 0.0f ? texture(samplerRoughness, inUV).r : pushConsts.roughness.x;
    float metaliness = pushConsts.metallic.x < 0.0f ? texture(samplerMetaliness, inUV).r : pushConsts.metallic.x;
    
    // Convert to linear
    vec2 brdfData = vec2(roughness, metaliness);
    brdfData = pow(brdfData.xyyy, vec4(2.2f)).xy;
    float opacity = metaliness > 0.5 && roughness < 0.6 ? 0.0 : 1.0;

    vec3 V = normalize(inCameraPosition.xyz - inWorldPos);

    BRDFInputs inputs;
    inputs.albedo = color.xyz;
    inputs.roughness = roughness;
    inputs.metallic = metaliness;

    vec3 fragcolor = vec3(0.f);

    for(int i = 0; i < LIGHT_COUNT; ++i)
    {
        vec4 shadowClip	= ubo.lights[i].viewMatrix * vec4(inWorldPos, 1.0f);
        float shadowFactor = filterPCF(i, shadowClip);

        // Vector to light
        vec3 L = ubo.lights[i].position.xyz - inWorldPos.xyz;
        // Distance from light to fragment position
        float dist = length(L);
        L = normalize(L);

        float lightCosInnerAngle = cos(radians(15.0));
        float lightCosOuterAngle = cos(radians(25.0));

        // Direction vector from source to target
        vec3 dir = normalize(ubo.lights[i].position.xyz - ubo.lights[i].target.xyz);

        // Dual cone spot light with smooth transition between inner and outer angle
        float cosDir = dot(L, dir);
        float spotEffect = smoothstep(lightCosOuterAngle, lightCosInnerAngle, cosDir);

        vec3 H = normalize(L + V);
        float NdotH = clamp(dot(N, H), 0.f, 1.f);

        // Diffuse lighting
        float NdotL = max(0.0, dot(N, L));

        vec3 BRDF = BRDF_Evaluate(inputs, V, N, L);
        
        fragcolor += NdotL * BRDF * spotEffect * shadowFactor * ubo.lights[i].color.rgb / (dist * dist);
    }

    outColor = vec4(fragcolor.rgb, opacity);
}