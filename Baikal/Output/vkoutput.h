#pragma once

#include "output.h"
#include "CLW.h"

namespace Baikal
{
    class VkOutput : public Output
    {
    public:
        VkOutput(std::uint32_t w, std::uint32_t h)
            : Output(w, h)
        {
        }

        void GetData(RadeonRays::float3* data) const override
        {
        }

    private:
    };
}
