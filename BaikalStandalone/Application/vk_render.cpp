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
#include "vk_render.h"
#include "SceneGraph/IO/scene_io.h"
#include "SceneGraph/IO/material_io.h"

namespace Baikal
{
    AppVkRender::AppVkRender(AppSettings& settings, GLuint tex)
    {
        InitVk(settings, tex);
        LoadScene(settings);
    }

    void AppVkRender::InitVk(AppSettings& settings, GLuint tex)
    {
        ConfigManager::CreateConfigs(
            settings.mode,
            settings.interop,
            m_cfgs,
            settings.num_bounces,
            settings.platform_index,
            settings.device_index);
    }

    //copy data from to GL
    void AppVkRender::Update(AppSettings& settings)
    {

    }

    //compile scene
    void AppVkRender::UpdateScene()
    {

    }

    //render
    void AppVkRender::Render(int sample_cnt)
    {

    }

    void AppVkRender::StartRenderThreads()
    {

    }

    void AppVkRender::StopRenderThreads()
    {

    }

    void AppVkRender::RunBenchmark(AppSettings& settings)
    {

    }

    void AppVkRender::SetNumBounces(int num_bounces)
    {

    }

    void AppVkRender::SetOutputType(Renderer<ClwScene>::OutputType type)
    {
        m_output_type = type;
    }

    void AppVkRender::SaveFrameBuffer(AppSettings& settings)
    {

    }

    void AppVkRender::SaveImage(const std::string& name, int width, int height, const RadeonRays::float3* data)
    {

    }

    void AppVkRender::LoadScene(AppSettings& settings)
    {
        rand_init();

        // Load obj file
        std::string basepath = settings.path;
        basepath += "/";
        std::string filename = basepath + settings.modelname;

        {
            // Load OBJ scene
            bool is_fbx = filename.find(".fbx") != std::string::npos;
            bool is_gltf = filename.find(".gltf") != std::string::npos;
            std::unique_ptr<Baikal::SceneIo> scene_io;
            if (is_gltf)
                scene_io = Baikal::SceneIo::CreateSceneIoGltf();
            else
                scene_io = is_fbx ? Baikal::SceneIo::CreateSceneIoFbx() : Baikal::SceneIo::CreateSceneIoObj();
            auto scene_io1 = Baikal::SceneIo::CreateSceneIoTest();
            m_scene = scene_io->LoadScene(filename, basepath);

            // Enable this to generate new materal mapping for a model
#if 0
            auto material_io{ Baikal::MaterialIo::CreateMaterialIoXML() };
            material_io->SaveMaterialsFromScene(basepath + "materials.xml", *m_scene);
            material_io->SaveIdentityMapping(basepath + "mapping.xml", *m_scene);
#endif

            // Check it we have material remapping
            std::ifstream in_materials(basepath + "materials.xml");
            std::ifstream in_mapping(basepath + "mapping.xml");

            if (in_materials && in_mapping)
            {
                in_materials.close();
                in_mapping.close();

                auto material_io = Baikal::MaterialIo::CreateMaterialIoXML();
                auto mats = material_io->LoadMaterials(basepath + "materials.xml");
                auto mapping = material_io->LoadMaterialMapping(basepath + "mapping.xml");

                material_io->ReplaceSceneMaterials(*m_scene, *mats, mapping);
            }
        }

        switch (settings.camera_type)
        {
        case CameraType::kPerspective:
            m_camera = Baikal::PerspectiveCamera::Create(
                settings.camera_pos
                , settings.camera_at
                , settings.camera_up);

            break;
        case CameraType::kOrthographic:
            m_camera = Baikal::OrthographicCamera::Create(
                settings.camera_pos
                , settings.camera_at
                , settings.camera_up);
            break;
        default:
            throw std::runtime_error("AppClRender::InitCl(...): unsupported camera type");
        }

        m_scene->SetCamera(m_camera);

        // Adjust sensor size based on current aspect ratio
        float aspect = (float)settings.width / settings.height;
        settings.camera_sensor_size.y = settings.camera_sensor_size.x / aspect;

        m_camera->SetSensorSize(settings.camera_sensor_size);
        m_camera->SetDepthRange(settings.camera_zcap);

        auto perspective_camera = std::dynamic_pointer_cast<Baikal::PerspectiveCamera>(m_camera);

        // if camera mode is kPerspective
        if (perspective_camera)
        {
            perspective_camera->SetFocalLength(settings.camera_focal_length);
            perspective_camera->SetFocusDistance(settings.camera_focus_distance);
            perspective_camera->SetAperture(settings.camera_aperture);
            std::cout << "Camera type: " << (perspective_camera->GetAperture() > 0.f ? "Physical" : "Pinhole") << "\n";
            std::cout << "Lens focal length: " << perspective_camera->GetFocalLength() * 1000.f << "mm\n";
            std::cout << "Lens focus distance: " << perspective_camera->GetFocusDistance() << "m\n";
            std::cout << "F-Stop: " << 1.f / (perspective_camera->GetAperture() * 10.f) << "\n";
        }

        std::cout << "Sensor size: " << settings.camera_sensor_size.x * 1000.f << "x" << settings.camera_sensor_size.y * 1000.f << "mm\n";
    }

} // Baikal
