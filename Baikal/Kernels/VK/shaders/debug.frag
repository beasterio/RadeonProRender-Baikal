#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout (binding = 1) uniform usampler2D buffer0;
layout (binding = 2) uniform sampler2D buffer1;
layout (binding = 3) uniform sampler2D buffer2;
layout (location = 0) in vec2 inUV;
layout (location = 1) in float instanceIdx;

layout (location = 0) out vec4 outFragColor;

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

void main() 
{
  vec4 color;

  if (instanceIdx == 0) 
  {
    uvec4 data = texture(buffer0, inUV);
    
    color.xyz = DecodeNormal(data.xy);   
  }
  else if (instanceIdx == 1) 
  {  
    color = texture(buffer1, inUV);
    color.xyz = (color.xyz / color.w) * 10.0f;
  }
  else if (instanceIdx == 2) 
  {
    color = texture(buffer2, inUV);
    color.xyz = (color.xyz / color.w);
  }

  outFragColor = color;
}