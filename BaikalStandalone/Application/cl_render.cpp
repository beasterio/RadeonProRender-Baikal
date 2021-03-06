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
#include "OpenImageIO/imageio.h"

#include "Application/cl_render.h"
#include "Application/gl_render.h"

#include "SceneGraph/scene1.h"
#include "SceneGraph/camera.h"
#include "SceneGraph/material.h"
#include "SceneGraph/IO/scene_io.h"
#include "SceneGraph/IO/image_io.h"

#include "SceneGraph/IO/material_io.h"
#include "SceneGraph/material.h"

#include "Renderers/monte_carlo_renderer.h"
#include "Renderers/adaptive_renderer.h"

#include <fstream>
#include <sstream>
#include <ostream>
#include <thread>
#include <chrono>
#include <cmath>

#include "PostEffects/wavelet_denoiser.h"
#include "Utils/clw_class.h"



namespace Baikal
{
    AppClRender::AppClRender(AppSettings& settings, GLuint tex) : m_tex(tex), m_output_type(Renderer::OutputType::kColor)
    {
        InitCl(settings, m_tex);
        LoadScene(settings);
    }

    void AppClRender::InitCl(AppSettings& settings, GLuint tex)
    {
        bool force_disable_itnerop = false;
        //create cl context
        try
        {
            ConfigManager::CreateConfigs(settings.mode, settings.interop, m_cfgs, settings.num_bounces);
        }
        catch (CLWException &)
        {
            force_disable_itnerop = true;
            ConfigManager::CreateConfigs(settings.mode, false, m_cfgs, settings.num_bounces);
        }


        std::cout << "Running on devices: \n";

        for (int i = 0; i < m_cfgs.size(); ++i)
        {
            std::cout << i << ": " << m_cfgs[i].context.GetDevice(0).GetName() << "\n";
        }

        settings.interop = false;

        m_outputs.resize(m_cfgs.size());
        m_ctrl.reset(new ControlData[m_cfgs.size()]);

        for (int i = 0; i < m_cfgs.size(); ++i)
        {
            if (m_cfgs[i].type == ConfigManager::kPrimary)
            {
                m_primary = i;

                if (m_cfgs[i].caninterop)
                {
                    m_cl_interop_image = m_cfgs[i].context.CreateImage2DFromGLTexture(tex);
                    settings.interop = true;
                }
            }

            m_ctrl[i].clear.store(1);
            m_ctrl[i].stop.store(0);
            m_ctrl[i].newdata.store(0);
            m_ctrl[i].idx = i;
        }

        if (force_disable_itnerop)
        {
            std::cout << "OpenGL interop is not supported, disabled, -interop flag is ignored\n";
        }
        else
        {
            if (settings.interop)
            {
                std::cout << "OpenGL interop mode enabled\n";
            }
            else
            {
                std::cout << "OpenGL interop mode disabled\n";
            }
        }

        //create renderer
#pragma omp parallel for
        for (int i = 0; i < m_cfgs.size(); ++i)
        {
            m_outputs[i].output = m_cfgs[i].factory->CreateOutput(settings.width, settings.height);

#ifdef ENABLE_DENOISER
            m_outputs[i].output_denoised = m_cfgs[i].factory->CreateOutput(settings.width, settings.height);
            m_outputs[i].output_normal = m_cfgs[i].factory->CreateOutput(settings.width, settings.height);
            m_outputs[i].output_position = m_cfgs[i].factory->CreateOutput(settings.width, settings.height);
            m_outputs[i].output_albedo = m_cfgs[i].factory->CreateOutput(settings.width, settings.height);	
            //m_outputs[i].denoiser = m_cfgs[i].factory->CreatePostEffect(Baikal::RenderFactory<Baikal::ClwScene>::PostEffectType::kBilateralDenoiser);
            m_outputs[i].denoiser = m_cfgs[i].factory->CreatePostEffect(Baikal::RenderFactory<Baikal::ClwScene>::PostEffectType::kWaveletDenoiser);
#endif
            m_cfgs[i].renderer->SetOutput(Baikal::Renderer::OutputType::kColor, m_outputs[i].output.get());

#ifdef ENABLE_DENOISER
            m_cfgs[i].renderer->SetOutput(Baikal::Renderer::OutputType::kWorldShadingNormal, m_outputs[i].output_normal.get());
            m_cfgs[i].renderer->SetOutput(Baikal::Renderer::OutputType::kWorldPosition, m_outputs[i].output_position.get());
            m_cfgs[i].renderer->SetOutput(Baikal::Renderer::OutputType::kAlbedo, m_outputs[i].output_albedo.get());
#endif

            m_outputs[i].fdata.resize(settings.width * settings.height);
            m_outputs[i].udata.resize(settings.width * settings.height * 4);

            if (m_cfgs[i].type == ConfigManager::kPrimary)
            {
                m_outputs[i].copybuffer = m_cfgs[i].context.CreateBuffer<RadeonRays::float3>(settings.width * settings.height, CL_MEM_READ_WRITE);
            }
        }

        m_cfgs[m_primary].renderer->Clear(RadeonRays::float3(0, 0, 0), *m_outputs[m_primary].output);
    }


    void AppClRender::LoadScene(AppSettings& settings)
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

            // Enable this to generate new material mapping for a model
#if 0
            auto material_io{Baikal::MaterialIo::CreateMaterialIoXML()};
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

        m_camera = Baikal::PerspectiveCamera::Create(
            settings.camera_pos
            , settings.camera_at
            , settings.camera_up);

        m_scene->SetCamera(m_camera);

        // Adjust sensor size based on current aspect ratio
        float aspect = (float)settings.width / settings.height;
        settings.camera_sensor_size.y = settings.camera_sensor_size.x / aspect;

        m_camera->SetSensorSize(settings.camera_sensor_size);
        m_camera->SetDepthRange(settings.camera_zcap);
        m_camera->SetFocalLength(settings.camera_focal_length);
        m_camera->SetFocusDistance(settings.camera_focus_distance);
        m_camera->SetAperture(settings.camera_aperture);

        std::cout << "Camera type: " << (m_camera->GetAperture() > 0.f ? "Physical" : "Pinhole") << "\n";
        std::cout << "Lens focal length: " << m_camera->GetFocalLength() * 1000.f << "mm\n";
        std::cout << "Lens focus distance: " << m_camera->GetFocusDistance() << "m\n";
        std::cout << "F-Stop: " << 1.f / (m_camera->GetAperture() * 10.f) << "\n";
        std::cout << "Sensor size: " << settings.camera_sensor_size.x * 1000.f << "x" << settings.camera_sensor_size.y * 1000.f << "mm\n";

        //load lights set
        LoadLightSet(settings);

        if (settings.light_set.empty())
        {
            // TODO: temporary code, add IBL
            std::string env_tex_name = "../Resources/Textures/studio015.hdr";
            auto image_io(ImageIo::CreateImageIo());
            auto ibl_texture = image_io->LoadImage(env_tex_name);
            ibl_texture->SetName(env_tex_name);
            auto ibl = ImageBasedLight::Create();
            ibl->SetTexture(ibl_texture);
            ibl->SetMultiplier(1.f);

            m_scene->AttachLight(ibl);
        }
    }

    void AppClRender::LoadLightSet(AppSettings& settings)
    {
        if (!settings.light_set.empty())
        {
            std::ifstream fs(settings.light_set);
            if (!fs)
            {
                throw std::runtime_error("Failed to open lights set file." + settings.light_set);
            }
            std::string line;
            Light::Ptr new_light;
            while (std::getline(fs, line))
            {
                //lights are separated by empty lines
                if (line.empty())
                {
                    continue;
                }

                std::istringstream iss(line);
                std::string val_name;
                iss >> val_name;

                if (val_name == "newlight")
                {
                    std::string type;
                    iss >> type;
                    if (type == "point")
                    {
                        new_light = PointLight::Create();
                    }
                    else if (type == "direct")
                    {
                        new_light = DirectionalLight::Create();
                    }
                    else if (type == "spot")
                    {
                        new_light = SpotLight::Create();
                    }
                    else if (type == "ibl")
                    {
                        new_light = ImageBasedLight::Create();
                    }
                    else
                    {
                        throw std::runtime_error("Invalid light type " + type);
                    }
                    m_scene->AttachLight(new_light);
                }
                else if (val_name == "p")
                {
                    float3 pos;
                    iss >> pos.x >> pos.y >> pos.z;
                    new_light->SetPosition(pos);
                }
                else if (val_name == "d")
                {
                    float3 dir;
                    iss >> dir.x >> dir.y >> dir.z;
                    new_light->SetDirection(dir);
                }
                else if (val_name == "r")
                {
                    float3 r;
                    iss >> r.x >> r.y >> r.z;
                    new_light->SetEmittedRadiance(r);
                }
                else if (val_name == "cs")
                {
                    float2 cs;
                    iss >> cs.x >> cs.y;
                    //this option available only for spot light
                    SpotLight::Ptr spot = std::dynamic_pointer_cast<SpotLight>(new_light);
                    spot->SetConeShape(cs);
                }
                else if (val_name == "tex")
                {
                    std::string tex_name;
                    //texture name can contain spaces
                    getline(iss, tex_name);
                    //remove ' ' from string
                    tex_name.erase(0, 1);
                    //this option available only for ibl
                    ImageBasedLight::Ptr ibl = std::dynamic_pointer_cast<ImageBasedLight>(new_light);
                    auto image_io(ImageIo::CreateImageIo());
                    Texture::Ptr tex = image_io->LoadImage(tex_name.c_str());
                    ibl->SetTexture(tex);
                }
                else if (val_name == "mul")
                {
                    float mul;
                    iss >> mul;
                    //this option available only for ibl
                    ImageBasedLight::Ptr ibl = std::dynamic_pointer_cast<ImageBasedLight>(new_light);
                    ibl->SetMultiplier(mul);
                }
            }

        }
    }


    void AppClRender::UpdateScene()
    {
        for (int i = 0; i < m_cfgs.size(); ++i)
        {
            if (i == m_primary)
            {
                m_cfgs[i].controller->CompileScene(m_scene);
                m_cfgs[i].renderer->Clear(float3(0, 0, 0), *m_outputs[i].output);
                for (auto& aov_ptr : m_outputs[i].aovs)
                {
                    m_cfgs[i].renderer->Clear(float3(0, 0, 0), *aov_ptr.get());
                }

#ifdef ENABLE_DENOISER
                m_cfgs[i].renderer->Clear(float3(0, 0, 0), *m_outputs[i].output_normal);
                m_cfgs[i].renderer->Clear(float3(0, 0, 0), *m_outputs[i].output_position);
                m_cfgs[i].renderer->Clear(float3(0, 0, 0), *m_outputs[i].output_albedo);
#endif

            }
            else
                m_ctrl[i].clear.store(true);
        }
    }

    void AppClRender::Update(AppSettings& settings)
    {
        //if (std::chrono::duration_cast<std::chrono::seconds>(time - updatetime).count() > 1)
        //{
        for (int i = 0; i < m_cfgs.size(); ++i)
        {
            if (m_cfgs[i].type == ConfigManager::kPrimary)
                continue;

            int desired = 1;
            if (std::atomic_compare_exchange_strong(&m_ctrl[i].newdata, &desired, 0))
            {
                {
                    m_cfgs[m_primary].context.WriteBuffer(0, m_outputs[m_primary].copybuffer, &m_outputs[i].fdata[0], settings.width * settings.height);
                }

                auto acckernel = static_cast<Baikal::MonteCarloRenderer*>(m_cfgs[m_primary].renderer.get())->GetAccumulateKernel();

                int argc = 0;
                acckernel.SetArg(argc++, m_outputs[m_primary].copybuffer);
                acckernel.SetArg(argc++, settings.width * settings.width);
                acckernel.SetArg(argc++, static_cast<Baikal::ClwOutput*>(m_outputs[m_primary].output.get())->data());

                int globalsize = settings.width * settings.height;
                m_cfgs[m_primary].context.Launch1D(0, ((globalsize + 63) / 64) * 64, 64, acckernel);
            }
        }

        //updatetime = time;
        //}

        if (!settings.interop)
        {
#ifdef ENABLE_DENOISER
            m_outputs[m_primary].output_denoised->GetData(&m_outputs[m_primary].fdata[0]);
#else
            m_outputs[m_primary].output->GetData(&m_outputs[m_primary].fdata[0]);
#endif

            float gamma = 2.2f;
            for (int i = 0; i < (int)m_outputs[m_primary].fdata.size(); ++i)
            {
                m_outputs[m_primary].udata[4 * i] = (unsigned char)clamp(clamp(pow(m_outputs[m_primary].fdata[i].x / m_outputs[m_primary].fdata[i].w, 1.f / gamma), 0.f, 1.f) * 255, 0, 255);
                m_outputs[m_primary].udata[4 * i + 1] = (unsigned char)clamp(clamp(pow(m_outputs[m_primary].fdata[i].y / m_outputs[m_primary].fdata[i].w, 1.f / gamma), 0.f, 1.f) * 255, 0, 255);
                m_outputs[m_primary].udata[4 * i + 2] = (unsigned char)clamp(clamp(pow(m_outputs[m_primary].fdata[i].z / m_outputs[m_primary].fdata[i].w, 1.f / gamma), 0.f, 1.f) * 255, 0, 255);
                m_outputs[m_primary].udata[4 * i + 3] = 1;
            }

            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, m_tex);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, m_outputs[m_primary].output->width(), m_outputs[m_primary].output->height(), GL_RGBA, GL_UNSIGNED_BYTE, &m_outputs[m_primary].udata[0]);
            glBindTexture(GL_TEXTURE_2D, 0);
        }
        else
        {
            std::vector<cl_mem> objects;
            objects.push_back(m_cl_interop_image);
            m_cfgs[m_primary].context.AcquireGLObjects(0, objects);

            auto copykernel = static_cast<Baikal::MonteCarloRenderer*>(m_cfgs[m_primary].renderer.get())->GetCopyKernel();

#ifdef ENABLE_DENOISER
            auto output = m_outputs[m_primary].output_denoised.get();
#else
            auto output = m_outputs[m_primary].output.get();
#endif

            int argc = 0;

            copykernel.SetArg(argc++, static_cast<Baikal::ClwOutput*>(output)->data());
            copykernel.SetArg(argc++, output->width());
            copykernel.SetArg(argc++, output->height());
            copykernel.SetArg(argc++, 2.2f);
            copykernel.SetArg(argc++, m_cl_interop_image);

            int globalsize = output->width() * output->height();
            m_cfgs[m_primary].context.Launch1D(0, ((globalsize + 63) / 64) * 64, 64, copykernel);

            m_cfgs[m_primary].context.ReleaseGLObjects(0, objects);
            m_cfgs[m_primary].context.Finish(0);
        }


        if (settings.benchmark)
        {
            auto& scene = m_cfgs[m_primary].controller->CompileScene(m_scene);
            static_cast<Baikal::MonteCarloRenderer*>(m_cfgs[m_primary].renderer.get())->Benchmark(scene, settings.stats);

            settings.benchmark = false;
            settings.rt_benchmarked = true;
        }

        //ClwClass::Update();
    }

    void AppClRender::Render(int sample_cnt)
    {
#ifdef ENABLE_DENOISER
        WaveletDenoiser* wavelet_denoiser = dynamic_cast<WaveletDenoiser*>(m_outputs[m_primary].denoiser.get());

        if (wavelet_denoiser != nullptr)
        {
            wavelet_denoiser->Update(m_camera.get());
        }
#endif
        auto& scene = m_cfgs[m_primary].controller->GetCachedScene(m_scene);
        m_cfgs[m_primary].renderer->Render(scene);

#ifdef ENABLE_DENOISER
        Baikal::PostEffect::InputSet input_set;
        input_set[Baikal::Renderer::OutputType::kColor] = m_outputs[m_primary].output.get();
        input_set[Baikal::Renderer::OutputType::kWorldShadingNormal] = m_outputs[m_primary].output_normal.get();
        input_set[Baikal::Renderer::OutputType::kWorldPosition] = m_outputs[m_primary].output_position.get();
        input_set[Baikal::Renderer::OutputType::kAlbedo] = m_outputs[m_primary].output_albedo.get();
        
        auto radius = 10U - RadeonRays::clamp((sample_cnt / 16), 1U, 9U);
        auto position_sensitivity = 5.f + 10.f * (radius / 10.f);

        const bool is_bilateral_denoiser = dynamic_cast<BilateralDenoiser*>(m_outputs[m_primary].denoiser.get()) != nullptr;

        if (is_bilateral_denoiser)
        {

            auto normal_sensitivity = 0.1f + (radius / 10.f) * 0.15f;
            auto color_sensitivity = (radius / 10.f) * 2.f;
            auto albedo_sensitivity = 0.5f + (radius / 10.f) * 0.5f;
            m_outputs[m_primary].denoiser->SetParameter("radius", radius);
            m_outputs[m_primary].denoiser->SetParameter("color_sensitivity", color_sensitivity);
            m_outputs[m_primary].denoiser->SetParameter("normal_sensitivity", normal_sensitivity);
            m_outputs[m_primary].denoiser->SetParameter("position_sensitivity", position_sensitivity);
            m_outputs[m_primary].denoiser->SetParameter("albedo_sensitivity", albedo_sensitivity);
        }

        m_outputs[m_primary].denoiser->Apply(input_set, *m_outputs[m_primary].output_denoised);
#endif
    }

    void AppClRender::SaveFrameBuffer(AppSettings& settings)
    {
        std::stringstream oss;
        auto camera_position = m_camera->GetPosition();
        auto camera_direction = m_camera->GetForwardVector();
        oss << "../Output/" << settings.modelname << "_p" << camera_position.x << camera_position.y << camera_position.z <<
            "_d" << camera_direction.x << camera_direction.y << camera_direction.z <<
            "_s" << settings.num_samples << ".exr";

        SaveFrameBuffer(GetOutputType(), settings, oss.str(), 16);
    }

    void AppClRender::SaveFrameBuffer(Renderer::OutputType type, AppSettings& settings, const std::string& filename, int bpp = 32)
    {
        std::vector<RadeonRays::float3> data;

        //read cl output in case of interop
        std::vector<RadeonRays::float3> output_data;
        {
            auto output = m_cfgs[m_primary].renderer->GetOutput(type);
            assert(output);
            auto buffer = static_cast<Baikal::ClwOutput*>(output)->data();
            output_data.resize(buffer.GetElementCount());
            m_cfgs[m_primary].context.ReadBuffer(0, static_cast<Baikal::ClwOutput*>(output)->data(), &output_data[0], output_data.size()).Wait();
        }


        data.resize(output_data.size());
        memcpy(data.data(), output_data.data(), sizeof(RadeonRays::float3) * output_data.size());
        //std::transform(fdata.cbegin(), fdata.cend(), data.begin(),
        //    [](RadeonRays::float3 const& v)
        //{
        //    float invw = 1.f / v.w;
        //    return v * invw;
        //});
        SaveImage(filename, settings.width, settings.height, bpp, data.data());

    }


    void AppClRender::SaveImage(const std::string& name, int width, int height, int bpp, const RadeonRays::float3* data)
    {
        OIIO_NAMESPACE_USING;

        TypeDesc fmt;
        switch (bpp)
        {
        case 8:
            fmt = TypeDesc::UINT8;
            break;
        case 16:
            fmt = TypeDesc::UINT16;
            break;
        case 32:
            fmt = TypeDesc::FLOAT;
            break;
        default:
            throw std::runtime_error("Unhandled bpp of image.");
        }


        std::vector<float3> tempbuf(width * height);
        tempbuf.assign(data, data + width * height);

        int nan_num = 0;
        for (auto y = 0; y < height; ++y)
            for (auto x = 0; x < width; ++x)
            {
                float3 val = data[(height - 1 - y) * width + x];
                if (std::isnan(val.x) ||
                    std::isnan(val.y) ||
                    std::isnan(val.z) ||
                    std::isnan(val.w))
                {
                    nan_num++;
                    val = { 0.f, 0.f , 0.f , 0.f };
                }
                tempbuf[y * width + x] = (1.f / val.w) * val;

                tempbuf[y * width + x].x = std::pow(tempbuf[y * width + x].x, 1.f / 2.2f);
                tempbuf[y * width + x].y = std::pow(tempbuf[y * width + x].y, 1.f / 2.2f);
                tempbuf[y * width + x].z = std::pow(tempbuf[y * width + x].z, 1.f / 2.2f);
            }

        if (nan_num != 0)
        {
            std::cerr << name << ": " << nan_num << " NaN pixels." << std::endl;
        }
        ImageOutput* out = ImageOutput::create(name);

        if (!out)
        {
            throw std::runtime_error("Can't create image file on disk");
        }

        ImageSpec spec(width, height, 3, fmt);

        out->open(name, spec);
        out->write_image(TypeDesc::FLOAT, &tempbuf[0], sizeof(float3));
        out->close();
    }

    void AppClRender::RenderThread(ControlData& cd)
    {
        auto renderer = m_cfgs[cd.idx].renderer.get();
        auto controller = m_cfgs[cd.idx].controller.get();
        auto output = m_outputs[cd.idx].output.get();

        auto updatetime = std::chrono::high_resolution_clock::now();

        while (!cd.stop.load())
        {
            int result = 1;
            bool update = false;

            if (std::atomic_compare_exchange_strong(&cd.clear, &result, 0))
            {
                renderer->Clear(float3(0, 0, 0), *output);
                controller->CompileScene(m_scene);
                update = true;
            }

            auto& scene = m_cfgs[m_primary].controller->GetCachedScene(m_scene);
            renderer->Render(scene);

            auto now = std::chrono::high_resolution_clock::now();

            update = update || (std::chrono::duration_cast<std::chrono::seconds>(now - updatetime).count() > 1);

            if (update)
            {
                m_outputs[cd.idx].output->GetData(&m_outputs[cd.idx].fdata[0]);
                updatetime = now;
                cd.newdata.store(1);
            }

            m_cfgs[cd.idx].context.Finish(0);
        }
    }

    void AppClRender::StartRenderThreads()
    {
        for (int i = 0; i < m_cfgs.size(); ++i)
        {
            if (i != m_primary)
            {
                m_renderthreads.push_back(std::thread(&AppClRender::RenderThread, this, std::ref(m_ctrl[i])));
                m_renderthreads.back().detach();
            }
        }

        std::cout << m_cfgs.size() << " OpenCL submission threads started\n";
    }

    void AppClRender::StopRenderThreads()
    {
        for (int i = 0; i < m_cfgs.size(); ++i)
        {
            if (i == m_primary)
                continue;

            m_ctrl[i].stop.store(true);
        }
    }

    void AppClRender::RunBenchmark(AppSettings& settings)
    {
        std::cout << "Running general benchmark...\n";

        auto time_bench_start_time = std::chrono::high_resolution_clock::now();
        for (auto i = 0U; i < 512; ++i)
        {
            Render(0);
        }

        m_cfgs[m_primary].context.Finish(0);

        auto delta = std::chrono::duration_cast<std::chrono::milliseconds>
            (std::chrono::high_resolution_clock::now() - time_bench_start_time).count();

        settings.time_benchmark_time = delta / 1000.f;

        m_outputs[m_primary].output->GetData(&m_outputs[m_primary].fdata[0]);
        float gamma = 2.2f;
        for (int i = 0; i < (int)m_outputs[m_primary].fdata.size(); ++i)
        {
            m_outputs[m_primary].udata[4 * i] = (unsigned char)clamp(clamp(pow(m_outputs[m_primary].fdata[i].x / m_outputs[m_primary].fdata[i].w, 1.f / gamma), 0.f, 1.f) * 255, 0, 255);
            m_outputs[m_primary].udata[4 * i + 1] = (unsigned char)clamp(clamp(pow(m_outputs[m_primary].fdata[i].y / m_outputs[m_primary].fdata[i].w, 1.f / gamma), 0.f, 1.f) * 255, 0, 255);
            m_outputs[m_primary].udata[4 * i + 2] = (unsigned char)clamp(clamp(pow(m_outputs[m_primary].fdata[i].z / m_outputs[m_primary].fdata[i].w, 1.f / gamma), 0.f, 1.f) * 255, 0, 255);
            m_outputs[m_primary].udata[4 * i + 3] = 1;
        }

        auto& fdata = m_outputs[m_primary].fdata;
        std::vector<RadeonRays::float3> data(fdata.size());
        std::transform(fdata.cbegin(), fdata.cend(), data.begin(),
            [](RadeonRays::float3 const& v)
        {
            float invw = 1.f / v.w;
            return v * invw;
        });

        std::stringstream oss;
        oss << "../Output/" << settings.modelname << ".exr";

        SaveImage(oss.str(), settings.width, settings.height, 32, &data[0]);

        std::cout << "Running RT benchmark...\n";

        auto& scene = m_cfgs[m_primary].controller->GetCachedScene(m_scene);
        static_cast<MonteCarloRenderer*>(m_cfgs[m_primary].renderer.get())->Benchmark(scene, settings.stats);
    }

    void AppClRender::SetNumBounces(int num_bounces)
    {
        for (int i = 0; i < m_cfgs.size(); ++i)
        {
            static_cast<Baikal::MonteCarloRenderer*>(m_cfgs[i].renderer.get())->SetMaxBounces(num_bounces);
        }
    }

    void AppClRender::SetOutputType(Renderer::OutputType type)
    {
        for (int i = 0; i < m_cfgs.size(); ++i)
        {
            m_cfgs[i].renderer->SetOutput(m_output_type, nullptr);
            m_cfgs[i].renderer->SetOutput(type, m_outputs[i].output.get());
        }
        m_output_type = type;
    }

    void AppClRender::EnableOutputType(Renderer::OutputType type)
    {
        assert(type != m_output_type);
        for (int i = 0; i < m_cfgs.size(); ++i)
        {
            auto main_output = m_cfgs[i].renderer->GetOutput(m_output_type);
            int w = main_output->width();
            int h = main_output->height();
            if (!m_cfgs[i].renderer->GetOutput(type))
            {
                auto aov = m_cfgs[i].factory->CreateOutput(w, h);
                m_cfgs[i].renderer->SetOutput(type, aov.get());
                m_outputs[i].aovs.push_back(std::move(aov));
            }
        }
    }
    void AppClRender::DisableOutputType(Renderer::OutputType type)
    {
        assert(type != m_output_type);
        for (int i = 0; i < m_cfgs.size(); ++i)
        {
            auto aov = m_cfgs[i].renderer->GetOutput(type);
            if (aov)
            {
                m_cfgs[i].renderer->SetOutput(type, nullptr);
                auto it = std::find_if(m_outputs[i].aovs.begin(), m_outputs[i].aovs.end(), [aov](const std::unique_ptr<Baikal::Output>& ptr) {return ptr.get() == aov; });
                m_outputs[i].aovs.erase(it);
            }
        }
    }


#ifdef ENABLE_DENOISER  
    void AppClRender::SetDenoiserFloatParam(const std::string& name, const float4& value)
    {
        m_outputs[m_primary].denoiser->SetParameter(name, value);
    }

    float4 AppClRender::GetDenoiserFloatParam(const std::string& name)
    {
        return m_outputs[m_primary].denoiser->GetParameter(name);
    }
#endif
} // Baikal
