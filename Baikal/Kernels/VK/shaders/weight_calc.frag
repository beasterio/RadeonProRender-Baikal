#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout (binding = 1) uniform sampler2D edgesTex;
layout (binding = 2) uniform sampler2D areaTex;

#define PI 3.14159265358979f
#define LIGHT_COUNT 3
#define MLAA_MAX_SEARCH_STEPS 16
#define MAX_DISTANCE 33

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

vec4 tex2Doffset(sampler2D map, vec2 texcoord, vec2 offset, vec2 rcp_frame) {
	return textureLod(map, texcoord + rcp_frame * offset, 0.0);
}

float SearchXLeft(vec2 texcoord, vec2 rcp_frame) {
	// We compare with 0.9 to prevent bilinear access precision problems.
    texcoord -= vec2(1.5, 0.0) * rcp_frame;
    float e = 0.0;
	int i = 0;
    // We offset by 0.5 to sample between edgels, thus fetching two in a row
    for (i = 0; i < MLAA_MAX_SEARCH_STEPS; i++) {
        //e = pow(textureLod(edgesTex, texcoord, 0).gggg, vec4(1.f / 2.2)).g;
		e = texture(edgesTex, texcoord).g;
        // We compare with 0.9 to prevent bilinear access precision problems
        if (e < 0.9) break;
        texcoord -= vec2(2.0, 0.0) * rcp_frame;
    }
    // When we exit the loop without founding the end, we want to return
    // -2 * maxSearchSteps
    return max(-2.0 * i - 2.0 * e, -2.0 * MLAA_MAX_SEARCH_STEPS);
}

float SearchXRight(vec2 texcoord, vec2 rcp_frame) {
    texcoord += vec2(1.5, 0.0) * rcp_frame;
    float e = 0.0;
	int i = 0;
    for (i = 0; i < MLAA_MAX_SEARCH_STEPS; i++) {
        e = textureLod(edgesTex, texcoord, 0).g;
        if (e < 0.9) break;
        texcoord += vec2(2.0, 0.0) * rcp_frame;
    }
    return min(2.0 * i + 2.0 * e, 2.0 * MLAA_MAX_SEARCH_STEPS);
}

float SearchYDown(vec2 texcoord, vec2 rcp_frame) {
	float i;
	float e = 0.0;
	for (i = -1.5; i > -2.0 * MLAA_MAX_SEARCH_STEPS; i -= 2.0) {
		e = tex2Doffset(edgesTex, texcoord, vec2(i, 0.0).yx, rcp_frame).r;
		if (e < 0.9) break;
	}
	return max(i + 1.5 - 2.0 * e, -2.0 * MLAA_MAX_SEARCH_STEPS);
}

float SearchYUp(vec2 texcoord, vec2 rcp_frame) {
	float i;
	float e = 0.0;
	for (i = 1.5; i < 2.0 * MLAA_MAX_SEARCH_STEPS; i += 2.0) {
		e = tex2Doffset(edgesTex, texcoord, vec2(i, 0.0).yx, rcp_frame).r;
		if (e < 0.9) break;
	}
	return min(i - 1.5 + 2.0 * e, 2.0 * MLAA_MAX_SEARCH_STEPS);
}

vec2 Area(vec2 distance, float e1, float e2) {
	// * By dividing by areaSize - 1.0 below we are implicitely offsetting to
	//   always fall inside of a pixel
	// * Rounding prevents bilinear access precision problems
	float areaSize = MAX_DISTANCE * 5.0;
	vec2 pixcoord = MAX_DISTANCE * round(4.0 * vec2(e1, e2)) + distance;
	vec2 texcoord = pixcoord / (areaSize - 1.0);
	return textureLod(areaTex, texcoord, 0).rg;
}

void main() 
{
    const vec2 rcp_frame = vec2(1.0f) / ubo.params.xy;
    
    vec4 weights = vec4(0.f);
	
    vec2 e = texture(edgesTex, inUV).xy;

    if (e.y != 0.f) { // Edge at north
        // Search distances to the left and to the right:
        vec2 d = vec2(SearchXLeft(inUV, rcp_frame), SearchXRight(inUV, rcp_frame));

        // Now fetch the crossing edges. Instead of sampling between edgels, we
        // sample at -0.25, to be able to discern what value has each edgel:
		
		vec4 coords = vec4(d.x, -0.25f, d.y + 1, -0.25f) *  rcp_frame.xyxy + inUV.xyxy;
        float e1 = texture(edgesTex, coords.xy).x;
        float e2 = texture(edgesTex, coords.zw).x;

        // Ok, we know how this pattern looks like, now it is time for getting
        // the actual area:
		weights.xy = Area(abs(d), e1, e2);
    }

    if (e.x != 0.f) { // Edge at west
        // Search distances to the top and to the bottom:
        vec2 d = vec2(SearchYUp(inUV, rcp_frame), SearchYDown(inUV, rcp_frame));

        // Now fetch the crossing edges (yet again):
		vec4 coords = vec4(-0.25f, d.x, -0.25f, d.y) *  rcp_frame.xyxy + inUV.xyxy;
        float e1 = texture(edgesTex, coords.xy).y;
        float e2 = texture(edgesTex, coords.zw).y;

        // Get the area for this direction:
        weights.zw = Area(abs(d), e1, e2);
    }

    outFragColor = weights;
}