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

#include <bvh.h>
#include <bvh_encoder.h>
#include <qbvh_encoder.h>

namespace RadeonRays {
    // Various BVH type configs
    // Binary BVH in world space
    using BVH2 = BVH<WorldMeshObjectTraits,
                     BVHNode,
                     BVHNodeTraits<WorldMeshObjectTraits>,
                     PostProcessor<WorldMeshObjectTraits,
                                   BVHNodeTraits<WorldMeshObjectTraits>>>;
    // QBVH in world space
    using BVH4 = QBVH<WorldMeshObjectTraits>;
    // Binary BVH in local space, being used in two level structure
    using BVH2LBottom = BVH<LocalMeshObjectTraits,
                            BVHNode,
                            BVHNodeTraits<LocalMeshObjectTraits>,
                            PostProcessor<LocalMeshObjectTraits,
                                          BVHNodeTraits<WorldMeshObjectTraits>>>;
    // Binary BVH built on bottom level BVH objects
    using BVH2LTop = BVH<BVHObjectTraits,
                         BVHNode,
                         TopLevelBVHNodeTraits<BVHObjectTraits>,
                         PostProcessor<BVHObjectTraits,
                                       TopLevelBVHNodeTraits<BVHObjectTraits>>>;

    // BVH streaming and control
    template <typename BVH> struct BVHTraits
    {
        static std::size_t GetSizeInBytes(BVH const& bvh)
        {
            return bvh.num_nodes() * sizeof(typename BVH::NodeT);
        }

        static void StreamBVH(BVH const& bvh, void* ptr)
        {
            auto data = reinterpret_cast<typename BVH::NodeT*>(ptr);
            for (auto i = 0u; i < bvh.num_nodes(); ++i) {
                data[i] = *bvh.GetNode(i);
            }
        }

        static constexpr char const* GetGPUTraversalFileName()
        {
            return BVH::NodeT::kTraversalKernelFileName;
        }
    };
}
