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

#include <cstdint>

namespace Raytrace
{
    static constexpr std::uint32_t kInvalidId = 0xfffffffffu;

    struct Shape
    {
        std::uint32_t indexOffset;
        std::uint32_t numTriangles;
        std::uint32_t materialIndex;
        std::uint32_t unused;
    };

    struct Material
    {
        float diffuse[3] = { 1.f, 1.f, 1.f };
        std::uint32_t diffuseMap = kInvalidId;
        float specular[3] = { 0.f, 0.f, 0.f };
        std::uint32_t specularMap = kInvalidId;
        float roughness = 1.f;
        std::uint32_t roughnessMap = kInvalidId;
        float metalness = 1.f;
        std::uint32_t metalnessMap = kInvalidId;
    };
}