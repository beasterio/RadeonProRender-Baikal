
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

#include <string>

#include "radeon_rays.h"
#include "Utils/config_manager.h"
#include "Baikal/Renderers/renderer.h"
#include "Baikal/Estimators/estimator.h"

namespace Baikal
{
    struct AppSettings
    {
        AppSettings();

        // OpenCL platform & device settings
        int platform_index;
        int device_index;

        //model load settings
        std::string path;
        std::string modelname;
        std::string envmapname;

        //render
        int width;
        int height;
        int num_bounces;
        int num_samples;
        bool interop;
        float cspeed;
        ConfigManager::Mode mode;

        //ao
        float ao_radius;
        int num_ao_rays;
        bool ao_enabled;

        //camera
        RadeonRays::float3 camera_pos;
        RadeonRays::float3 camera_at;
        RadeonRays::float3 camera_up;
        RadeonRays::float2 camera_sensor_size;
        RadeonRays::float2 camera_zcap;
        float camera_aperture;
        float camera_focus_distance;
        float camera_focal_length;
        CameraType camera_type;

        //file with camera positions
        std::string camera_set;
        //range of camera set
        int camera_set_min;
        int camera_set_max;

        //folder to store camera position output
        std::string camera_out_folder;
        //folder to store AOV framebuffers
        std::string aov_out_folder;
        bool save_aov;
        std::string aov_samples_set;
        std::vector<int> aov_samples;

        //file with scene lights description
        std::string light_set;


        //app
        bool progressive;
        bool cmd_line_mode;
        bool recording_enabled;
        bool benchmark;
        bool gui_visible;

        //bencmark
        Estimator::RayTracingStats stats;
        bool time_benchmarked;
        bool rt_benchmarked;
        bool time_benchmark;
        float time_benchmark_time;

        //image file
        std::string base_image_file_name;
        std::string image_file_format;

        //unused
        int num_shadow_rays;
        int samplecount;
        float envmapmul;
    };

    class AppCliParser
    {
    public:

        AppCliParser();
        AppSettings Parse(int argc, char * argv[]);
    private:
        char* GetCmdOption(char ** begin, char ** end, const std::string & option);
        bool CmdOptionExists(char ** begin, char ** end, const std::string& option);
        void ShowHelp();


    };
}
