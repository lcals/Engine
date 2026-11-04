#pragma once

#include "runtime/render/render_features.hpp"

#include <array>
#include <string_view>

namespace engine::render_detail
{
inline constexpr std::array<std::string_view, static_cast<std::size_t>(RenderFeature::count)>
    feature_names{"frustum_culling", "static_instancing", "static_lod",    "wireframe",
                  "debug_draw",      "mesh_bounds",       "camera_frusta", "stats"};

inline bool parse_feature(std::string_view name, RenderFeature& output) noexcept
{
    for (std::size_t index = 0; index < feature_names.size(); ++index)
    {
        if (feature_names[index] == name)
        {
            output = static_cast<RenderFeature>(index);
            return true;
        }
    }
    return false;
}

inline std::string_view feature_reason_name(RenderFeatureReason reason) noexcept
{
    constexpr std::array names{"none",     "device_unavailable",  "device_unsupported",
                               "no_scene", "no_applicable_draws", "resources_unavailable"};
    const auto index = static_cast<std::size_t>(reason);
    return index < names.size() ? names[index] : "device_unsupported";
}
} // namespace engine::render_detail
