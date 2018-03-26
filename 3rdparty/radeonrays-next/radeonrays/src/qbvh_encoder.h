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

//#include <radeonrays.h>
#include <cstdint>
#include <qbvh_utils.h>
#include <bvh_encoder.h>
#include <vector>

#define RR_INVALID_ID 0xffffffffu

namespace RadeonRays
{
    // Encoded node format.
    // In QBVH we store 4x16-bits AABBs per node
    struct QBVHNode
    {
        // Min points of AABB 0 and 1 for internal node
        // or first triangle vertex for the leaf
        std::uint32_t aabb01_min_or_v0[3];
        // Address of the first child for internal node,
        // RR_INVALID_ID for the leaf
        std::uint32_t addr0 = RR_INVALID_ID;
        // Max points of AABB 0 and 1 for internal node
        // or second triangle vertex for the leaf
        std::uint32_t aabb01_max_or_v1[3];
        // Address of the second child for internal node,
        // mesh ID for the leaf
        std::uint32_t addr1_or_mesh_id = RR_INVALID_ID;
        // Min points of AABB 2 and 3 for internal node
        // or third triangle vertex for the leaf
        std::uint32_t aabb23_min_or_v2[3];
        // Address of the third child for internal node,
        // primitive ID for the leaf
        std::uint32_t addr2_or_prim_id = RR_INVALID_ID;
        // Max points of AABB 2 and 3 for internal node
        std::uint32_t aabb23_max[3];
        // Address of the fourth child for internal node
        std::uint32_t addr3 = RR_INVALID_ID;

        static constexpr char const* kTraversalKernelFileName = "isect_fp16.comp.spv";
    };

    // 4-BVH with 16bits AABBs
    template <typename ObjectTraits> class QBVH
    {
        // Base BVH this one is built upon
        using BaseBVHType = BVH<ObjectTraits,
                                BVHNode,
                                BVHNodeTraits<ObjectTraits>,
                                PostProcessor<ObjectTraits, BVHNodeTraits<ObjectTraits>>>;
        
    public:
        using NodeT = QBVHNode;
        
        // Build from a range of objects
        template<typename Iter> void Build(Iter begin, Iter end)
        {
            // Build base BVH
            BaseBVHType bvh;
            bvh.Build(begin, end);

            nodes_.resize(1);
            
            // Node correspondence structure
            struct Elem {
                std::uint32_t bvh_node_index;
                std::uint32_t qbvh_node_index;
            };

            std::stack<Elem> stack;
            stack.push({ 0u, 0u });

            // Traverse base BVH and build QBVH out of it
            while (!stack.empty())
            {
                // Pop next BVH node
                auto node = bvh.GetNode(stack.top().bvh_node_index);
                auto qbvh_node_index = stack.top().qbvh_node_index;
                stack.pop();

                // If it is a leaf node, copy its data into QBVH node
                if (!BVHNodeTraits<ObjectTraits>::IsInternal(*node))
                {
                    nodes_[qbvh_node_index].addr0 = RR_INVALID_ID;
                    nodes_[qbvh_node_index].addr3 = RR_INVALID_ID;

                    copy3(node->aabb_left_min_or_v0, nodes_[qbvh_node_index].aabb01_min_or_v0);
                    copy3(node->aabb_left_max_or_v1, nodes_[qbvh_node_index].aabb01_max_or_v1);
                    copy3(node->aabb_right_min_or_v2, nodes_[qbvh_node_index].aabb23_min_or_v2);

                    nodes_[qbvh_node_index].addr1_or_mesh_id = node->mesh_id;
                    nodes_[qbvh_node_index].addr2_or_prim_id = node->prim_id;
                    continue;
                }

                // If it is internal, we need to fetch its kids
                auto c0idx = BVHNodeTraits<ObjectTraits>::GetChildIndex(*node, 0);
                auto c1idx = BVHNodeTraits<ObjectTraits>::GetChildIndex(*node, 1);

                auto c0 = bvh.GetNode(c0idx);
                auto c1 = bvh.GetNode(c1idx);

                // Now we check if kids are internal,
                // if yes we pull them up to QBVH our node.
                if (BVHNodeTraits<ObjectTraits>::IsInternal(*c0))
                {
                    copy3pack_lo_min(c0->aabb_left_min_or_v0, nodes_[qbvh_node_index].aabb01_min_or_v0);
                    copy3pack_lo_max(c0->aabb_left_max_or_v1, nodes_[qbvh_node_index].aabb01_max_or_v1);
                    copy3pack_hi_min(c0->aabb_right_min_or_v2, nodes_[qbvh_node_index].aabb01_min_or_v0);
                    copy3pack_hi_max(c0->aabb_right_max, nodes_[qbvh_node_index].aabb01_max_or_v1);

                    auto idx = (std::uint32_t)nodes_.size();
                    nodes_.push_back(QBVHNode{});
                    nodes_.push_back(QBVHNode{});

                    nodes_[qbvh_node_index].addr0 = idx;
                    nodes_[qbvh_node_index].addr1_or_mesh_id = idx + 1;

                    stack.push({ BVHNodeTraits<ObjectTraits>::GetChildIndex(*c0, 0),(std::uint32_t)idx });
                    stack.push({ BVHNodeTraits<ObjectTraits>::GetChildIndex(*c0, 1),(std::uint32_t)idx + 1 });
                }
                else
                {
                    // Otherwise create a leaf node and reference it
                    copy3pack_lo_min(node->aabb_left_min_or_v0, nodes_[qbvh_node_index].aabb01_min_or_v0);
                    copy3pack_lo_max(node->aabb_left_max_or_v1, nodes_[qbvh_node_index].aabb01_max_or_v1);

                    auto idx = (std::uint32_t)nodes_.size();
                    nodes_.push_back(QBVHNode{});

                    nodes_[qbvh_node_index].addr0 = idx;
                    nodes_[qbvh_node_index].addr1_or_mesh_id = RR_INVALID_ID;

                    auto& child_node = nodes_[idx];
                    child_node.addr0 = RR_INVALID_ID;
                    child_node.addr3 = RR_INVALID_ID;
                    copy3(c0->aabb_left_min_or_v0, child_node.aabb01_min_or_v0);
                    copy3(c0->aabb_left_max_or_v1, child_node.aabb01_max_or_v1);
                    copy3(c0->aabb_right_min_or_v2, child_node.aabb23_min_or_v2);
                    child_node.addr1_or_mesh_id = c0->mesh_id;
                    child_node.addr2_or_prim_id = c0->prim_id;
                }

                // Now we check if kids are internal,
                // if yes we pull them up to QBVH our node.
                if (BVHNodeTraits<ObjectTraits>::IsInternal(*c1))
                {
                    copy3pack_lo_min(c1->aabb_left_min_or_v0, nodes_[qbvh_node_index].aabb23_min_or_v2);
                    copy3pack_lo_max(c1->aabb_left_max_or_v1, nodes_[qbvh_node_index].aabb23_max);
                    copy3pack_hi_min(c1->aabb_right_min_or_v2, nodes_[qbvh_node_index].aabb23_min_or_v2);
                    copy3pack_hi_max(c1->aabb_right_max, nodes_[qbvh_node_index].aabb23_max);

                    auto idx = (std::uint32_t)nodes_.size();
                    nodes_.push_back(QBVHNode{});
                    nodes_.push_back(QBVHNode{});

                    nodes_[qbvh_node_index].addr2_or_prim_id = idx;
                    nodes_[qbvh_node_index].addr3 = idx + 1;

                    stack.push({ BVHNodeTraits<ObjectTraits>::GetChildIndex(*c1, 0),(std::uint32_t)idx });
                    stack.push({ BVHNodeTraits<ObjectTraits>::GetChildIndex(*c1, 1),(std::uint32_t)idx + 1 });
                }
                else
                {
                    // Otherwise create a leaf node and reference it
                    copy3pack_lo_min(node->aabb_right_min_or_v2, nodes_[qbvh_node_index].aabb23_min_or_v2);
                    copy3pack_lo_max(node->aabb_right_max, nodes_[qbvh_node_index].aabb23_max);

                    auto idx = (std::uint32_t)nodes_.size();
                    nodes_.push_back(QBVHNode{});

                    nodes_[qbvh_node_index].addr2_or_prim_id = idx;
                    nodes_[qbvh_node_index].addr3 = RR_INVALID_ID;

                    auto& child_node = nodes_[idx];
                    child_node.addr0 = RR_INVALID_ID;
                    child_node.addr3 = RR_INVALID_ID;
                    copy3(c1->aabb_left_min_or_v0, child_node.aabb01_min_or_v0);
                    copy3(c1->aabb_left_max_or_v1, child_node.aabb01_max_or_v1);
                    copy3(c1->aabb_right_min_or_v2, child_node.aabb23_min_or_v2);
                    child_node.addr1_or_mesh_id = c1->mesh_id;
                    child_node.addr2_or_prim_id = c1->prim_id;
                }
            }
        }

        // Remove the nodes
        void Clear()
        {
            nodes_.resize(0);
        }

        // Access root node
        auto root() const
        {
            return nodes_[0];
        }
        
        // Number of BVH nodes
        auto num_nodes() const
        {
            return nodes_.size();
        }

        // Random node access
        auto GetNode(std::size_t idx) const
        {
            return &nodes_[idx];
        }

    private:
        // Node array
        std::vector<QBVHNode> nodes_;
    };
}
