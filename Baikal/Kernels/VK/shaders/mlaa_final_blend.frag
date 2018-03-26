#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout (binding = 1) uniform sampler2D blendTex;
layout (binding = 2) uniform sampler2D colorTex;

#define PI 3.14159265358979f
#define LIGHT_COUNT 3

struct Light 
{
    vec4 position;
    vec4 target;
    vec4 color;
    mat4 viewMatrix;
};

layout (binding = 3) uniform UBO 
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

float ConvertToSRGB(float v)
{
	if( v <= 0.0031308 )
		v *= 12.92;
	else
		v = 1.055 * pow(v, 1.0/2.4) - 0.055;
	return v;
}

vec4 ConvertToSRGB(vec4 v)
{
    return vec4(ConvertToSRGB(v.x), ConvertToSRGB(v.y), ConvertToSRGB(v.z), ConvertToSRGB(v.w));
}

void main() 
{
    vec2 rcp_frame = vec2(1.0f) / ubo.params.xy;
    
    vec4 topLeft    = texture(blendTex, inUV);
	float bottom    = texture(blendTex, inUV + vec2(0, 1.0f) * rcp_frame).y;
	float right     = texture(blendTex, inUV + vec2(1.0f, 0) * rcp_frame).w;

    vec4 a          = vec4(topLeft.r, bottom, topLeft.b, right);

    // There is some blending weight with a value greater than 0.0?
    //float sum = dot(topLeft, vec4(1.0));
    float sum = dot(a, vec4(1.0));

	if (sum < 1e-5) {
        outFragColor = ConvertToSRGB(texture(colorTex, inUV));
        return;
    }

    vec4 color = vec4(0.0);
    
    vec4 coords = vec4( 0.0, a.x, 0.0,  +a.y) * rcp_frame.yyyy + inUV.xyxy;
    color += texture(colorTex, coords.xy) * a.x;
    color += texture(colorTex, coords.zw) * a.y;

    coords = vec4(-a.z,  0.0, +a.w,  0.0) * rcp_frame.xxxx + inUV.xyxy;
    color += texture(colorTex, coords.xy) * a.z;
    color += texture(colorTex, coords.zw) * a.w;
    
    color = color / sum;

    outFragColor = ConvertToSRGB(color);
}