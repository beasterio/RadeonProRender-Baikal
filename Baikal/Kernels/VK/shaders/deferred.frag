#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout (binding = 1) uniform usampler2D samplerData0;
layout (binding = 2) uniform sampler2D samplerAlbedo;
layout (binding = 3) uniform sampler2D samplerData1;

layout (location = 0) in vec2 inUV;

layout (location = 0) out vec4 outFragColor;

#define LIGHT_COUNT 3
#define PI 3.14159265358979f
#define EPS 0.00000001f

struct Light 
{
    vec4 position;
    vec4 target;
    vec4 color;
    mat4 viewMatrix;
};

layout (binding = 4) uniform UBO 
{
    vec4 viewPos;
    mat4 view;
    mat4 invView;
    mat4 invProj;
    vec4 params;
    Light lights[LIGHT_COUNT];
} ubo;

// TODO: texture array or deffered shadows
layout (binding = 5) uniform sampler2D samplerShadowMapLight0;
layout (binding = 6) uniform sampler2D samplerShadowMapLight1;
layout (binding = 7) uniform sampler2D samplerShadowMapLight2;
layout (binding = 8) uniform sampler2D samplerGI;
layout (binding = 9) uniform sampler2D samplerAO;

layout(push_constant) uniform PushConsts {
    int giEnabled;
    int aoEnabled;
} pushConsts;

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

float DTerm_GGX(float roughness, float NdotH)
{
    float roughness2 = roughness * roughness;
    float v = (NdotH * NdotH * (roughness2 - 1) + 1);
    return roughness2 / (PI * v * v);
}


float G1(vec3 n, vec3 v, float k)
{
    const float NdotV = clamp(dot(n, v), 0.f, 1.f);
    return NdotV / (NdotV * (1 - k) + k);
}

float GTerm(float roughness, vec3 n, vec3 v, vec3 l)
{
    const float t = roughness + 1;
    const float k = t * t / 8.f;

    return G1(n, v, k) * G1(n, l, k);
}

float FTerm(float F0, vec3 v, vec3 h)
{
    const float VdotH = clamp(dot(v, h), 0.f, 1.f);
    float p = (-5.55473 * VdotH - 6.98316) * VdotH;

    return F0 + (1 - F0) * pow(2.f, p);
}

// Schlick approximation
vec3 FTerm(vec3 specularColor, vec3 h, vec3 v)
{
    const float VdotH = clamp(dot(v, h), 0.f, 1.f);
    return (specularColor + (1.0f - specularColor) * pow(1.0f - VdotH, 5));
}

// Spheremap Transform
vec3 DecodeNormal(uvec2 enc)
{
    vec2 enc_n = vec2(enc.xy) / 65535.0f;
    enc_n = enc_n * 2.0f - vec2(1.0f);

    vec4 nn = enc_n.xyxx * vec4(2,2,0,0) + vec4(-1,-1, 1,-1);
    float l = abs(dot(nn.xyz,-nn.xyw));
    nn.z = l;
    nn.xy *= sqrt(l);
    return nn.xyz * 2 + vec3(0,0,-1);
}

vec3 ReconstructVSPositionFromDepth(vec2 uv, float depth)
{
    uv = uv * vec2(2.0f, 2.0f) - vec2(1.0f, 1.0f);

    vec4 projPos = vec4(uv, depth, 1.0f);
    vec4 viewPos = ubo.invProj * projPos;

    return viewPos.xyz / viewPos.w;
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

void main() 
{
    // Get G-Buffer values and decode
    uvec4 encodedValue = texture(samplerData0, inUV);

    vec3 normal = DecodeNormal(encodedValue.xy);
    vec2 depthAndMeshID = DecodeDepthAndMeshID(encodedValue.zw);

    vec4 albedo = texture(samplerAlbedo, inUV);
    vec4 rawGI = pushConsts.giEnabled > 0 ? texture(samplerGI, inUV) : vec4(0.f, 0.f, 0.f, 1.f);
    vec4 rawAO = pushConsts.aoEnabled > 0 ? texture(samplerAO, inUV) : vec4(1.f, 1.f, 1.f, 1.f);
    
    vec3 gi = rawGI.xyz / max(rawGI.w, 1.0f);
    float ao = rawAO.x / max(rawAO.w, 1.0f);

    vec4 data1 = texture(samplerData1, inUV);	

    float roughness = data1.z;
    float metallic = data1.w;

    // 0.03 - default specular value for dielectric.
    vec3 realSpecularColor = mix( vec3(0.03f), albedo.rgb, metallic);
    vec3 realAlbedo = clamp( albedo.rgb - albedo.rgb * metallic, vec3(0.f), vec3(1.0f) );
	
	float frameCount = ubo.viewPos.w;

    vec3 fragcolor = vec3(0.0f);

    vec3 vPos = ReconstructVSPositionFromDepth(inUV, depthAndMeshID.x);

    vec3 N = normalize((ubo.invView * vec4(normal, 0.0f)).xyz);
    vec4 wPos = ubo.invView * vec4(vPos, 1.0);

    for(int i = 0; i < LIGHT_COUNT; ++i)
    {
        vec4 shadowClip	= ubo.lights[i].viewMatrix * wPos;
        float shadowFactor = filterPCF(i, shadowClip);

        // Vector to light
        vec3 L = ubo.lights[i].position.xyz - wPos.xyz;
        // Distance from light to fragment position
        float dist = length(L);
        L = normalize(L);

        // Viewer to fragment
        vec3 V = ubo.viewPos.xyz - wPos.xyz;
        V = normalize(V);

        float lightCosInnerAngle = cos(radians(15.0));
        float lightCosOuterAngle = cos(radians(25.0));

        // Direction vector from source to target
        vec3 dir = normalize(ubo.lights[i].position.xyz - ubo.lights[i].target.xyz);

        // Dual cone spot light with smooth transition between inner and outer angle
        float cosDir = dot(L, dir);
        float spotEffect = smoothstep(lightCosOuterAngle, lightCosInnerAngle, cosDir);

        vec3 H = normalize(L + V);
        float NdotV = clamp(dot(N, V), 0.f, 1.f);
        float NdotH = clamp(dot(N, H), 0.f, 1.f);

        // Diffuse lighting
        float NdotL = max(0.0, dot(N, L));
        vec3 diff = vec3(NdotL);

        // Specular lighting
        float 	Dterm = DTerm_GGX(roughness, NdotH);
        float 	Gterm = GTerm(roughness, N, V, L);
        vec3	Fterm = FTerm(realSpecularColor, H, V);
        vec3 	spec = ( Dterm * Gterm * Fterm ) / (4.0f * NdotL * NdotV + EPS);
        
        diff	= diff * realAlbedo;
        
        fragcolor += vec3((diff + spec) * spotEffect * shadowFactor) * ubo.lights[i].color.rgb / (dist * dist);
    }    	

    float giWeight = 1.0;//frameCount < 64.0 ? (frameCount / 64.0) : 1.0;
    fragcolor += giWeight * ao * (gi + realAlbedo * 0.05);
    
    outFragColor = vec4(pow(fragcolor.xyzz, vec4(1.0f / 2.2f)).xyz, 1.0f);
} 