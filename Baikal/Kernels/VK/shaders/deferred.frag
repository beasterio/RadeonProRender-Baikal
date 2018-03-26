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

struct BRDFInputs
{
    vec3 albedo;
    float roughness;
    float metallic;
    float transparency;
};

struct SH9
{
    float sh[9];
};

struct SH9Color
{
    vec4 coefficients[9];
};

// TODO: texture array or deffered shadows
layout (binding = 5) uniform sampler2D samplerShadowMapLight0;
layout (binding = 6) uniform sampler2D samplerShadowMapLight1;
layout (binding = 7) uniform sampler2D samplerShadowMapLight2;
layout (binding = 8) uniform sampler2D samplerGI;
layout (binding = 9) uniform samplerCube samplerEnv;

layout (binding = 10) uniform sampler2D brdfLUT;
layout (binding = 11) uniform samplerCube irradiance;
layout (binding = 12) uniform samplerCube prefilteredMap;

layout (binding = 13) buffer Sh9ColorBuffer
{
    SH9Color shColor[];
};

layout(push_constant) uniform PushConsts {
    vec4 giAoEnabled;
    vec4 bbox_scene_min;
    vec4 probe_count;
    vec4 probe_dist;
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

vec3 prefilteredReflection(vec3 R, float roughness)
{
	const float MAX_REFLECTION_LOD = 9.0; // todo: param/const
	float lod = roughness * MAX_REFLECTION_LOD;
	float lodf = floor(lod);
	float lodc = ceil(lod);
	vec3 a = textureLod(prefilteredMap, R, lodf).rgb;
	vec3 b = textureLod(prefilteredMap, R, lodc).rgb;
	return mix(a, b, lod - lodf);
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

const float CosineA0 = PI;
const float CosineA1 = (2.0f * PI) / 3.0f;
const float CosineA2 = PI / 4.0f;

SH9 SH_Get2ndOrderCoeffs(vec3 d)
{
    SH9 sh9;

    d = normalize(d);

    float fC0, fC1, fS0, fS1, fTmpA, fTmpB, fTmpC;
    float pz2 = d.z * d.z;

    sh9.sh[0] = 0.2820947917738781f * CosineA0;
    sh9.sh[2] = 0.4886025119029199f * d.z * CosineA1;
    sh9.sh[6] = 0.9461746957575601f * pz2 + -0.3153915652525201f;
    fC0 = d.x;
    fS0 = d.y;
    fTmpA = -0.48860251190292f;
    sh9.sh[3] = fTmpA * fC0 * CosineA1;
    sh9.sh[1] = fTmpA * fS0 * CosineA1;
    fTmpB = -1.092548430592079f * d.z;
    sh9.sh[7] = fTmpB * fC0 * CosineA2;
    sh9.sh[5] = fTmpB * fS0 * CosineA2;
    fC1 = d.x*fC0 - d.y*fS0;
    fS1 = d.x*fS0 + d.y*fC0;
    fTmpC = 0.5462742152960395f;
    sh9.sh[8] = fTmpC * fC1 * CosineA2;
    sh9.sh[4] = fTmpC * fS1 * CosineA2;

    return sh9;
}

vec3 EvaluateSHIrradiance(vec3 dir, SH9Color radiance)
{
    SH9 shBasis = SH_Get2ndOrderCoeffs(dir);

    vec3 irradiance = vec3(0.0f);

    for(int i = 0; i < 9; ++i)
    {
        irradiance += radiance.coefficients[i].xyz * shBasis.sh[i];
    }

    return irradiance;
}

vec3 ConvertIdxToPosition(uint idx) 
{
    ivec3 _probes_count = ivec3(pushConsts.probe_count.xyz);
    vec3 _probe_dist    = vec3(pushConsts.probe_dist.xyz);
    vec3  _scene_min    = pushConsts.bbox_scene_min.xyz;

    uint x = uint(idx % _probes_count.x);
    uint z = uint(idx % (_probes_count.x * _probes_count.z)) / _probes_count.x;
    uint y = uint(idx / (_probes_count.x * _probes_count.z));

    return _scene_min + vec3(x * _probe_dist.x, y * _probe_dist.y, z * _probe_dist.z);
}

uint ConvertPositionToIdx(vec3 p) 
{
    ivec3 _probes_count = ivec3(pushConsts.probe_count.xyz);
    vec3 _probe_dist    = vec3(pushConsts.probe_dist.xyz);
    vec3  _scene_min    = pushConsts.bbox_scene_min.xyz;

    p = p - _scene_min;
    
    int x = int(p.x / _probe_dist.x);
    int y = int(p.y / _probe_dist.y);
    int z = int(p.z / _probe_dist.z);
    
    return min(x + z * _probes_count.x + y * _probes_count.x * _probes_count.z, _probes_count.x * _probes_count.y * _probes_count.z - 1);
}

vec3 GetIrradianceLayer(vec3 N, vec3 p, uint v0, uint v1, uint v2, uint v3) 
{
    vec3 irradiance[4] = {
         EvaluateSHIrradiance(N, shColor[v0]),
         EvaluateSHIrradiance(N, shColor[v1]),
         EvaluateSHIrradiance(N, shColor[v2]),
         EvaluateSHIrradiance(N, shColor[v3])
    };

    vec3 p0 = ConvertIdxToPosition(v0);
    vec3 p1 = ConvertIdxToPosition(v1);
    vec3 p2 = ConvertIdxToPosition(v2);
    vec3 p3 = ConvertIdxToPosition(v3);
    
    float alpha = v1 == v0 ? 1.0f : (p1.x - p.x) / (p1.x - p0.x);
    float beta = v2 == v0 ? 1.0f : (p2.z - p.z) / (p2.z - p0.z);

    vec3 t0 = mix(irradiance[0], irradiance[1], 1.0f - alpha);
    vec3 t1 = mix(irradiance[2], irradiance[3], 1.0f - alpha);

    return mix(t0, t1, 1.0f - beta) / PI;
}


vec3 EvaluateSHIrradiance(vec3 p, vec3 N) 
{
    ivec3 _probes_count = ivec3(pushConsts.probe_count.xyz);
    ivec3 _probe_dist   = ivec3(pushConsts.probe_dist.xyz);
    vec3  _scene_min    = pushConsts.bbox_scene_min.xyz;
    
    uint probes_total = _probes_count.x * _probes_count.y * _probes_count.z;
    uint probes_in_layer = _probes_count.x * _probes_count.z;

    uint v0 = ConvertPositionToIdx(p);
    uint v1 = (v0 + 1) % _probes_count.x < v0 ? v0 + 1 : v0;
    uint v2 = v0 + _probes_count.x > probes_total ? v0 : v0 + _probes_count.x;
    uint v3 = (v2 + 1) % _probes_count.x < v2 ? v2 + 1 : v2;

    uint v4 = (v0 + probes_in_layer > probes_total) ? v0 : v0 + probes_in_layer;
    uint v5 = (v1 + probes_in_layer > probes_total) ? v1 : v1 + probes_in_layer;
    uint v6 = (v2 + probes_in_layer > probes_total) ? v2 : v2 + probes_in_layer;
    uint v7 = (v3 + probes_in_layer > probes_total) ? v3 : v3 + probes_in_layer;

    vec3 p0 = ConvertIdxToPosition(v0);
    vec3 p4 = ConvertIdxToPosition(v4);

    float theta = v0 == v4 ? 1.0f : (p4.y - p.y) / (p4.y - p0.y);

    vec3 layer0 = GetIrradianceLayer(N, p, v0, v1, v2, v3);
    vec3 layer1 = GetIrradianceLayer(N, p, v4, v5, v6, v7);

    return mix(layer0, layer1, 1.0f - theta);
}

void main() 
{
    // Get G-Buffer values and decode
    uvec4 encodedValue = texture(samplerData0, inUV);

    vec3 normal = DecodeNormal(encodedValue.xy);
    vec2 depthAndMeshID = DecodeDepthAndMeshID(encodedValue.zw);

    vec4 albedo = texture(samplerAlbedo, inUV);

    vec4 filteredTraceResults = texture(samplerGI, inUV);

    vec3 gi = pushConsts.giAoEnabled.x > 0 ? filteredTraceResults.xyz : vec3(0.f, 0.f, 0.f);
    float ao = pushConsts.giAoEnabled.y > 0 ? filteredTraceResults.w : 1.0f;

    vec4 data1 = texture(samplerData1, inUV);	

	BRDFInputs inputs;
    inputs.albedo = albedo.xyz;
    inputs.roughness = data1.z;
    inputs.metallic = data1.w;


	float frameCount = ubo.viewPos.w;

    vec3 fragcolor = vec3(0.0f);

    vec3 vPos = ReconstructVSPositionFromDepth(inUV, depthAndMeshID.x);

    const vec3 N = normalize((ubo.invView * vec4(normal, 0.0f)).xyz);
    vec4 wPos = ubo.invView * vec4(vPos, 1.0);
    
    // Viewer to fragment
    vec3 V = normalize(ubo.viewPos.xyz - wPos.xyz);
    
    float NdotV = clamp(dot(N, V), 0.f, 1.f);

    for(int i = 0; i < LIGHT_COUNT; ++i)
    {
        vec4 shadowClip	= ubo.lights[i].viewMatrix * wPos;
        float shadowFactor = filterPCF(i, shadowClip);

        // Vector to light
        vec3 L = ubo.lights[i].position.xyz - wPos.xyz;
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

    float c = frameCount < 64.0 ? (frameCount / 64.0) : 1.0;
    vec3 giWeight = vec3(1.0);//inputs.albedo.xyz;//frameCount < 64.0 ? (frameCount / 64.0) : 1.0;

    fragcolor += (1 - c) * inputs.albedo.xyz * 0.01 + c * giWeight * gi;

    if (depthAndMeshID.x == 0.f)
    {
        const uint width  = uint(ubo.params.x);
        const uint height = uint(ubo.params.y);
        const float fov   = ubo.params.w;

        const float aspectRatio = float(width) / float(height);

        vec2 uv = (inUV.xy * 2.0f - vec2(1.0f)) * fov;
        uv.x = uv.x * aspectRatio;

        vec4 ray_dir = vec4(uv, -1.0f, 0.0f) * ubo.view;
        ray_dir.y = -ray_dir.y;

        fragcolor += texture(samplerEnv, normalize(ray_dir.xyz)).rgb;     
    }
    else
    {
        // IBL255
        vec3 R = normalize(reflect(V, N));
        vec3 irradiance = EvaluateSHIrradiance(wPos.xyz, N);

        vec3 reflection = prefilteredReflection(R, inputs.roughness).rgb;
        vec2 brdf = texture(brdfLUT, vec2(inputs.roughness, NdotV)).rg;

        vec3 specAlbedo = mix( vec3(0.03f), inputs.albedo, inputs.metallic);

        vec3 indirectDiffuse = irradiance * albedo.xyz * (1.0f - inputs.metallic);
        vec3 indirectSpecular = reflection * (specAlbedo * brdf.x + brdf.y);

        fragcolor += (indirectDiffuse + indirectSpecular) * ao;
    }

    outFragColor = fragcolor.xyzz;
} 