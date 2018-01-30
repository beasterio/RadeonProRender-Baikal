#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

struct Texture
{
    int w, h;
    int data_offset;
};

layout(binding = 1) buffer TextureDescriptors 
{
  Texture textures[];
};

layout(binding = 2) buffer TextureData 
{
  uint textureData[];
};

layout (location = 0) in vec2 inUV;
layout (location = 1) in float instanceIdx;

layout (location = 0) out vec4 outFragColor;

layout(push_constant) uniform PushConsts {
  int texture_num;
} pushConsts;

vec4 Fetch_UnpackedTextureData(uint offset)
{
  uint data = textureData[offset];
  uvec4 unpacked_data = uvec4(  data & 0x000000FF, 
                                (data & 0x0000FF00) >> 8, 
                                (data & 0x00FF0000) >> 16, 
                                (data & 0xFF000000) >> 24);

  return vec4(unpacked_data / 255.0f);
}

vec4 Texture_Sample2D(uint textureIdx, vec2 uv)
{
   // Get width and height
   uint width = textures[textureIdx].w;
   uint height = textures[textureIdx].h;
   uint offset = textures[textureIdx].data_offset;

   // Handle UV wrap
   uv -= floor(uv);

   // Reverse Y:
   // it is needed as textures are loaded with Y axis going top to down
   // and our axis goes from down to top
   uv.y = 1.0 - uv.y;

   // Calculate integer coordinates
   uint x0 = clamp(uint(floor(uv.x * width)), 0, width - 1);
   uint y0 = clamp(uint(floor(uv.y * height)), 0, height - 1);

   // Calculate samples for linear filtering
   uint x1 = clamp(x0 + 1, 0,  width - 1);
   uint y1 = clamp(y0 + 1, 0, height - 1);

   // Calculate weights for linear filtering
   float wx = uv.x * width - floor(uv.x * width);
   float wy = uv.y * height - floor(uv.y * height);

   vec4 val00 = Fetch_UnpackedTextureData(offset + width * y0 + x0);
   vec4 val01 = Fetch_UnpackedTextureData(offset + width * y0 + x1);
   vec4 val10 = Fetch_UnpackedTextureData(offset + width * y1 + x0);
   vec4 val11 = Fetch_UnpackedTextureData(offset + width * y1 + x1);
    
   return mix(mix(val00, val01, wx), mix(val10, val11, wx), wy);
}

void main() 
{
  vec4 color;

  if (instanceIdx == 0) 
  {
    color = Texture_Sample2D(3, inUV);
  }

  outFragColor = color;
}