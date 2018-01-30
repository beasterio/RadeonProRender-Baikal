#pragma once

#include "math/float3.h"
#include "SceneGraph/scene1.h"
#include "radeon_rays.h"
#include "SceneGraph/Collector/collector.h"

#include "vulkan.h"


namespace Baikal
{
    using namespace RadeonRays;

    struct VkScene
    {

        std::unique_ptr<Bundle> material_bundle;
        std::unique_ptr<Bundle> volume_bundle;
        std::unique_ptr<Bundle> texture_bundle;
    };
}
