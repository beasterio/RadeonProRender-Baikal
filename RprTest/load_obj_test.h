#include "../Rpr/RadeonProRender.h"
#include "../RadeonRays/RadeonRays/include/math/matrix.h"
#include "../RadeonRays/RadeonRays/include/math/mathutils.h"

#include <cassert>
#include <memory>
#include <map>
#include <iostream>

#include "../Baikal/SceneGraph/scene1.h"
#include "../Baikal/SceneGraph/IO/scene_io.h"

void InitLight(rpr_light light, const RadeonRays::float3& pos, const RadeonRays::float3& at, const RadeonRays::float3& color)
{
    rpr_int status = RPR_SUCCESS;
    RadeonRays::float3 dir = at - pos; dir.normalize();
    RadeonRays::float3 default_dir(0, 0, -1);
    RadeonRays::float3 axis = RadeonRays::cross(default_dir, dir);
    if (axis.sqnorm() < std::numeric_limits<float>::min())
    {
        //rotate around x axis
        axis = RadeonRays::float3(1.f, 0.f, 0.f, 0.f);
    }
    axis.normalize();

    float angle = acos(RadeonRays::dot(dir, default_dir));

    RadeonRays::matrix lightm = translation(pos) * RadeonRays::rotation(axis, angle);
    status = rprLightSetTransform(light, true, &lightm.m00);
    assert(status == RPR_SUCCESS);
    status = rprSpotLightSetRadiantPower3f(light, color.x, color.y, color.z);
    assert(status == RPR_SUCCESS);

}

void TranslateMaterialBaikalToRpr(const std::string& base_path, Baikal::Material::Ptr baikal_mat, rpr_context context, rpr_material_system matsys, rpr_material_node& mat)
{
    rpr_int status = RPR_SUCCESS;
    if (!baikal_mat)
    {
        return;
    }

    Baikal::SingleBxdf* single_mat = dynamic_cast<Baikal::SingleBxdf*>(baikal_mat.get());
    Baikal::MultiBxdf* multi_mat = dynamic_cast<Baikal::MultiBxdf*>(baikal_mat.get());

    if (multi_mat)
    {
        //get albedo material from MultiBxdf material
        single_mat = dynamic_cast<Baikal::SingleBxdf*>(multi_mat->GetInputValue("base_material").mat_value.get());
    }

    auto input = single_mat->GetInputValue("albedo");
    switch (input.type)
    {
        case Baikal::Material::InputType::kFloat4:
            status = rprMaterialNodeSetInputF(mat, "color", input.float_value.x, input.float_value.y, input.float_value.z, 1.0f);
            assert(status == RPR_SUCCESS);
            break;
        case Baikal::Material::InputType::kTexture:
        {
            rpr_image_format format;
            rpr_image_desc desc;
            //collect texture data
            Baikal::Texture::Ptr tex = input.tex_value;

            switch (tex->GetFormat())
            {
            case Baikal::Texture::Format::kRgba8:
                format.type = RPR_COMPONENT_TYPE_UINT8;
                break;
            case Baikal::Texture::Format::kRgba16:
                format.type = RPR_COMPONENT_TYPE_FLOAT16;
                break;
            case Baikal::Texture::Format::kRgba32:
                format.type = RPR_COMPONENT_TYPE_FLOAT32;
                break;
            default:
                std::cout << "Invalid Baikal texture type" << std::endl;
            }
            format.num_components = 4;
            desc = { (rpr_uint)tex->GetSize().x, (rpr_uint)tex->GetSize().y, 0, 0, 0 };
            void const* data = tex->GetData();
            //create image
            std::string tex_filename = base_path + tex->GetName();
            rpr_image img = NULL; status = rprContextCreateImageFromFile(context, tex_filename.c_str(), &img);
            //rpr_image img = NULL; status = rprContextCreateImage(context, format, &desc, data, &img);
            assert(status == RPR_SUCCESS);
            rpr_material_node materialNodeTexture = NULL; status = rprMaterialSystemCreateNode(matsys, RPR_MATERIAL_NODE_IMAGE_TEXTURE, &materialNodeTexture);
            assert(status == RPR_SUCCESS);
            status = rprMaterialNodeSetInputImageData(materialNodeTexture, "data", img);
            assert(status == RPR_SUCCESS);
            status = rprMaterialNodeSetInputN(mat, "color", materialNodeTexture);
            assert(status == RPR_SUCCESS);
            break;
        }
        case Baikal::Material::InputType::kMaterial:
        default:
            std::cout << "Invalid albedo input type\n";
            return;

    }
    assert(status == RPR_SUCCESS);
    //status = rprMaterialNodeSetInputF(diffuse, "color", 0.9f, 0.9f, 0.f, 1.0f);
    //assert(status == RPR_SUCCESS);

}
void LoadObjTest(const std::string& basepath, const std::string& filename)
{
    rpr_int status = RPR_SUCCESS;
    //context, scene and mat. system
    rpr_context	context;
    status = rprCreateContext(RPR_API_VERSION, nullptr, 0, RPR_CREATION_FLAGS_ENABLE_GPU0, NULL, NULL, &context);
    assert(status == RPR_SUCCESS);
    rpr_material_system matsys = NULL;
    status = rprContextCreateMaterialSystem(context, 0, &matsys);
    assert(status == RPR_SUCCESS);
    rpr_scene scene = NULL; status = rprContextCreateScene(context, &scene);
    assert(status == RPR_SUCCESS);
    status = rprContextSetScene(context, scene);
    assert(status == RPR_SUCCESS);
    
    std::unique_ptr<Baikal::SceneIo> scene_io = Baikal::SceneIo::CreateSceneIoObj();
    Baikal::Scene1::Ptr baikal_scene = scene_io->LoadScene(basepath + "/" + filename, basepath + "/");
    std::map<Baikal::Mesh const*, rpr_shape> mesh_map;
    //meshes
    for (auto it = baikal_scene->CreateShapeIterator(); it->IsValid(); it->Next())
    {
        Baikal::Shape::Ptr shape = it->ItemAs<Baikal::Shape>();
        Baikal::Instance const* baikal_inst = dynamic_cast<Baikal::Instance const*>(shape.get());
        Baikal::Mesh const* baikal_mesh = dynamic_cast<Baikal::Mesh const*>(shape.get());

        if (baikal_inst)
        {
            //create instances after all meshes
            continue;
        }
        else
        {
            RadeonRays::float3 const* verts = baikal_mesh->GetVertices();
            RadeonRays::float3 const* norms = baikal_mesh->GetNormals();
            RadeonRays::float2 const* uvs = baikal_mesh->GetUVs();
            std::uint32_t const* inds = baikal_mesh->GetIndices();
            int verts_num = baikal_mesh->GetNumVertices();
            int norms_num = baikal_mesh->GetNumNormals();
            int uvs_num = baikal_mesh->GetNumUVs();
            int inds_num = baikal_mesh->GetNumIndices();

            std::vector<rpr_int> faces(inds_num / 3, 3);

            //create
            rpr_shape mesh = NULL; status = rprContextCreateMesh(context,
                (rpr_float const*)&verts[0].x, verts_num, sizeof(RadeonRays::float3),
                (rpr_float const*)&norms[0].x, norms_num, sizeof(RadeonRays::float3),
                (rpr_float const*)&uvs[0].x, uvs_num, sizeof(RadeonRays::float2),
                (rpr_int const*)inds, sizeof(std::uint32_t),
                (rpr_int const*)inds, sizeof(std::uint32_t),
                (rpr_int const*)inds, sizeof(std::uint32_t),
                faces.data(), inds_num/3, &mesh);

            
            //translate cubes
            RadeonRays::matrix m = baikal_mesh->GetTransform();
            status = rprShapeSetTransform(mesh, true, &m.m00);
            assert(status == RPR_SUCCESS);

            //attach shapes
            status = rprSceneAttachShape(scene, mesh);
            assert(status == RPR_SUCCESS);

            //materials
            Baikal::Material::Ptr baikal_mat = baikal_mesh->GetMaterial();
            
            rpr_material_node diffuse = NULL; status = rprMaterialSystemCreateNode(matsys, RPR_MATERIAL_NODE_DIFFUSE, &diffuse);
            TranslateMaterialBaikalToRpr(basepath + "/", baikal_mat, context, matsys, diffuse);
            status = rprShapeSetMaterial(mesh, diffuse);

            assert(status == RPR_SUCCESS);

            mesh_map[baikal_mesh] = mesh;
        }

    }

    //instances
    for (auto it = baikal_scene->CreateShapeIterator(); it->IsValid(); it->Next())
    {
        Baikal::Shape::Ptr shape = it->ItemAs<Baikal::Shape>();
        Baikal::Instance const* baikal_inst = dynamic_cast<Baikal::Instance const*>(shape.get());

        if (!baikal_inst)
        {
            continue;
        }

        Baikal::Mesh const* baikal_mesh = dynamic_cast<Baikal::Mesh const*>(baikal_inst->GetBaseShape().get());
        rpr_shape base_shape = mesh_map[baikal_mesh];
        rpr_shape inst;
        status = rprContextCreateInstance(context, base_shape, &inst);
        assert(status == RPR_SUCCESS);
        RadeonRays::matrix m = baikal_inst->GetTransform();
        status = rprShapeSetTransform(inst, true, &m.m00);
        assert(status == RPR_SUCCESS);
    }

    //camera
    {
        RadeonRays::float3 eye = { -6.89f, 27.17f, 1.25f };
        RadeonRays::float3 forward = { -1.f, -0.13f, 0.11f };
        RadeonRays::float3 at = forward + eye;
        RadeonRays::float3 up = { 0.f, 1.f, 0.f };
        rpr_camera camera = NULL; status = rprContextCreateCamera(context, &camera);
        assert(status == RPR_SUCCESS);
        status = rprCameraLookAt(camera, eye.x, eye.y, eye.z, at.x, at.y, at.z, up.x, up.y, up.z);
        
        assert(status == RPR_SUCCESS);
        status = rprCameraSetSensorSize(camera, 22.5f, 15.f);
        assert(status == RPR_SUCCESS);
        status = rprCameraSetFocalLength(camera, 35.f);
        assert(status == RPR_SUCCESS);
        status = rprCameraSetFStop(camera, 6.4f);
        assert(status == RPR_SUCCESS);
        status = rprSceneSetCamera(scene, camera);
        assert(status == RPR_SUCCESS);
    }

    //lights
    {
        float height = -35.0f;

        rpr_light light[3] = { NULL, NULL, NULL }; 
        status = rprContextCreateSpotLight(context, &light[0]);
        assert(status == RPR_SUCCESS);
        status = rprContextCreateSpotLight(context, &light[1]);
        assert(status == RPR_SUCCESS);
        status = rprContextCreateSpotLight(context, &light[2]);
        assert(status == RPR_SUCCESS);

        InitLight(light[0], RadeonRays::float3(23.8847504f, -16.0555954f, 5.01268339f),
                            RadeonRays::float3(23.0653629f, -15.9814873f, 4.44425392f),
                            RadeonRays::float3(5000.0f, 5000.0f, 5000.0f));

        InitLight(light[1], RadeonRays::float3(0.0f, height, 0.0f),
                            RadeonRays::float3(-1.0f, height, 0.0f),
                            RadeonRays::float3(3000.0f, 3000.0f, 3000.0f));

        InitLight(light[2], RadeonRays::float3(24.2759151f, -17.7952175f, -4.77304792f),
                            RadeonRays::float3(23.3718319f, -17.5660172f, -4.41235495f),
                            RadeonRays::float3(5000.0f, 5000.0f, 5000.0f));

        status = rprSceneAttachLight(scene, light[0]);
        assert(status == RPR_SUCCESS);
        status = rprSceneAttachLight(scene, light[1]);
        assert(status == RPR_SUCCESS);
        status = rprSceneAttachLight(scene, light[2]);
        assert(status == RPR_SUCCESS);
    }


    //result buffer
    rpr_framebuffer_desc desc;
    desc.fb_width = 800;
    desc.fb_height = 600;
    rpr_framebuffer_format fmt = { 4, RPR_COMPONENT_TYPE_FLOAT32 };
    rpr_framebuffer frame_buffer = NULL; status = rprContextCreateFrameBuffer(context, fmt, &desc, &frame_buffer);
    assert(status == RPR_SUCCESS);
    status = rprContextSetAOV(context, RPR_AOV_COLOR, frame_buffer);
    assert(status == RPR_SUCCESS);
    status = rprFrameBufferClear(frame_buffer);
    assert(status == RPR_SUCCESS);

    //render
    for (int i = 0; i < 100; ++i)
    {
        status = rprContextRender(context);
        assert(status == RPR_SUCCESS);
    }
    status = rprFrameBufferSaveToFile(frame_buffer, "Output/LoadObjTest.jpg");
    assert(status == RPR_SUCCESS);

}