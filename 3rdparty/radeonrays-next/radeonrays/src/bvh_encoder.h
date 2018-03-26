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
#include <bvh.h>
#include <mesh.h>

#define RR_INVALID_ID 0xffffffffu

namespace RadeonRays
{
    // Encoded node format.
    struct BVHNode
    {
        // Left AABB min or vertex 0 for a leaf node
        float aabb_left_min_or_v0[3] = { 0.f, 0.f, 0.f };
        // Left child node address
        uint32_t addr_left = RR_INVALID_ID;
        // Left AABB max or vertex 1 for a leaf node
        float aabb_left_max_or_v1[3] = { 0.f, 0.f, 0.f };
        // Mesh ID for a leaf node
        uint32_t mesh_id = RR_INVALID_ID;
        // Right AABB min or vertex 2 for a leaf node
        float aabb_right_min_or_v2[3] = { 0.f, 0.f, 0.f };
        // Right child node address
        uint32_t addr_right = RR_INVALID_ID;
        // Left AABB max
        float aabb_right_max[3] = { 0.f, 0.f, 0.f };
        // Primitive ID for a leaf node
        uint32_t prim_id = RR_INVALID_ID;

        static constexpr char const* kTraversalKernelFileName = "isect.comp.spv";
    };
    
    // Object description for world-space meshes
    struct WorldMeshObjectTraits
    {
        using Type = Shape const*;
        
        // Number of subobjects == number of faces for the mesh
        static std::size_t GetNumSubObjects(Type shape)
        {
            auto mesh = static_cast<Mesh const*>(shape);
            return mesh->num_faces();
        }
        
        // AABB is in world space
        static auto GetAABB(Type shape, std::size_t face_index)
        {
            AABB aabb;
            auto mesh = static_cast<Mesh const*>(shape);
            auto bounds = mesh->GetFaceBounds(face_index, Mesh::CoordinateSpace::kWorld);
            
            aabb.pmin = _mm_set_ps(bounds.pmin.w, bounds.pmin.z, bounds.pmin.y, bounds.pmin.x);
            aabb.pmax = _mm_set_ps(bounds.pmax.w, bounds.pmax.z, bounds.pmax.y, bounds.pmax.x);
            return aabb;
        }
    };
    
    // Object description for local-space meshes
    struct LocalMeshObjectTraits
    {
        using Type = Shape const*;
        
        // Number of subobjects == number of faces for the mesh
        static std::size_t GetNumSubObjects(Type shape)
        {
            auto mesh = static_cast<Mesh const*>(shape);
            return mesh->num_faces();
        }
        
        // AABB is in local space
        static auto GetAABB(Type shape, std::size_t face_index)
        {
            AABB aabb;
            
            auto mesh = static_cast<Mesh const*>(shape);
            auto face = mesh->GetIndexData(face_index);
            
            auto v0 = _mm_load_ps((float*)mesh->GetVertexDataPtr(face.idx[0]));
            auto v1 = _mm_load_ps((float*)mesh->GetVertexDataPtr(face.idx[1]));
            auto v2 = _mm_load_ps((float*)mesh->GetVertexDataPtr(face.idx[2]));
            
            aabb.pmin = _mm_min_ps(_mm_min_ps(v0, v1), v2);
            aabb.pmax = _mm_max_ps(_mm_max_ps(v0, v1), v2);
            return aabb;
        }
    };

    // Properties of BVHNode for meshes
    template <typename ObjectTraits> struct BVHNodeTraits
    {
        // Max triangles per leaf
        static std::uint32_t constexpr kMaxLeafPrimitives = 1u;
        // Threshold number of primitives to disable SAH split
        static std::uint32_t constexpr kMinSAHPrimitives = 32u;
        // Traversal vs intersection cost ratio
        static std::uint32_t constexpr kTraversalCost = 10u;

        // Create leaf node
        static void EncodeLeaf(BVHNode& node, std::uint32_t)
        {
            node.addr_left = RR_INVALID_ID;
            node.addr_right = RR_INVALID_ID;
        }

        // Create internal node
        static void EncodeInternal(BVHNode& node,
                                   AABB const& aabb,
                                   std::uint32_t child0,
                                   std::uint32_t child1)
        {
            _mm_store_ps(node.aabb_left_min_or_v0, aabb.pmin);
            _mm_store_ps(node.aabb_left_max_or_v1, aabb.pmax);
            node.addr_left = child0;
            node.addr_right = child1;
        }

        // Encode primitive
        static void SetPrimitive(BVHNode& node,
                                 std::uint32_t,
                                 std::tuple<Shape const*, std::size_t, std::size_t> ref)
        {
            auto mesh = static_cast<Mesh const*>(std::get<0>(ref));
            auto vertices = mesh->GetFaceVertexData(std::get<2>(ref));
            node.aabb_left_min_or_v0[0] = vertices[0].x;
            node.aabb_left_min_or_v0[1] = vertices[0].y;
            node.aabb_left_min_or_v0[2] = vertices[0].z;
            node.aabb_left_max_or_v1[0] = vertices[1].x;
            node.aabb_left_max_or_v1[1] = vertices[1].y;
            node.aabb_left_max_or_v1[2] = vertices[1].z;
            node.aabb_right_min_or_v2[0] = vertices[2].x;
            node.aabb_right_min_or_v2[1] = vertices[2].y;
            node.aabb_right_min_or_v2[2] = vertices[2].z;
            node.mesh_id = mesh->GetId();
            node.prim_id = static_cast<std::uint32_t>(std::get<2>(ref));
        }

        // Check if the node is internal/leaf
        static bool IsInternal(BVHNode& node)
        {
            return node.addr_left != RR_INVALID_ID;
        }
        
        static std::uint32_t GetChildIndex(BVHNode& node, std::uint8_t idx)
        {
            return IsInternal(node)
                ? (idx == 0 ?
                    node.addr_left
                    : node.addr_right)
                : RR_INVALID_ID;
        }
    };
    
    template <typename ObjectTraits, typename BVHNodeTraits>
    struct PostProcessor
    {
        // We set 1 AABB for each node during BVH build process,
        // however our resulting structure keeps two AABBs for
        // left and right child nodes in the parent node. To
        // convert 1 AABB per node -> 2 AABBs for child nodes
        // we need to traverse the tree pulling child node AABBs
        // into their parent node. That's exactly what PropagateBounds
        // is doing.
        static void Finalize(BVH<ObjectTraits,
                             BVHNode, BVHNodeTraits,
                             PostProcessor<ObjectTraits, BVHNodeTraits>>& bvh)
        {
            // Traversal stack
            std::stack<std::uint32_t> s;
            s.push(0);
            
            while (!s.empty())
            {
                auto idx = s.top();
                s.pop();
                // Fetch the node
                auto node = bvh.GetNode(idx);
                
                if (BVHNodeTraits::IsInternal(*node))
                {
                    // If the node is internal we fetch child nodes
                    auto idx0 = BVHNodeTraits::GetChildIndex(*node, 0);
                    auto idx1 = BVHNodeTraits::GetChildIndex(*node, 1);
                    
                    auto child0 = bvh.GetNode(idx0);
                    auto child1 = bvh.GetNode(idx1);
                    
                    // If the child is internal node itself we pull it
                    // up the tree into its parent. If the child node is
                    // a leaf, then we do not have AABB for it (we store
                    // vertices directly in the leaf), so we calculate
                    // AABB on the fly.
                    // TODO: Fix code duplication here
                    if (BVHNodeTraits::IsInternal(*child0))
                    {
                        node->aabb_left_min_or_v0[0] = child0->aabb_left_min_or_v0[0];
                        node->aabb_left_min_or_v0[1] = child0->aabb_left_min_or_v0[1];
                        node->aabb_left_min_or_v0[2] = child0->aabb_left_min_or_v0[2];
                        node->aabb_left_max_or_v1[0] = child0->aabb_left_max_or_v1[0];
                        node->aabb_left_max_or_v1[1] = child0->aabb_left_max_or_v1[1];
                        node->aabb_left_max_or_v1[2] = child0->aabb_left_max_or_v1[2];
                        s.push(idx0);
                    }
                    else
                    {
                        node->aabb_left_min_or_v0[0] = std::min(child0->aabb_left_min_or_v0[0],
                                                                std::min(child0->aabb_left_max_or_v1[0],
                                                                         child0->aabb_right_min_or_v2[0]));
                        
                        node->aabb_left_min_or_v0[1] = std::min(child0->aabb_left_min_or_v0[1],
                                                                std::min(child0->aabb_left_max_or_v1[1],
                                                                         child0->aabb_right_min_or_v2[1]));
                        
                        node->aabb_left_min_or_v0[2] = std::min(child0->aabb_left_min_or_v0[2],
                                                                std::min(child0->aabb_left_max_or_v1[2],
                                                                         child0->aabb_right_min_or_v2[2]));
                        
                        node->aabb_left_max_or_v1[0] = std::max(child0->aabb_left_min_or_v0[0],
                                                                std::max(child0->aabb_left_max_or_v1[0],
                                                                         child0->aabb_right_min_or_v2[0]));
                        
                        node->aabb_left_max_or_v1[1] = std::max(child0->aabb_left_min_or_v0[1],
                                                                std::max(child0->aabb_left_max_or_v1[1],
                                                                         child0->aabb_right_min_or_v2[1]));
                        
                        node->aabb_left_max_or_v1[2] = std::max(child0->aabb_left_min_or_v0[2],
                                                                std::max(child0->aabb_left_max_or_v1[2],
                                                                         child0->aabb_right_min_or_v2[2]));
                    }
                    
                    // If the child is internal node itself we pull it
                    // up the tree into its parent. If the child node is
                    // a leaf, then we do not have AABB for it (we store
                    // vertices directly in the leaf), so we calculate
                    // AABB on the fly.
                    if (BVHNodeTraits::IsInternal(*child1))
                    {
                        node->aabb_right_min_or_v2[0] = child1->aabb_left_min_or_v0[0];
                        node->aabb_right_min_or_v2[1] = child1->aabb_left_min_or_v0[1];
                        node->aabb_right_min_or_v2[2] = child1->aabb_left_min_or_v0[2];
                        node->aabb_right_max[0] = child1->aabb_left_max_or_v1[0];
                        node->aabb_right_max[1] = child1->aabb_left_max_or_v1[1];
                        node->aabb_right_max[2] = child1->aabb_left_max_or_v1[2];
                        s.push(idx1);
                    }
                    else
                    {
                        node->aabb_right_min_or_v2[0] = std::min(child1->aabb_left_min_or_v0[0],
                                                                 std::min(child1->aabb_left_max_or_v1[0],
                                                                          child1->aabb_right_min_or_v2[0]));
                        
                        node->aabb_right_min_or_v2[1] = std::min(child1->aabb_left_min_or_v0[1],
                                                                 std::min(child1->aabb_left_max_or_v1[1],
                                                                          child1->aabb_right_min_or_v2[1]));
                        
                        node->aabb_right_min_or_v2[2] = std::min(child1->aabb_left_min_or_v0[2],
                                                                 std::min(child1->aabb_left_max_or_v1[2],
                                                                          child1->aabb_right_min_or_v2[2]));
                        
                        node->aabb_right_max[0] = std::max(child1->aabb_left_min_or_v0[0],
                                                           std::max(child1->aabb_left_max_or_v1[0],
                                                                    child1->aabb_right_min_or_v2[0]));
                        
                        node->aabb_right_max[1] = std::max(child1->aabb_left_min_or_v0[1],
                                                           std::max(child1->aabb_left_max_or_v1[1],
                                                                    child1->aabb_right_min_or_v2[1]));
                        
                        node->aabb_right_max[2] = std::max(child1->aabb_left_min_or_v0[2],
                                                           std::max(child1->aabb_left_max_or_v1[2],
                                                                    child1->aabb_right_min_or_v2[2]));
                    }
                }
            }
        }
    };
    
    // Object description for bottom-level BVH
    struct BVHObjectTraits
    {
        // BVH is based on LocalMeshObjects
        using BVHType = BVH<LocalMeshObjectTraits,
                            BVHNode,
                            BVHNodeTraits<LocalMeshObjectTraits>,
                            PostProcessor<LocalMeshObjectTraits, BVHNodeTraits<LocalMeshObjectTraits>>>;
        
        // BVH description
        struct BVHDesc
        {
            // BVH
            BVHType bvh;
            // World transform
            RadeonRays::matrix transform;
            // Offset in nodes buffer (for linear layout)
            std::size_t root_offset;
        };
        
        using Type = BVHDesc;
        
        // Always has 1 subobject
        static std::size_t GetNumSubObjects(Type const&)
        {
            return 1u;
        }
        
        // AABB is in world space
        static auto GetAABB(Type const& bvh, std::size_t)
        {
            // Take AABB from the BVH
            AABB aabb;
            auto root = bvh.bvh.root();
            
            // TODO: accelerate on SSE
            auto lmin = float3(root->aabb_left_min_or_v0[0], root->aabb_left_min_or_v0[1], root->aabb_left_min_or_v0[2], 1.f);
            auto lmax = float3(root->aabb_left_max_or_v1[0], root->aabb_left_max_or_v1[1], root->aabb_left_max_or_v1[2], 1.f);
            
            auto rmin = float3(root->aabb_right_min_or_v2[0], root->aabb_right_min_or_v2[1], root->aabb_right_min_or_v2[2], 1.f);
            auto rmax = float3(root->aabb_right_max[0], root->aabb_right_max[1], root->aabb_right_max[2], 1.f);
            
            bbox bounds(lmin, lmax);
            bounds.grow(rmin);
            bounds.grow(rmax);
            
            // Transform it into world space
            auto tbounds = transform_bbox(bounds, bvh.transform);
            
            aabb.pmin = _mm_set_ps(tbounds.pmin.w, tbounds.pmin.z, tbounds.pmin.y, tbounds.pmin.x);
            aabb.pmax = _mm_set_ps(tbounds.pmax.w, tbounds.pmax.z, tbounds.pmax.y, tbounds.pmax.x);
            
            return aabb;
        }
    };
    
    // Properties of BVHNode for a top level of 2 level BVH
    template <typename ObjectTraits> struct TopLevelBVHNodeTraits
    {
        // Max triangles per leaf
        static std::uint32_t constexpr kMaxLeafPrimitives = 1u;
        // Threshold number of primitives to disable SAH split
        static std::uint32_t constexpr kMinSAHPrimitives = 8u;
        // Traversal vs intersection cost ratio
        static std::uint32_t constexpr kTraversalCost = 10u;
        
        // Create leaf node
        static void EncodeLeaf(BVHNode& node, std::uint32_t)
        {
            node.addr_left = RR_INVALID_ID;
            node.addr_right = RR_INVALID_ID;
        }
        
        // Create internal node
        static void EncodeInternal(BVHNode& node,
                                   AABB const& aabb,
                                   std::uint32_t child0,
                                   std::uint32_t child1)
        {
            _mm_store_ps(node.aabb_left_min_or_v0, aabb.pmin);
            _mm_store_ps(node.aabb_left_max_or_v1, aabb.pmax);
            node.addr_left = child0;
            node.addr_right = child1;
        }
        
        // Encode primitive
        static void SetPrimitive(BVHNode& node,
                                 std::uint32_t,
                                 std::tuple<BVHObjectTraits::Type, std::size_t, std::size_t> ref)
        {
            auto& bvh_desc = std::get<0>(ref);
            auto transform = bvh_desc.transform;
            auto object_index = std::get<1>(ref);
            node.aabb_left_min_or_v0[0] = transform.m00;
            node.aabb_left_min_or_v0[1] = transform.m01;
            node.aabb_left_min_or_v0[2] = transform.m02;
            node.aabb_left_max_or_v1[0] = transform.m10;
            node.aabb_left_max_or_v1[1] = transform.m11;
            node.aabb_left_max_or_v1[2] = transform.m12;
            node.aabb_right_min_or_v2[0] = transform.m20;
            node.aabb_right_min_or_v2[1] = transform.m21;
            node.aabb_right_min_or_v2[2] = transform.m22;
            node.aabb_right_max[2] = transform.m30;
            node.aabb_right_max[2] = transform.m31;
            node.aabb_right_max[2] = transform.m32;
            node.mesh_id = object_index;
            node.prim_id = bvh_desc.root_offset;
        }
        
        // Check if the node is internal/leaf
        static bool IsInternal(BVHNode& node)
        {
            return node.addr_left != RR_INVALID_ID;
        }
        
        // Child index
        static std::uint32_t GetChildIndex(BVHNode& node, std::uint8_t idx)
        {
            return IsInternal(node)
            ? (idx == 0 ?
               node.addr_left
               : node.addr_right)
            : RR_INVALID_ID;
        }
    };
}
