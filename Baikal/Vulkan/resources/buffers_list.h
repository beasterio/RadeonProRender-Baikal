#pragma once

#include "resource_list.h"

#include "VulkanBuffer.hpp"

class BuffersList : public ResourceList<vks::Buffer>
{
public:
    BuffersList(vks::VulkanDevice& dev) : ResourceList(dev) {};
    ~BuffersList() { }
};