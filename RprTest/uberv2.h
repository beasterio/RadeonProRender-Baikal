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

#include "RprSupport.h"

rprx_material CreateDiffuseMaterial(rprx_context context)
{
    rprx_material material;
    rpr_int status;
    status = rprxCreateMaterial(context, RPRX_MATERIAL_UBER, &material);
    assert(status == RPR_SUCCESS);

    status = rprxMaterialSetParameterF(context, material, RPRX_UBER_MATERIAL_DIFFUSE_COLOR, 1.0f, 0.0f, 0.0f, 0.0f);
    assert(status == RPR_SUCCESS);

    return material;
}

rprx_material CreateTransparentMaterial(rprx_context context)
{
    rprx_material material;
    rpr_int status;
    status = rprxCreateMaterial(context, RPRX_MATERIAL_UBER, &material);
    assert(status == RPR_SUCCESS);

    status = rprxMaterialSetParameterF(context, material, RPRX_UBER_MATERIAL_DIFFUSE_COLOR, 1.0f, 0.0f, 0.0f, 0.0f);
    assert(status == RPR_SUCCESS);
    status = rprxMaterialSetParameterF(context, material, RPRX_UBER_MATERIAL_TRANSPARENCY, 0.8f, 0.8f, 0.8f, 0.8f);
    assert(status == RPR_SUCCESS);

    return material;
}

rprx_material CreateCoatingMaterial(rprx_context context)
{
    rprx_material material;
    rpr_int status;
    status = rprxCreateMaterial(context, RPRX_MATERIAL_UBER, &material);
    assert(status == RPR_SUCCESS);

    status = rprxMaterialSetParameterF(context, material, RPRX_UBER_MATERIAL_DIFFUSE_COLOR, 1.0f, 0.0f, 0.0f, 0.0f);
    assert(status == RPR_SUCCESS);
    status = rprxMaterialSetParameterF(context, material, RPRX_UBER_MATERIAL_COATING_COLOR, 1.0f, 0.0f, 0.0f, 0.0f);
    assert(status == RPR_SUCCESS);
    status = rprxMaterialSetParameterF(context, material, RPRX_UBER_MATERIAL_COATING_IOR, 2.0f, 2.0f, 2.0f, 2.0f);
    assert(status == RPR_SUCCESS);

    return material;
}

rprx_material CreateReflectionMaterial(rprx_context context)
{
    rprx_material material;
    rpr_int status;
    status = rprxCreateMaterial(context, RPRX_MATERIAL_UBER, &material);
    assert(status == RPR_SUCCESS);

    status = rprxMaterialSetParameterF(context, material, RPRX_UBER_MATERIAL_DIFFUSE_COLOR, 1.0f, 0.0f, 0.0f, 0.0f);
    assert(status == RPR_SUCCESS);
    status = rprxMaterialSetParameterF(context, material, RPRX_UBER_MATERIAL_REFLECTION_COLOR, 1.0f, 0.0f, 0.0f, 0.0f);
    assert(status == RPR_SUCCESS);
    status = rprxMaterialSetParameterF(context, material, RPRX_UBER_MATERIAL_REFLECTION_IOR, 2.0f, 2.0f, 2.0f, 2.0f);
    assert(status == RPR_SUCCESS);
    status = rprxMaterialSetParameterF(context, material, RPRX_UBER_MATERIAL_REFLECTION_ROUGHNESS, 0.0f, 0.0f, 0.0f, 0.0f);
    assert(status == RPR_SUCCESS);
    status = rprxMaterialSetParameterF(context, material, RPRX_UBER_MATERIAL_REFLECTION_METALNESS, 0.0f, 0.0f, 0.0f, 0.0f);
    assert(status == RPR_SUCCESS);

    return material;
}

rprx_material CreateRefractionMaterial(rprx_context context)
{
    rprx_material material;
    rpr_int status;
    status = rprxCreateMaterial(context, RPRX_MATERIAL_UBER, &material);
    assert(status == RPR_SUCCESS);

    status = rprxMaterialSetParameterF(context, material, RPRX_UBER_MATERIAL_DIFFUSE_COLOR, 1.0f, 0.0f, 0.0f, 0.0f);
    assert(status == RPR_SUCCESS);
    status = rprxMaterialSetParameterF(context, material, RPRX_UBER_MATERIAL_REFRACTION_COLOR, 1.0f, 1.0f, 1.0f, 1.0f);
    assert(status == RPR_SUCCESS);
    status = rprxMaterialSetParameterF(context, material, RPRX_UBER_MATERIAL_REFRACTION_IOR, 2.0f, 2.0f, 2.0f, 2.0f);
    assert(status == RPR_SUCCESS);
    status = rprxMaterialSetParameterF(context, material, RPRX_UBER_MATERIAL_REFRACTION_ROUGHNESS, 0.0f, 0.0f, 0.0f, 0.0f);
    assert(status == RPR_SUCCESS);

    return material;
}

rprx_material CreateMetalMaterial(rprx_context context)
{
    rprx_material material;
    rpr_int status;
    status = rprxCreateMaterial(context, RPRX_MATERIAL_UBER, &material);
    assert(status == RPR_SUCCESS);


    status = rprxMaterialSetParameterF(context, material, RPRX_UBER_MATERIAL_DIFFUSE_COLOR, 1.0f, 0.0f, 0.0f, 0.0f);
    assert(status == RPR_SUCCESS);
    status = rprxMaterialSetParameterF(context, material, RPRX_UBER_MATERIAL_REFLECTION_COLOR, 1.0f, 0.0f, 0.0f, 0.0f);
    assert(status == RPR_SUCCESS);
    status = rprxMaterialSetParameterF(context, material, RPRX_UBER_MATERIAL_REFLECTION_METALNESS, 1.0f, 1.0f, 1.0f, 1.0f);
    assert(status == RPR_SUCCESS);
    status = rprxMaterialSetParameterF(context, material, RPRX_UBER_MATERIAL_REFLECTION_ROUGHNESS, 0.0f, 0.0f, 0.0f, 0.0f);
    assert(status == RPR_SUCCESS);
    status = rprxMaterialSetParameterF(context, material, RPRX_UBER_MATERIAL_REFLECTION_IOR, 2.0f, 0.0f, 0.0f, 0.0f);
    assert(status == RPR_SUCCESS);

    return material;

}

void UberV2Test()
{
    // Indicates whether the last operation has suceeded or not
    rpr_int status = RPR_SUCCESS;

    rpr_context	context;
    status = rprCreateContext(RPR_API_VERSION, nullptr, 0, RPR_CREATION_FLAGS_ENABLE_GPU0, NULL, NULL, &context);
    assert(status == RPR_SUCCESS);
    rpr_material_system matsys = NULL;
    status = rprContextCreateMaterialSystem(context, 0, &matsys);
    assert(status == RPR_SUCCESS);

    rpr_scene scene = NULL; status = rprContextCreateScene(context, &scene);
    assert(status == RPR_SUCCESS);

    rprx_context xcontext;
    status = rprxCreateContext(matsys, 0, &xcontext);

    //Materials
    std::vector<rprx_material> materials; //order: diffuse, transparent, coating, reflection, refraction, metallic
    materials.push_back(CreateDiffuseMaterial(xcontext));
    materials.push_back(CreateTransparentMaterial(xcontext));
    materials.push_back(CreateCoatingMaterial(xcontext));
    materials.push_back(CreateReflectionMaterial(xcontext));
    materials.push_back(CreateRefractionMaterial(xcontext));
    materials.push_back(CreateMetalMaterial(xcontext));

    rpr_image img;
    status = rprContextCreateImageFromFile(context, "../Resources/Textures/test_albedo1.jpg", &img);
    assert(status == RPR_SUCCESS);
    rpr_material_node tex;
    status = rprMaterialSystemCreateNode(matsys, RPR_MATERIAL_NODE_IMAGE_TEXTURE, &tex);
    assert(status == RPR_SUCCESS);
    status = rprMaterialNodeSetInputN(tex, "data", img);
    assert(status == RPR_SUCCESS);

    status = rprxMaterialSetParameterN(xcontext, materials[0], RPRX_UBER_MATERIAL_DIFFUSE_COLOR, tex);
    assert(status == RPR_SUCCESS);

    //Shapes
    std::vector<rpr_shape> shapes;
    shapes.push_back(CreateSphere(context, 64, 32, 2.f, float3()));
    shapes.push_back(CreateSphere(context, 64, 32, 2.f, float3()));
    shapes.push_back(CreateSphere(context, 64, 32, 2.f, float3()));
    shapes.push_back(CreateSphere(context, 64, 32, 2.f, float3()));
    shapes.push_back(CreateSphere(context, 64, 32, 2.f, float3()));
    shapes.push_back(CreateSphere(context, 64, 32, 2.f, float3()));

    matrix m;
    int id = 0;
    //Place spheres and assign materials
    for (int y = 0; y < 2; ++y)
    {
        for (int x = 0; x < 3; ++x)
        {
            float ypos = (y == 0) ? -2.5f : 2.5f;
            m = translation({ 5.0f * (float)x - 5.0f, ypos, 0.0f });
            status = rprShapeSetMaterial(shapes[id], materials[id]);
            assert(status == RPR_SUCCESS);
            status = rprShapeSetTransform(shapes[id], true, &m.m00);
            assert(status == RPR_SUCCESS);
            status = rprSceneAttachShape(scene, shapes[id]);
            assert(status == RPR_SUCCESS);
            status = rprxMaterialCommit(xcontext, materials[id]);
            assert(status == RPR_SUCCESS);

            ++id;
        }
    }

    rpr_light light = NULL; status = rprContextCreateEnvironmentLight(context, &light);
    assert(status == RPR_SUCCESS);
    rpr_image imageInput = NULL; status = rprContextCreateImageFromFile(context, "../Resources/Textures/studio015.hdr", &imageInput);
    assert(status == RPR_SUCCESS);
    status = rprEnvironmentLightSetImage(light, imageInput);
    assert(status == RPR_SUCCESS);
    status = rprSceneAttachLight(scene, light);
    assert(status == RPR_SUCCESS);

    //camera
    rpr_camera camera = NULL; status = rprContextCreateCamera(context, &camera);
    assert(status == RPR_SUCCESS);
    status = rprCameraLookAt(camera, 0, 0, 10, 0, 0, 0, 0, 1, 0);
    assert(status == RPR_SUCCESS);
    status = rprCameraSetFocalLength(camera, 23.f);
    assert(status == RPR_SUCCESS);
    //status = rprCameraSetFStop(camera, 5.4f);
    assert(status == RPR_SUCCESS);
    status = rprSceneSetCamera(scene, camera);
    assert(status == RPR_SUCCESS);

    status = rprContextSetScene(context, scene);
    assert(status == RPR_SUCCESS);

    //light

    //setup out
    rpr_framebuffer_desc desc;
    desc.fb_width = 900;
    desc.fb_height = 600;

    rpr_framebuffer_format fmt = { 4, RPR_COMPONENT_TYPE_FLOAT32 };
    rpr_framebuffer frame_buffer = NULL; status = rprContextCreateFrameBuffer(context, fmt, &desc, &frame_buffer);
    assert(status == RPR_SUCCESS);
    status = rprContextSetAOV(context, RPR_AOV_COLOR, frame_buffer);
    assert(status == RPR_SUCCESS);
    status = rprFrameBufferClear(frame_buffer);
    assert(status == RPR_SUCCESS);

    for (int i = 0; i < kRenderIterations; ++i)
    {
        status = rprContextRender(context);
        assert(status == RPR_SUCCESS);
    }

    status = rprFrameBufferSaveToFile(frame_buffer, "Output/UberMaterialTest.jpg");
    assert(status == RPR_SUCCESS);


    rpr_render_statistics rs;
    status = rprContextGetInfo(context, RPR_CONTEXT_RENDER_STATISTICS, sizeof(rpr_render_statistics), &rs, NULL);
    assert(status == RPR_SUCCESS);

    status = rprSceneDetachLight(scene, light);
    assert(status == RPR_SUCCESS);
    status = rprObjectDelete(light); light = NULL;
    assert(status == RPR_SUCCESS);

    for (int a = 0; a < shapes.size(); ++a)
    {
        status = rprSceneDetachShape(scene, shapes[a]);
        assert(status == RPR_SUCCESS);
        status = rprObjectDelete(shapes[a]);
        assert(status == RPR_SUCCESS);
    }

    for (int a = 0; a < materials.size(); ++a)
    {
        status = rprObjectDelete(materials[a]);
        assert(status == RPR_SUCCESS);
    }

    status = rprSceneSetCamera(scene, NULL);
    assert(status == RPR_SUCCESS);
    status = rprObjectDelete(scene); scene = NULL;
    assert(status == RPR_SUCCESS);
    status = rprObjectDelete(camera); camera = NULL;
    assert(status == RPR_SUCCESS);
    status = rprObjectDelete(frame_buffer); frame_buffer = NULL;
    assert(status == RPR_SUCCESS);
    status = rprObjectDelete(matsys); matsys = NULL;
    assert(status == RPR_SUCCESS);
    status = rprObjectDelete(context); context = NULL;
    assert(status == RPR_SUCCESS);
}

void UberV2Test_Arithmetics()
{
    // Indicates whether the last operation has suceeded or not
    rpr_int status = RPR_SUCCESS;

    rpr_context	context;
    status = rprCreateContext(RPR_API_VERSION, nullptr, 0, RPR_CREATION_FLAGS_ENABLE_GPU0, NULL, NULL, &context);
    assert(status == RPR_SUCCESS);
    rpr_material_system matsys = NULL;
    status = rprContextCreateMaterialSystem(context, 0, &matsys);
    assert(status == RPR_SUCCESS);
    rprx_context xcontext;
    status = rprxCreateContext(matsys, 0, &xcontext);

    rpr_scene scene = NULL; status = rprContextCreateScene(context, &scene);
    assert(status == RPR_SUCCESS);

    //Materials
    rprx_material material;
    status = rprxCreateMaterial(xcontext, RPRX_MATERIAL_UBER, &material);
    assert(status == RPR_SUCCESS);

    //Generate following input: (test_albedo1.jpg * vec3(1.0, 0.0, 0.0)) + vec3(0.0, 1.0, 0.0)
    rpr_material_node add, mul;
    status = rprMaterialSystemCreateNode(matsys, RPR_MATERIAL_NODE_ARITHMETIC, &add);
    assert(status == RPR_SUCCESS);
    status = rprMaterialSystemCreateNode(matsys, RPR_MATERIAL_NODE_ARITHMETIC, &mul);
    assert(status == RPR_SUCCESS);
    status = rprMaterialNodeSetInputU(add, "op", RPR_MATERIAL_NODE_OP_ADD);
    assert(status == RPR_SUCCESS);
    status = rprMaterialNodeSetInputU(mul, "op", RPR_MATERIAL_NODE_OP_MUL);
    assert(status == RPR_SUCCESS);
    rpr_image inputTextureImage = NULL;
    status = rprContextCreateImageFromFile(context, "../Resources/Textures/test_albedo1.jpg", &inputTextureImage);
    assert(status == RPR_SUCCESS);
    rpr_material_node inputTexture;
    status = rprMaterialSystemCreateNode(matsys, RPR_MATERIAL_NODE_IMAGE_TEXTURE, &inputTexture);
    assert(status == RPR_SUCCESS);
    status = rprMaterialNodeSetInputImageData(inputTexture, "data", inputTextureImage);
    assert(status == RPR_SUCCESS);

    status = rprMaterialNodeSetInputN(mul, "color0", inputTexture);
    assert(status == RPR_SUCCESS);
    status = rprMaterialNodeSetInputF(mul, "color1", 1.0f, 0.0f, 0.0f, 0.0f);
    assert(status == RPR_SUCCESS);
    status = rprMaterialNodeSetInputN(add, "color0", mul);
    assert(status == RPR_SUCCESS);
    status = rprMaterialNodeSetInputF(add, "color1", 0.0f, 1.0f, 0.0f, 0.0f);
    assert(status == RPR_SUCCESS);

    status = rprxMaterialSetParameterN(xcontext, material, RPRX_UBER_MATERIAL_DIFFUSE_COLOR, add);
    assert(status == RPR_SUCCESS);

    //Shapes
    rpr_shape shape(CreateSphere(context, 64, 32, 2.f, float3()));

    status = rprShapeSetMaterial(shape, material);
    assert(status == RPR_SUCCESS);
    status = rprSceneAttachShape(scene, shape);
    assert(status == RPR_SUCCESS);
    status = rprxMaterialCommit(xcontext, material);
    assert(status == RPR_SUCCESS);

    rpr_light light = NULL; status = rprContextCreateEnvironmentLight(context, &light);
    assert(status == RPR_SUCCESS);
    rpr_image imageInput = NULL; status = rprContextCreateImageFromFile(context, "../Resources/Textures/studio015.hdr", &imageInput);
    assert(status == RPR_SUCCESS);
    status = rprEnvironmentLightSetImage(light, imageInput);
    assert(status == RPR_SUCCESS);
    status = rprSceneAttachLight(scene, light);
    assert(status == RPR_SUCCESS);

    //camera
    rpr_camera camera = NULL; status = rprContextCreateCamera(context, &camera);
    assert(status == RPR_SUCCESS);
    status = rprCameraLookAt(camera, 0, 0, 10, 0, 0, 0, 0, 1, 0);
    assert(status == RPR_SUCCESS);
    status = rprCameraSetFocalLength(camera, 23.f);
    assert(status == RPR_SUCCESS);
    //status = rprCameraSetFStop(camera, 5.4f);
    assert(status == RPR_SUCCESS);
    status = rprSceneSetCamera(scene, camera);
    assert(status == RPR_SUCCESS);

    status = rprContextSetScene(context, scene);
    assert(status == RPR_SUCCESS);

    //light

    //setup out
    rpr_framebuffer_desc desc;
    desc.fb_width = 900;
    desc.fb_height = 600;

    rpr_framebuffer_format fmt = { 4, RPR_COMPONENT_TYPE_FLOAT32 };
    rpr_framebuffer frame_buffer = NULL; status = rprContextCreateFrameBuffer(context, fmt, &desc, &frame_buffer);
    assert(status == RPR_SUCCESS);
    status = rprContextSetAOV(context, RPR_AOV_COLOR, frame_buffer);
    assert(status == RPR_SUCCESS);
    status = rprFrameBufferClear(frame_buffer);
    assert(status == RPR_SUCCESS);

    for (int i = 0; i < kRenderIterations; ++i)
    {
        status = rprContextRender(context);
        assert(status == RPR_SUCCESS);
    }

    status = rprFrameBufferSaveToFile(frame_buffer, "Output/RprArithmeticsTest.jpg");
    assert(status == RPR_SUCCESS);


    rpr_render_statistics rs;
    status = rprContextGetInfo(context, RPR_CONTEXT_RENDER_STATISTICS, sizeof(rpr_render_statistics), &rs, NULL);
    assert(status == RPR_SUCCESS);

    status = rprSceneDetachLight(scene, light);
    assert(status == RPR_SUCCESS);
    status = rprObjectDelete(light); light = NULL;
    assert(status == RPR_SUCCESS);

    status = rprSceneDetachShape(scene, shape);
    assert(status == RPR_SUCCESS);
    status = rprObjectDelete(shape);
    assert(status == RPR_SUCCESS);

    status = rprObjectDelete(material);
    assert(status == RPR_SUCCESS);

    status = rprSceneSetCamera(scene, NULL);
    assert(status == RPR_SUCCESS);
    status = rprObjectDelete(scene); scene = NULL;
    assert(status == RPR_SUCCESS);
    status = rprObjectDelete(camera); camera = NULL;
    assert(status == RPR_SUCCESS);
    status = rprObjectDelete(frame_buffer); frame_buffer = NULL;
    assert(status == RPR_SUCCESS);
    status = rprObjectDelete(matsys); matsys = NULL;
    assert(status == RPR_SUCCESS);
    status = rprObjectDelete(context); context = NULL;
    assert(status == RPR_SUCCESS);
}

void UberV2Test_Bump()
{
    // Indicates whether the last operation has suceeded or not
    rpr_int status = RPR_SUCCESS;

    rpr_context	context;
    status = rprCreateContext(RPR_API_VERSION, nullptr, 0, RPR_CREATION_FLAGS_ENABLE_GPU0, NULL, NULL, &context);
    assert(status == RPR_SUCCESS);
    rpr_material_system matsys = NULL;
    status = rprContextCreateMaterialSystem(context, 0, &matsys);
    assert(status == RPR_SUCCESS);
    rprx_context xcontext;
    status = rprxCreateContext(matsys, 0, &xcontext);
    assert(status == RPR_SUCCESS);

    rpr_scene scene = NULL; status = rprContextCreateScene(context, &scene);
    assert(status == RPR_SUCCESS);

    //Materials
    rprx_material material;
    status = rprxCreateMaterial(xcontext, RPRX_MATERIAL_UBER, &material);
    assert(status == RPR_SUCCESS);

    rpr_image bumpImage = NULL;
    status = rprContextCreateImageFromFile(context, "../Resources/Textures/test_bump.jpg", &bumpImage);
    assert(status == RPR_SUCCESS);
    rpr_material_node inputTexture;
    status = rprMaterialSystemCreateNode(matsys, RPR_MATERIAL_NODE_IMAGE_TEXTURE, &inputTexture);
    assert(status == RPR_SUCCESS);
    status = rprMaterialNodeSetInputImageData(inputTexture, "data", bumpImage);
    assert(status == RPR_SUCCESS);

    status = rprxMaterialSetParameterN(xcontext, material, RPRX_UBER_MATERIAL_BUMP, inputTexture);
    assert(status == RPR_SUCCESS);

    //Shapes
    rpr_shape shape(CreateSphere(context, 64, 32, 2.f, float3()));

    status = rprShapeSetMaterial(shape, material);
    assert(status == RPR_SUCCESS);
    status = rprSceneAttachShape(scene, shape);
    assert(status == RPR_SUCCESS);
    status = rprxMaterialCommit(xcontext, material);
    assert(status == RPR_SUCCESS);

    rpr_light light = NULL; status = rprContextCreateEnvironmentLight(context, &light);
    assert(status == RPR_SUCCESS);
    rpr_image imageInput = NULL; status = rprContextCreateImageFromFile(context, "../Resources/Textures/studio015.hdr", &imageInput);
    assert(status == RPR_SUCCESS);
    status = rprEnvironmentLightSetImage(light, imageInput);
    assert(status == RPR_SUCCESS);
    status = rprSceneAttachLight(scene, light);
    assert(status == RPR_SUCCESS);

    //camera
    rpr_camera camera = NULL; status = rprContextCreateCamera(context, &camera);
    assert(status == RPR_SUCCESS);
    status = rprCameraLookAt(camera, 0, 0, 10, 0, 0, 0, 0, 1, 0);
    assert(status == RPR_SUCCESS);
    status = rprCameraSetFocalLength(camera, 23.f);
    assert(status == RPR_SUCCESS);
    //status = rprCameraSetFStop(camera, 5.4f);
    assert(status == RPR_SUCCESS);
    status = rprSceneSetCamera(scene, camera);
    assert(status == RPR_SUCCESS);

    status = rprContextSetScene(context, scene);
    assert(status == RPR_SUCCESS);

    //light

    //setup out
    rpr_framebuffer_desc desc;
    desc.fb_width = 900;
    desc.fb_height = 600;

    rpr_framebuffer_format fmt = { 4, RPR_COMPONENT_TYPE_FLOAT32 };
    rpr_framebuffer frame_buffer = NULL; status = rprContextCreateFrameBuffer(context, fmt, &desc, &frame_buffer);
    assert(status == RPR_SUCCESS);
    status = rprContextSetAOV(context, RPR_AOV_COLOR, frame_buffer);
    assert(status == RPR_SUCCESS);
    status = rprFrameBufferClear(frame_buffer);
    assert(status == RPR_SUCCESS);

    for (int i = 0; i < kRenderIterations; ++i)
    {
        status = rprContextRender(context);
        assert(status == RPR_SUCCESS);
    }

    status = rprFrameBufferSaveToFile(frame_buffer, "Output/RprBumpTest.jpg");
    assert(status == RPR_SUCCESS);


    rpr_render_statistics rs;
    status = rprContextGetInfo(context, RPR_CONTEXT_RENDER_STATISTICS, sizeof(rpr_render_statistics), &rs, NULL);
    assert(status == RPR_SUCCESS);

    status = rprSceneDetachLight(scene, light);
    assert(status == RPR_SUCCESS);
    status = rprObjectDelete(light); light = NULL;
    assert(status == RPR_SUCCESS);

    status = rprSceneDetachShape(scene, shape);
    assert(status == RPR_SUCCESS);
    status = rprObjectDelete(shape);
    assert(status == RPR_SUCCESS);

    status = rprObjectDelete(material);
    assert(status == RPR_SUCCESS);

    status = rprSceneSetCamera(scene, NULL);
    assert(status == RPR_SUCCESS);
    status = rprObjectDelete(scene); scene = NULL;
    assert(status == RPR_SUCCESS);
    status = rprObjectDelete(camera); camera = NULL;
    assert(status == RPR_SUCCESS);
    status = rprObjectDelete(frame_buffer); frame_buffer = NULL;
    assert(status == RPR_SUCCESS);
    status = rprObjectDelete(matsys); matsys = NULL;
    assert(status == RPR_SUCCESS);
    status = rprObjectDelete(context); context = NULL;
    assert(status == RPR_SUCCESS);
}

void UberV2Test_NormalMap()
{
    // Indicates whether the last operation has suceeded or not
    rpr_int status = RPR_SUCCESS;

    rpr_context	context;
    status = rprCreateContext(RPR_API_VERSION, nullptr, 0, RPR_CREATION_FLAGS_ENABLE_GPU0, NULL, NULL, &context);
    assert(status == RPR_SUCCESS);
    rpr_material_system matsys = NULL;
    status = rprContextCreateMaterialSystem(context, 0, &matsys);
    assert(status == RPR_SUCCESS);
    rprx_context xcontext;
    status = rprxCreateContext(matsys, 0, &xcontext);

    rpr_scene scene = NULL; status = rprContextCreateScene(context, &scene);
    assert(status == RPR_SUCCESS);

    //Materials
    rprx_material material;
    status = rprxCreateMaterial(xcontext, RPRX_MATERIAL_UBER, &material);
    assert(status == RPR_SUCCESS);

    rpr_image normalImage = NULL;
    status = rprContextCreateImageFromFile(context, "../Resources/Textures/test_normal.jpg", &normalImage);
    assert(status == RPR_SUCCESS);
    rpr_material_node inputTexture;
    status = rprMaterialSystemCreateNode(matsys, RPR_MATERIAL_NODE_IMAGE_TEXTURE, &inputTexture);
    assert(status == RPR_SUCCESS);
    status = rprMaterialNodeSetInputImageData(inputTexture, "data", normalImage);
    assert(status == RPR_SUCCESS);

    status = rprxMaterialSetParameterN(xcontext, material, RPRX_UBER_MATERIAL_NORMAL, inputTexture);
    assert(status == RPR_SUCCESS);

    //Shapes
    rpr_shape shape(CreateSphere(context, 64, 32, 2.f, float3()));

    status = rprShapeSetMaterial(shape, material);
    assert(status == RPR_SUCCESS);
    status = rprSceneAttachShape(scene, shape);
    assert(status == RPR_SUCCESS);
    status = rprxMaterialCommit(xcontext, material);
    assert(status == RPR_SUCCESS);

    rpr_light light = NULL; status = rprContextCreateEnvironmentLight(context, &light);
    assert(status == RPR_SUCCESS);
    rpr_image imageInput = NULL; status = rprContextCreateImageFromFile(context, "../Resources/Textures/studio015.hdr", &imageInput);
    assert(status == RPR_SUCCESS);
    status = rprEnvironmentLightSetImage(light, imageInput);
    assert(status == RPR_SUCCESS);
    status = rprSceneAttachLight(scene, light);
    assert(status == RPR_SUCCESS);

    //camera
    rpr_camera camera = NULL; status = rprContextCreateCamera(context, &camera);
    assert(status == RPR_SUCCESS);
    status = rprCameraLookAt(camera, 0, 0, 10, 0, 0, 0, 0, 1, 0);
    assert(status == RPR_SUCCESS);
    status = rprCameraSetFocalLength(camera, 23.f);
    assert(status == RPR_SUCCESS);
    //status = rprCameraSetFStop(camera, 5.4f);
    assert(status == RPR_SUCCESS);
    status = rprSceneSetCamera(scene, camera);
    assert(status == RPR_SUCCESS);

    status = rprContextSetScene(context, scene);
    assert(status == RPR_SUCCESS);

    //light

    //setup out
    rpr_framebuffer_desc desc;
    desc.fb_width = 900;
    desc.fb_height = 600;

    rpr_framebuffer_format fmt = { 4, RPR_COMPONENT_TYPE_FLOAT32 };
    rpr_framebuffer frame_buffer = NULL; status = rprContextCreateFrameBuffer(context, fmt, &desc, &frame_buffer);
    assert(status == RPR_SUCCESS);
    status = rprContextSetAOV(context, RPR_AOV_COLOR, frame_buffer);
    assert(status == RPR_SUCCESS);
    status = rprFrameBufferClear(frame_buffer);
    assert(status == RPR_SUCCESS);

    for (int i = 0; i < kRenderIterations; ++i)
    {
        status = rprContextRender(context);
        assert(status == RPR_SUCCESS);
    }

    status = rprFrameBufferSaveToFile(frame_buffer, "Output/RprNormalMapTest.jpg");
    assert(status == RPR_SUCCESS);


    rpr_render_statistics rs;
    status = rprContextGetInfo(context, RPR_CONTEXT_RENDER_STATISTICS, sizeof(rpr_render_statistics), &rs, NULL);
    assert(status == RPR_SUCCESS);

    status = rprSceneDetachLight(scene, light);
    assert(status == RPR_SUCCESS);
    status = rprObjectDelete(light); light = NULL;
    assert(status == RPR_SUCCESS);

    status = rprSceneDetachShape(scene, shape);
    assert(status == RPR_SUCCESS);
    status = rprObjectDelete(shape);
    assert(status == RPR_SUCCESS);

    status = rprObjectDelete(material);
    assert(status == RPR_SUCCESS);

    status = rprSceneSetCamera(scene, NULL);
    assert(status == RPR_SUCCESS);
    status = rprObjectDelete(scene); scene = NULL;
    assert(status == RPR_SUCCESS);
    status = rprObjectDelete(camera); camera = NULL;
    assert(status == RPR_SUCCESS);
    status = rprObjectDelete(frame_buffer); frame_buffer = NULL;
    assert(status == RPR_SUCCESS);
    status = rprObjectDelete(matsys); matsys = NULL;
    assert(status == RPR_SUCCESS);
    status = rprObjectDelete(context); context = NULL;
    assert(status == RPR_SUCCESS);
}
