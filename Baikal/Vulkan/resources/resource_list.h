#pragma once

#include "vulkan/vulkan.h"

#include "VulkanDevice.hpp"

#include <unordered_map>

namespace
{
    const char* g_asset_path = "../Resources/";
}
template <typename T>
class ResourceList
{
public:
    ResourceList(vks::VulkanDevice& dev) : _device(dev) {};

    inline const void Set(uint32_t name, T resource) {
        _resources[name] = resource;
    }
    
    inline T Get(uint32_t name) {
        return _resources[name];
    }
    
    inline bool Present(uint32_t name) const {
        return _resources.find(name) != _resources.end();
    }

    inline size_t GetNumResources() const {
        return _resources.size(); 
    }

protected:
    vks::VulkanDevice& _device;
    std::unordered_map<uint32_t, T> _resources;
};