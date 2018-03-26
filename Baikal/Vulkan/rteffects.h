/**********************************************************************
Copyright (c) 2016 Advanced Micro Devices, Inc. All rights reserved.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
********************************************************************/
#pragma once

#include <vulkan/vulkan.h>
#include <stdint.h>

#include "radeonrays.h"

#define RTE_STATIC_LIBRARY 1
#if !RTE_STATIC_LIBRARY
#ifdef WIN32
#ifdef EXPORT_API
#define RTE_API __declspec(dllexport)
#else
#define RTE_API __declspec(dllimport)
#endif
#elif defined(__GNUC__)
#ifdef EXPORT_API
#define RTE_API __attribute__((visibility ("default")))
#else
#define RTE_API
#endif
#endif
#else
#define RTE_API
#endif

// Error codes
#define RTE_SUCCESS 0
#define RTE_ERROR_INVALID_VALUE -1
#define RTE_ERROR_NOT_IMPLEMENTED -2
#define RTE_ERROR_OUT_OF_SYSTEM_MEMORY -3
#define RTE_ERROR_OUT_OF_VIDEO_MEMORY -4

// Invalid index marker
#define RTE_INVALID_ID 0xffffffffu

// Light type
#define RTE_LIGHT_UNKNOWN 0
#define RTE_LIGHT_POINT 1
#define RTE_LIGHT_SPOT 2
#define RTE_LIGHT_DIRECTIONAL 3
#define RTE_LIGHT_IBL 4
#define RTE_LIGHT_AREA 5

// Effects
#define RTE_EFFECT_AO (1 << 0)
#define RTE_EFFECT_DIRECT_LIGHT (1 << 1)
#define RTE_EFFECT_INDIRECT_DIFFUSE (1 << 2)
#define RTE_EFFECT_INDIRECT_GLOSSY (1 << 3)

// Data types
typedef int rte_status;
typedef int rte_init_flags;
typedef struct {}* rte_instance;
typedef int rte_effect;

typedef struct
{
    uint32_t indexOffset = RTE_INVALID_ID;
    uint32_t numTriangles = 0u;
    uint32_t materialIndex = RTE_INVALID_ID;
    uint32_t unused = RTE_INVALID_ID;
} rte_shape;

typedef struct
{
    VkDescriptorBufferInfo camera;
    VkDescriptorBufferInfo shapes;
    VkDescriptorBufferInfo vertices;
    VkDescriptorBufferInfo indices;
    VkDescriptorBufferInfo materials;
    VkDescriptorBufferInfo lights;
    VkDescriptorBufferInfo textures;
    VkDescriptorBufferInfo textureDescs;
    VkDescriptorImageInfo shadowMap0;
    VkDescriptorImageInfo shadowMap1;
    VkDescriptorImageInfo shadowMap2;
    uint32_t numLights;
} rte_scene;

typedef struct
{
    float position[3] = { 0.f, 0.f, 0.f };
    uint32_t type = RTE_LIGHT_UNKNOWN;
    float intensity[4] = { 1.f, 1.f, 1.f };
    float direction[3] = { 0.f, -1.f, 0.f };
    uint32_t areaLightShapeIndex = RTE_INVALID_ID;
    float spotAngles[2] = { 0.7f, 1.4f};
    float iblMutltiplier = 1.f;
    uint32_t iblTexture = RTE_INVALID_ID;
} rte_light;

typedef struct
{
    VkDescriptorImageInfo depthAndNormal;
    VkDescriptorImageInfo albedo;
    VkDescriptorImageInfo pbrInputs;
} rte_gbuffer;

typedef struct
{
    VkDescriptorImageInfo image;
    VkDescriptorImageInfo sampleCounter;
    uint32_t width;
    uint32_t height;
} rte_output;


typedef struct
{
    float albedo[3] = { 0.7f, 0.7f, 0.7f };
    float roughness = 1.f;
    uint32_t rougnessMap = RTE_INVALID_ID;
    float metalness = 0.f;
    uint32_t metalnessMap = RTE_INVALID_ID;
    uint32_t unused = RTE_INVALID_ID;
} rte_pbr_material;


// API functions
#ifdef __cplusplus
extern "C"
{
#endif
    // Initialize and instance of library.
    RTE_API rte_status rteInitInstance(// GPU to run ray queries on
                                       VkDevice device,
                                       //
                                       VkPhysicalDevice physical_device,
                                       // Command pool to allocate command buffers
                                       VkCommandPool command_pool,
                                       // Intersector
                                       rr_instance intersector,
                                       // Enabled effects
                                       rte_effect effects,
                                       // Resulting instance
                                       rte_instance* out_instance);

    // Set scene for
    RTE_API rte_status rteSetScene(// API instance
                                   rte_instance instance,
                                   // Scene
                                   rte_scene* scene);


    // Commit changes for the current scene maintained by an API.
    RTE_API rte_status rteCommit(// API instance
                                 rte_instance instance,
                                 // Effect
                                 rte_effect effect,
                                 // Scene creation command buffer
                                 VkCommandBuffer* out_command_buffer);

    // Commit changes for the current scene maintained by an API.
    RTE_API rte_status rteCalculate(// API instance
                                    rte_instance instance,
                                    // Effect to calculate
                                    rte_effect effect,
                                    // Scene creation command buffer
                                    VkCommandBuffer* out_command_buffers,
                                    // Number of elements in out_command_buffers array
                                    uint32_t command_buffers_count,
                                    // 
                                    uint32_t* command_buffers_required);

    // Bind buffers for the query
    RTE_API rte_status rteSetGBuffer(// API instance
                                      rte_instance instance,
                                      // 
                                      rte_gbuffer* gbuffer);

    // Bind buffers for the query
    RTE_API rte_status rteSetOutput(// API instance
                                    rte_instance instance,
                                    // 
                                    rte_output* output);

    // Shutdown API instance
    RTE_API rte_status rteShutdownInstance(
        // API instance
        rte_instance instance);
#ifdef __cplusplus
}
#endif
