#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

#define PI 3.1415926535897932384626433832795

//layout (binding = 1) uniform usampler2D buffer0;
layout (binding = 1) uniform samplerCube buffer0;
layout (binding = 2) uniform sampler2D buffer1;
layout (binding = 3) uniform sampler2D buffer2;
layout (location = 0) in vec2 inUV;
layout (location = 1) in float instanceIdx;

layout (location = 0) out vec4 outFragColor;

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

void main() 
{
  vec4 color;

  if (instanceIdx == 0) 
  {
    float theta = 2.0f * PI * inUV.x;
    float phi = PI * inUV.y;

    vec3 n = vec3(cos(theta) * sin(phi), sin(theta) * sin(phi), cos(phi));
    n = normalize(n);

    color.xyz = texture(buffer0, n).xyz;
  }
  else if (instanceIdx == 1) 
  {  
    color.xyz = pow(texture(buffer1, inUV).xyz, vec3(1.0 / 2.2));
    color.w = 1.0;
  }
  else if (instanceIdx == 2) 
  {
    color = texture(buffer2, inUV).wwww;
  }

  outFragColor = color;
}