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
#include <vulkan/vulkan.hpp>
#include "world.h"

namespace RadeonRays {
    class Intersector {
    public:
        Intersector() = default;
        virtual ~Intersector() = default;
        virtual vk::CommandBuffer Commit(World const& world) = 0;
        virtual void BindBuffers(VkDescriptorBufferInfo rays,
            VkDescriptorBufferInfo hits,
            VkDescriptorBufferInfo ray_count) = 0;
        virtual void TraceRays(std::uint32_t max_rays,
                               VkCommandBuffer& command_buffer) = 0;

        virtual void SetPerformanceQueryInfo(VkQueryPool query_pool, uint32_t begin_query, uint32_t end_query) = 0;

        Intersector(Intersector const&) = delete;
        Intersector& operator = (Intersector const&) = delete;
    };
}