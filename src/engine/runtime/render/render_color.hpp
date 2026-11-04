#pragma once

#include <cmath>

namespace engine::render_detail
{
// CPU equivalent of bgfx shaderlib.sh toLinearAccurate (BSD-2-Clause;
// third_party/notices/bgfx-hdr.md) for display-encoded clear colors.
inline float display_srgb_to_linear(float color)
{
    return color <= 0.04045F ? color / 12.92F
                             : std::pow((color + 0.055F) / 1.055F, 2.4F);
}
} // namespace engine::render_detail
