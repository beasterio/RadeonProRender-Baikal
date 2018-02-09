
/**********************************************************************
 Copyright (c) 2017 Advanced Micro Devices, Inc. All rights reserved.
 
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

#include <memory>
#include <vector>
#include "Output/output.h"
#include "SceneGraph/vkscene.h"
#include "math/float3.h"
#include "app_utils.h"
#include "GL/glew.h"

namespace Baikal
{
    class AppVkRender
    {
        struct OutputData
        {
            std::unique_ptr<Baikal::Output> output;
            std::vector<RadeonRays::float3> fdata;
            std::vector<unsigned char> udata;
            //CLWBuffer<RadeonRays::float3> copybuffer;
        };

        //struct ControlData
        //{
        //    std::atomic<int> clear;
        //    std::atomic<int> stop;
        //    std::atomic<int> newdata;
        //    std::mutex datamutex;
        //    int idx;
        //};

    public:
        AppVkRender(AppSettings& settings, GLuint tex);
        //copy data from to GL
        void Update(AppSettings& settings);

        //compile scene
        void UpdateScene();
        //render
        void Render(int sample_cnt);
        void StartRenderThreads();
        void StopRenderThreads();
        void RunBenchmark(AppSettings& settings);

        //save cl frame buffer to file
        void SaveFrameBuffer(AppSettings& settings);
        void SaveImage(const std::string& name, int width, int height, const RadeonRays::float3* data);

        inline Baikal::Camera::Ptr GetCamera() { return m_camera; };
        inline Baikal::Scene1::Ptr GetScene() { return m_scene; };
//        inline CLWDevice GetDevice(int i) { return m_cfgs[m_primary].context.GetDevice(i); };
        inline OutputType GetOutputType() { return m_output_type; };

        void SetNumBounces(int num_bounces);
        void SetOutputType(OutputType type);
#ifdef ENABLE_DENOISER        
        // Denoiser
        void SetDenoiserFloatParam(const std::string& name, const float4& value);
        float4 GetDenoiserFloatParam(const std::string& name);
#endif
    private:
        void InitVk(AppSettings& settings, GLuint tex);
        void LoadScene(AppSettings& settings);

        Baikal::Scene1::Ptr m_scene;
        Baikal::Camera::Ptr m_camera;

        std::vector<ConfigManager::VkConfig> m_cfgs;
        //std::vector<OutputData> m_outputs;
        // std::unique_ptr<ControlData[]> m_ctrl;
        // std::vector<std::thread> m_renderthreads;
        int m_primary = -1;

        // //if interop
        // CLWImage2D m_cl_interop_image;
        // //save GL tex for no interop case
        // GLuint m_tex;
        OutputType m_output_type;
        OutputData m_output;

        //gl_reder tex
        GLuint m_tex;
    };
}
