#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout (binding = 1) uniform usampler2D samplerData0;

#define PI 3.14159265358979f
#define LIGHT_COUNT 3

struct Light 
{
    vec4 position;
    vec4 target;
    vec4 color;
    mat4 viewMatrix;
};

layout (binding = 2) uniform UBO 
{
    vec4 viewPos;
    mat4 view;
    mat4 invView;
    mat4 invProj;
    vec4 params;
    Light lights[LIGHT_COUNT];
} ubo;

layout (location = 0) in vec2 inUV;

layout (location = 0) out vec4 outFragColor;

// Decode depth z/w - 24 bits, mesh id - 8 bits
vec2 DecodeDepthAndMeshID(uvec2 data)
{
    float meshID 		= float(data.y & 0xFF);
    uint depthHighBits 	= ((data.y >> 8) & 0xFF) << 16;
    float depth 		= float(depthHighBits | data.x) / 16777215.0f;

    return vec2(depth, meshID);
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

vec2 EdgeDetection(vec2 uv, vec2 rcp_frame)
{
    uvec4 data      = texture(samplerData0, uv);
    uvec4 data_left = texture(samplerData0, uv + vec2(-1, 0) * rcp_frame);
    uvec4 data_top  = texture(samplerData0, uv + vec2(0, -1) * rcp_frame);

    ivec2 edges = ivec2(0, 0);

    float id      = DecodeDepthAndMeshID(data.zw).y;
    float id_left = DecodeDepthAndMeshID(data_left.zw).y;
    float id_top  = DecodeDepthAndMeshID(data_top.zw).y;

    edges.x = id != id_left ? 1 : 0;
    edges.y = id != id_top ? 1 : 0;
    
    vec3 n =  DecodeNormal(data.xy);

    if (length(n) > 0)
    {
        const float threshold  = cos(PI / 8.f);
        
        vec3        n_left  = DecodeNormal(data_left.xy);
        vec3        n_top   = DecodeNormal(data_top.xy);

        vec2 delta = vec2(dot(n, n_left), dot(n, n_top));

        edges.x |= delta.x < threshold ? 1 : 0;
        edges.y |= delta.y < threshold ? 1 : 0;
    }

    return vec2(float(edges.x), float(edges.y));
}

void main() 
{
    vec2 edges = EdgeDetection(inUV, vec2(1.0f) / ubo.params.xy);
    outFragColor = vec4(edges, 0.f, 1.0f);
}