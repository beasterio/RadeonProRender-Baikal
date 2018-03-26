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

#include <math/float3.h>

#include <iostream>
#include <memory>
#include <limits>
#include <numeric>
#include <chrono>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <stack>
#include <xmmintrin.h>
#include <smmintrin.h>

#include "mesh.h"
#include "utils.h"
#include "bvh_utils.h"

#define PARALLEL_BUILD

namespace RadeonRays
{
    // BVH builder class
    // BVH builder relies on several traits classes in order to
    // receive the information about primitives BVH is being build on
    // (ObjectTraits), about the tree node layout (Node, NodeTraits),
    // and about operations happening on the tree after the build has
    // been done (PostProcessor). Allocator optionally provides the means
    // for aligned memory allocation.
    template <
        // Describes underlying primitive type
        typename ObjectTraits,
        // Node type for the BVH
        typename Node,
        // Describes Node type and node interface
        typename NodeTraits,
        // Post processing operations on the tree
        typename PostProcessor,
        // Memory allocation
        typename Allocator = aligned_allocator>
    class BVH
    {
        // Array of primitive references (essentially indices)
        using RefArray = std::vector<std::uint32_t>;
        // Array of objecs/subobjects to pass to BVH node encoder
        using MetaData = std::tuple<typename ObjectTraits::Type,
                                    std::size_t,
                                    std::size_t>;
        // Array of metadata objects
        using MetaDataArray = std::vector<MetaData>;
        
        // Node type
        enum class NodeType
        {
            kLeaf,
            kInternal
        };
        
    public:
        using NodeT = Node;
        
        // Build the tree out of a single object
        template <typename Object> void Build(Object const* obj)
        {
            std::vector<Object const*> object = {obj};
            Build(object.cbegin(), object.cend());
        }
        
        // Build from a range of objects
        template<typename Iter> void Build(Iter begin, Iter end)
        {
            // Clear previously stored data
            Clear();
            
            // Calculate number of subobjects (i.e. faces in the mesh)
            std::size_t num_subobjects = 0;
            for (auto i = begin; i != end; ++i)
            {
                num_subobjects += ObjectTraits::GetNumSubObjects(*i);
            }
            
            assert(num_subobjects > 0);
            
            // Allocate aligned AABB arrays and centroids array
            auto aabb_min = AllocAlignedFloat3Array(num_subobjects);
            auto aabb_max = AllocAlignedFloat3Array(num_subobjects);
            auto aabb_centroid = AllocAlignedFloat3Array(num_subobjects);
            
            // Allocate metadata array
            MetaDataArray metadata(num_subobjects);
            
#ifndef _DEBUG
#ifdef TEST1
            auto start = std::chrono::high_resolution_clock::now();
#endif
#endif
            // Keep track of scene AABB and scene centroids AABB
            AABB scene_aabb;
            AABB scene_centroid_aabb;
            
            // For each subobject calculate bounding box and
            // update scene AABB and centroids AABB
            std::size_t current_object = 0;
            std::size_t current_subobject = 0;
            // For each object in the range
            for (auto iter = begin; iter != end; ++iter, ++current_object)
            {
                // For each subobject in the object
                for (std::size_t subobject_index = 0;
                     subobject_index < ObjectTraits::GetNumSubObjects(*iter);
                     ++subobject_index, ++current_subobject)
                {
                    // Get subobject's AABB
                    auto aabb = ObjectTraits::GetAABB(*iter, subobject_index);
                    auto centroid = aabb.center();
                    
                    // Update scene and centroids AABBs
                    scene_aabb.grow(aabb);
                    scene_centroid_aabb.grow(centroid);
                    
                    // Store subobject's AABB in an aligned array
                    _mm_store_ps(&aabb_min[current_subobject].x, aabb.pmin);
                    _mm_store_ps(&aabb_max[current_subobject].x, aabb.pmax);
                    _mm_store_ps(&aabb_centroid[current_subobject].x, centroid);
                    
                    // Keep subobject in metadata array
                    // Metadata keeps object index, subobject index and an object pointer itself
                    // Builder passes this information into leaf encoding function later
                    metadata[current_subobject] = std::make_tuple(*iter, current_object, subobject_index);
                }
            }
            
#ifndef _DEBUG
#ifdef TEST1
            auto delta = std::chrono::high_resolution_clock::now() - start;
            std::cout << "AABB calculation time " << std::chrono::duration_cast<
            std::chrono::milliseconds>(delta).count() << " ms\n";
#endif
#endif
            
#ifndef _DEBUG
#ifdef TEST1
            start = std::chrono::high_resolution_clock::now();
#endif
#endif
            // Perform BVH build
            BuildImpl(scene_aabb,
                      scene_centroid_aabb,
                      aabb_min.get(),
                      aabb_max.get(),
                      aabb_centroid.get(),
                      metadata,
                      num_subobjects);
            
#ifndef _DEBUG
#ifdef TEST1
            delta = std::chrono::high_resolution_clock::now() - start;
            std::cout << "Pure build time " << std::chrono::duration_cast<
            std::chrono::milliseconds>(delta).count() << " ms\n";
#endif
#endif
            // Perform post-processing on the tree
            PostProcessor::Finalize(*this);
        }
        
        // Clear node data from previous builds
        void Clear()
        {
            for (auto i = 0u; i < num_nodes_; ++i)
            {
                nodes_[i].~Node();
            }
            
            Allocator::deallocate(nodes_);
            nodes_ = nullptr;
            num_nodes_ = 0;
        }
        
        // Access root node
        auto root() const
        {
            return &nodes_[0];
        }
        
        // Number of BVH nodes
        auto num_nodes() const
        {
            return num_nodes_;
        }
        
        // Access node by its index
        auto GetNode(std::size_t idx) const
        {
            return nodes_ + idx;
        }
        
    private:
        // Local stack depth
        static constexpr std::uint32_t kStackSize = 1024u;
        // Split requests for the builder
        struct SplitRequest
        {
            // AABB of the range
            AABB aabb;
            // AABB of the centroids in the range
            AABB centroid_aabb;
            // Starting index of the range
            std::size_t start_index;
            // Number of references in the range
            std::size_t num_refs;
            // Level of the split
            std::uint32_t level;
            // Index of the node
            std::uint32_t index;
        };
        
        // Handle split request on the range
        NodeType HandleRequest(SplitRequest const& request,
                               float3 const* aabb_min,
                               float3 const* aabb_max,
                               float3 const* aabb_centroid,
                               MetaDataArray const& metadata,
                               RefArray& refs,
                               SplitRequest& request_left,
                               SplitRequest& request_right)
        {
            // If we reached target primitive count for the leaf, encode a leaf and leave.
            if (request.num_refs <= NodeTraits::kMaxLeafPrimitives)
            {
                // Let encoder now we want a leaf with request.num_refs prims
                NodeTraits::EncodeLeaf(nodes_[request.index],
                                       static_cast<std::uint32_t>(request.num_refs));
                
                // Encode primitives
                for (auto i = 0u; i < request.num_refs; ++i)
                {
                    // Pass metadata to primitive encoding function
                    auto& face_data = metadata[refs[request.start_index + i]];
                    NodeTraits::SetPrimitive(nodes_[request.index], i, face_data);
                }
                
                return NodeType::kLeaf;
            }
            
            // Here we need to create an internal node
            // Start with median split
            auto split_axis = request.centroid_aabb.max_extent_axis();
            auto split_axis_extent = mm_select(request.centroid_aabb.extents(), split_axis);
            auto split_value = mm_select(request.centroid_aabb.center(), split_axis);
            auto split_idx = request.start_index;
            
            AABB left_aabb;
            AABB left_centroid_aabb;
            
            AABB right_aabb;
            AABB right_centroid_aabb;
            
            // Function to fetch AABB from memory and grow both AABB
            // and centroid AABB, used during partitioning phase to
            // precalculate bounds for the kids.
            auto load_and_grow = [&aabb_min, &aabb_max, &aabb_centroid](AABB& aabb,
                                                                        AABB& aabb_c,
                                                                        std::size_t index)
            {
                aabb.grow_min(_mm_load_ps(&aabb_min[index].x));
                aabb.grow_max(_mm_load_ps(&aabb_max[index].x));
                
                auto c = _mm_load_ps(&aabb_centroid[index].x);
                aabb_c.grow(c);
            };
            
            // If we have space to split
            if (split_axis_extent > 0.f)
            {
                // Check if we have enough primitives to apply SAH (otherwise use median split)
                if (request.num_refs > NodeTraits::kMinSAHPrimitives)
                {
                    // Try to find SAH split
                    switch (split_axis)
                    {
                        case 0:
                            split_value = FindSahSplit<0>(request,
                                                          aabb_min,
                                                          aabb_max,
                                                          aabb_centroid,
                                                          &refs[0]);
                            break;
                        case 1:
                            split_value = FindSahSplit<1>(request,
                                                          aabb_min,
                                                          aabb_max,
                                                          aabb_centroid,
                                                          &refs[0]);
                            break;
                        case 2:
                            split_value = FindSahSplit<2>(request,
                                                          aabb_min,
                                                          aabb_max,
                                                          aabb_centroid,
                                                          &refs[0]);
                            break;
                    }
                }
                
                // Now we have some split, so perform partition
                auto first = request.start_index;
                auto last = request.start_index + request.num_refs;
                
                while (1)
                {
                    // Go from the left
                    while (first != last &&
                           aabb_centroid[refs[first]][split_axis] < split_value)
                    {
                        load_and_grow(left_aabb, left_centroid_aabb, refs[first++]);
                    }
                    
                    // If we have reached end of range, bail out
                    if (first == last--) break;
                    
                    load_and_grow(right_aabb, right_centroid_aabb, refs[first]);
                    
                    // Go from the right
                    while (first != last &&
                           aabb_centroid[refs[last]][split_axis] >= split_value)
                    {
                        load_and_grow(right_aabb, right_centroid_aabb, refs[last--]);
                    }
                    
                    // If we have reached end of range, bail out
                    if (first == last) break;
                    
                    load_and_grow(left_aabb, left_centroid_aabb, refs[last]);
                    
                    std::swap(refs[first++], refs[last]);
                }
                
                // We have a split
                split_idx = first;
#ifdef _DEBUG
#ifdef TEST1
                {
                    for (auto i = request.start_index;
                         i < request.start_index + request.num_refs;
                         ++i)
                    {
                        if (i < split_idx)
                        {
                            assert(aabb_centroid[refs[i]][split_axis] <  split_value);
                        }
                        else
                        {
                            assert(aabb_centroid[refs[i]][split_axis] >= split_value);
                        }
                    }
                }
#endif
#endif
            }
            
            // If split is degenerate we apply median split
            if (split_idx == request.start_index ||
                split_idx == request.start_index + request.num_refs)
            {
                split_idx = request.start_index + (request.num_refs >> 1);
                
                left_aabb = AABB();
                right_aabb = AABB();
                left_centroid_aabb = AABB();
                right_centroid_aabb = AABB();
                
                // Calculate left part
                for (auto i = request.start_index; i < split_idx; ++i)
                {
                    load_and_grow(left_aabb, left_centroid_aabb, refs[i]);
                }
                
                // Calculate right part
                auto range_end = request.start_index + request.num_refs;
                for (auto i = split_idx; i < range_end; ++i)
                {
                    load_and_grow(right_aabb, right_centroid_aabb, refs[i]);
                }
            }
            
#ifdef _DEBUG
#ifdef TEST1
            {
                _MM_ALIGN16 bbox left, right, parent;
                _mm_store_ps(&left.pmin.x, lmin);
                _mm_store_ps(&left.pmax.x, lmax);
                _mm_store_ps(&right.pmin.x, rmin);
                _mm_store_ps(&right.pmax.x, rmax);
                _mm_store_ps(&parent.pmin.x, request.aabb_min);
                _mm_store_ps(&parent.pmax.x, request.aabb_max);
                
                assert(contains(parent, left));
                assert(contains(parent, right));
            }
#endif
#endif
            // Create a request for left child node
            request_left.aabb = left_aabb;
            request_left.centroid_aabb = left_centroid_aabb;
            request_left.start_index = request.start_index;
            request_left.num_refs = split_idx - request.start_index;
            request_left.level = request.level + 1;
            request_left.index = request.index + 1;
            
            // Create a request for right child node
            request_right.aabb = right_aabb;
            request_right.centroid_aabb = right_centroid_aabb;
            request_right.start_index = split_idx;
            request_right.num_refs = request.num_refs - request_left.num_refs;
            request_right.level = request.level + 1;
            request_right.index = static_cast<std::uint32_t>(request.index +
                                                             request_left.num_refs * 2);
            
            // Encode internal node
            NodeTraits::EncodeInternal(nodes_[request.index],
                                       request.aabb,
                                       request_left.index,
                                       request_right.index);
            
            return NodeType::kInternal;
        }
        
        // Actual build implementation
        void BuildImpl(AABB scene_aabb,
                       AABB scene_centroid_aabb,
                       float3 const* aabb_min,
                       float3 const* aabb_max,
                       float3 const* aabb_centroid,
                       MetaDataArray const& metadata,
                       std::size_t num_aabbs)
        {
            // Create refs array, which is going to be
            RefArray refs(num_aabbs);
            std::iota(refs.begin(), refs.end(), 0);
            
            // We use binary BVH, so we can preallocate the nodes
            num_nodes_ = num_aabbs * 2 - 1;
            nodes_ = reinterpret_cast<Node*>(Allocator::allocate(sizeof(Node) * num_nodes_,
                                             16u));
            
            for (auto i = 0u; i < num_nodes_; ++i)
            {
                new (&nodes_[i]) Node;
            }
            
#ifndef PARALLEL_BUILD
            // Local stack for split requests
            _MM_ALIGN16 SplitRequest requests[kStackSize];
            auto sptr = 0u;
            
            requests[sptr++] = SplitRequest
            {
                scene_aabb,
                scene_centroid_aabb,
                0,
                num_aabbs,
                0u,
                0u
            };
            
            // While we have requests in the stack
            while (sptr > 0)
            {
                // Pop next request
                auto request = requests[--sptr];
                
                // Preallocate space for left and right child requests (perf opt)
                auto& request_left{ requests[sptr++] };
                
                // Check for overflow
                if (sptr == kStackSize)
                {
                    throw std::runtime_error("Build stack overflow");
                }
                
                auto& request_right{ requests[sptr++] };
                
                
                if (sptr == kStackSize)
                {
                    throw std::runtime_error("Build stack overflow");
                }
                
                // Handle split request
                if (HandleRequest(request,
                                  aabb_min,
                                  aabb_max,
                                  aabb_centroid,
                                  metadata,
                                  refs,
                                  request_left,
                                  request_right) ==
                    NodeType::kLeaf)
                {
                    // Here we have leaf, so deallocate preallocated space back
                    --sptr;
                    --sptr;
                }
            }
#else
            
            
#ifdef PARALLEL_BUILD
            // Parallel build variables
            // Global requests stack
            std::stack<SplitRequest> requests;
            // Condition to wait on the global stack
            std::condition_variable cv;
            // Mutex to guard cv
            std::mutex mutex;
            // Indicates if we need to shutdown all the threads
            std::atomic_bool shutdown;
            // Number of primitives processed so far
            std::atomic_uint32_t num_refs_processed;
#endif
            // Initialize variables
            shutdown.store(false);
            num_refs_processed.store(0u);
            
            // Push root request
            requests.push(SplitRequest
            {
                scene_aabb,
                scene_centroid_aabb,
                0,
                num_aabbs,
                0u,
                0u
            });
            
            // Worker builder function
            auto worker_thread = [&]()
            {
                // Local stack for requests
                thread_local std::stack<SplitRequest> local_requests;
                
                // Thread loop
                while (1)
                {
                    {
                        // Wait on the global stack to receive a request
                        std::unique_lock<std::mutex> lock(mutex);
                        cv.wait(lock, [&]() { return !requests.empty() || shutdown; });
                        // If we have been awaken by shutdown, we need to leave asap.
                        if (shutdown) return;
                        
                        // Otherwise take a request from global stack and put it
                        // into our local stack
                        local_requests.push(requests.top());
                        requests.pop();
                    }
                    
                    // Allocated space for requests
                    _MM_ALIGN16 SplitRequest request_left;
                    _MM_ALIGN16 SplitRequest request_right;
                    _MM_ALIGN16 SplitRequest request;
                    
                    // Start handling local stack of requests
                    while (!local_requests.empty())
                    {
                        // Pop next request
                        request = local_requests.top();
                        local_requests.pop();
                        
                        // Handle it
                        auto node_type = HandleRequest(request,
                                                       aabb_min,
                                                       aabb_max,
                                                       aabb_centroid,
                                                       metadata,
                                                       refs,
                                                       request_left,
                                                       request_right);
                        
                        // If it is a leaf, update number of processed primitives
                        // and continue
                        if (node_type == NodeType::kLeaf)
                        {
                            num_refs_processed += static_cast<std::uint32_t>(request.num_refs);
                            continue;
                        }
                        
                        // Here we know we have just built and internal node,
                        // so we are going to handle its left child on this thread and
                        // its right child on:
                        // - this thread if it is small
                        // - another thread if it is huge (since this one is going to handle left child)
                        if (request_right.num_refs > 4096u)
                        {
                            // Put request into the global queue
                            std::unique_lock<std::mutex> lock(mutex);
                            requests.push(request_right);
                            // Wake up one of the workers
                            cv.notify_one();
                        }
                        else
                        {
                            // Put small request into the local queue
                            local_requests.push(request_right);
                        }
                        
                        // Put left request to local stack (always handled on this thread)
                        local_requests.push(request_left);
                    }
                }
            };
            
            // Luch several threads
            auto num_threads = std::thread::hardware_concurrency();
            std::vector<std::thread> threads(num_threads);
            
            for (auto i = 0u; i < num_threads; ++i)
            {
                threads[i] = std::thread(worker_thread);
            }
            
            // Wait until all primitives are handled
            while (num_refs_processed != num_aabbs)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
            
            // Signal shutdown and wake up all the threads
            shutdown.store(true);
            cv.notify_all();
            
            // Wait for all the threads to finish
            for (auto i = 0u; i < num_threads; ++i)
            {
                threads[i].join();
            }
#endif
        }
        
        // Find SAH split for a range of primitives
        template <std::uint32_t axis> float FindSahSplit(SplitRequest const& request,
                                                         float3 const* aabb_min,
                                                         float3 const* aabb_max,
                                                         float3 const* aabb_centroid,
                                                         std::uint32_t const* refs)
        {
            // Start with high SAH value
            auto sah = std::numeric_limits<float>::max();
            
            // Use 64 bins to evaluate SAH
            auto constexpr kNumBins = 128u;
            SIMDVec4 bin_min[kNumBins];
            SIMDVec4 bin_max[kNumBins];
            std::uint32_t bin_count[kNumBins];
            
            // Initilize bins
            auto constexpr inf = std::numeric_limits<float>::infinity();
            for (auto i = 0u; i < kNumBins; ++i)
            {
                bin_count[i] = 0;
                bin_min[i] = _mm_set_ps(inf, inf, inf, inf);
                bin_max[i] = _mm_set_ps(-inf, -inf, -inf, -inf);
            }
            
            // Precalculate constants
            auto centroid_extent = request.centroid_aabb.extents();
            auto centroid_min = _mm_shuffle_ps(request.centroid_aabb.pmin,
                                               request.centroid_aabb.pmin,
                                               _MM_SHUFFLE(axis, axis, axis, axis));
            centroid_extent = _mm_shuffle_ps(centroid_extent,
                                             centroid_extent,
                                             _MM_SHUFFLE(axis, axis, axis, axis));
            auto centroid_extent_inv = _mm_rcp_ps(centroid_extent);
            auto area_inv = mm_select(_mm_rcp_ps(request.aabb.surface_area()), 0);
            
            // Determine the number of 4-prims to handle on SIMD
            auto full4 = request.num_refs & ~0x3;
            auto num_bins = _mm_set_ps((float)kNumBins,
                                       (float)kNumBins,
                                       (float)kNumBins,
                                       (float)kNumBins);
            
            // Start binning
            auto end_range = request.start_index + full4;
            for (auto i = request.start_index; i < end_range; i += 4u)
            {
                // Fetch 4 indices
                auto idx0 = refs[i];
                auto idx1 = refs[i + 1];
                auto idx2 = refs[i + 2];
                auto idx3 = refs[i + 3];

                // Fetch 4 centroids
                auto c = _mm_set_ps(aabb_centroid[idx3][axis],
                                    aabb_centroid[idx2][axis],
                                    aabb_centroid[idx1][axis],
                                    aabb_centroid[idx0][axis]);

                // Bin primitive
                auto bin_idx = _mm_mul_ps(_mm_mul_ps(_mm_sub_ps(c, centroid_min),
                                                     centroid_extent_inv), num_bins);

                // Extract bin indices
                auto bin_idx0 = std::min(static_cast<uint32_t>(mm_select(bin_idx, 0u)), kNumBins - 1);
                auto bin_idx1 = std::min(static_cast<uint32_t>(mm_select(bin_idx, 1u)), kNumBins - 1);
                auto bin_idx2 = std::min(static_cast<uint32_t>(mm_select(bin_idx, 2u)), kNumBins - 1);
                auto bin_idx3 = std::min(static_cast<uint32_t>(mm_select(bin_idx, 3u)), kNumBins - 1);
                
#ifdef _DEBUG
#ifdef TEST1
                assert(bin_idx0 >= 0u); assert(bin_idx0 < kNumBins);
                assert(bin_idx1 >= 0u); assert(bin_idx1 < kNumBins);
                assert(bin_idx3 >= 0u); assert(bin_idx2 < kNumBins);
                assert(bin_idx3 >= 0u); assert(bin_idx3 < kNumBins);
#endif
#endif
                
                // Add to bin histograms
                ++bin_count[bin_idx0];
                ++bin_count[bin_idx1];
                ++bin_count[bin_idx2];
                ++bin_count[bin_idx3];
                
                // Update bin AABBs
                bin_min[bin_idx0] = _mm_min_ps(bin_min[bin_idx0],
                                               _mm_load_ps(&aabb_min[idx0].x));
                bin_max[bin_idx0] = _mm_max_ps(bin_max[bin_idx0],
                                               _mm_load_ps(&aabb_max[idx0].x));
                bin_min[bin_idx1] = _mm_min_ps(bin_min[bin_idx1],
                                               _mm_load_ps(&aabb_min[idx1].x));
                bin_max[bin_idx1] = _mm_max_ps(bin_max[bin_idx1],
                                               _mm_load_ps(&aabb_max[idx1].x));
                bin_min[bin_idx2] = _mm_min_ps(bin_min[bin_idx2],
                                               _mm_load_ps(&aabb_min[idx2].x));
                bin_max[bin_idx2] = _mm_max_ps(bin_max[bin_idx2],
                                               _mm_load_ps(&aabb_max[idx2].x));
                bin_min[bin_idx3] = _mm_min_ps(bin_min[bin_idx3],
                                               _mm_load_ps(&aabb_min[idx3].x));
                bin_max[bin_idx3] = _mm_max_ps(bin_max[bin_idx3],
                                               _mm_load_ps(&aabb_max[idx3].x));
            }
            
            // Bin the rest (up to 3 prims)
            auto cm = mm_select(centroid_min, 0u);
            auto cei = mm_select(centroid_extent_inv, 0u);
            
            auto start_range = request.start_index + full4;
            end_range = request.start_index + request.num_refs;
            for (auto i = start_range; i < end_range; ++i)
            {
                auto idx = refs[i];
                
                auto bin_idx = std::min(static_cast<uint32_t>(kNumBins *
                                                              (aabb_centroid[idx][axis] - cm) *
                                                              cei), kNumBins - 1);
                ++bin_count[bin_idx];
                
                bin_min[bin_idx] = _mm_min_ps(bin_min[bin_idx],
                                              _mm_load_ps(&aabb_min[idx].x));
                bin_max[bin_idx] = _mm_max_ps(bin_max[bin_idx],
                                              _mm_load_ps(&aabb_max[idx].x));
            }
            
#ifdef _DEBUG
#ifdef TEST1
            auto num_refs = request.num_refs;
            for (auto i = 0u; i < kNumBins; ++i)
            {
                num_refs -= bin_count[i];
            }
            
            assert(num_refs == 0);
#endif
#endif
            // Sweep
            SIMDVec4 right_min[kNumBins - 1];
            SIMDVec4 right_max[kNumBins - 1];
            AABB tmp_aabb;

            // Precalculate right AABBs for each bin
            for (auto i = kNumBins - 1; i > 0; --i)
            {
                tmp_aabb.grow_min(bin_min[i]);
                tmp_aabb.grow_max(bin_max[i]);
                
                right_min[i - 1] = tmp_aabb.pmin;
                right_max[i - 1] = tmp_aabb.pmax;
            }

            // Sweep from the left (all refs to the right)
            tmp_aabb = AABB();
            auto  lc = 0u;
            auto  rc = request.num_refs;

            auto split_idx = -1;
            // Each iteration advances split plane to the right
            for (auto i = 0u; i < kNumBins - 1; ++i)
            {
                // Extend left AABB
                tmp_aabb.grow_min(bin_min[i]);
                tmp_aabb.grow_max(bin_max[i]);

                lc += bin_count[i];
                rc -= bin_count[i];

                // Evaluate SAH
                auto lsa = mm_select(tmp_aabb.surface_area(), 0);
                auto rsa = mm_select(AABB(right_min[i], right_max[i]).surface_area(), 0);
                auto s = static_cast<float>(NodeTraits::kTraversalCost) +
                         (lc * lsa + rc * rsa) * area_inv;
                
                // Update split if needed
                if (s < sah)
                {
                    split_idx = i;
                    sah = s;
                }
            }
            
            // Return split plane
            return mm_select(centroid_min, 0u) +
                   (split_idx + 1) * (mm_select(centroid_extent, 0u) / kNumBins);
        }
        
        // Allocate num_elements float3s
        auto AllocAlignedFloat3Array(std::size_t num_elements)
        {
            auto deleter = [](void* p) { Allocator::deallocate(p); };
            
            using AlignedFloat3Ptr = std::unique_ptr<float3[], decltype(deleter)>;
            
            return AlignedFloat3Ptr(reinterpret_cast<float3*>(
                                    Allocator::allocate(sizeof(float3) * num_elements, 16u)),
                                                        deleter);
        }
        
        // BVH nodes
        Node* nodes_ = nullptr;
        // Number of BVH nodes
        std::size_t num_nodes_ = 0;
    };
}
