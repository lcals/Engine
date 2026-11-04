#pragma once

#include "runtime/scene/scene_types.hpp"

#include <array>
#include <cstdint>

namespace engine
{

enum class FogMode : std::uint8_t
{
    none,
    linear,
    exponential,
    exponential_squared
};

// 组件组成的只读帧快照，不是场景写入接口或另一份持久状态。
struct RenderSettings
{
    // Generated scene data copied for this frame; not another persistent model.
    EnvironmentSettings atmosphere;
    std::uint64_t atmosphere_scene_id = 0;
    bool atmosphere_sun_valid = false;
    bool atmosphere_submitted = false;
    std::array<float, 3> atmosphere_sun_direction{0, -1, 0};
    std::array<float, 3> atmosphere_sun_radiance{};
    std::array<float, 3> ambient_color{1.0F, 1.0F, 1.0F};
    float ambient_intensity = 0.0F;
    bool ambient_tricolor = false;
    std::array<float, 3> ambient_sky_color{1.0F, 1.0F, 1.0F};
    std::array<float, 3> ambient_equator_color{1.0F, 1.0F, 1.0F};
    std::array<float, 3> ambient_ground_color{1.0F, 1.0F, 1.0F};
    FogMode fog_mode = FogMode::none;
    std::array<float, 3> fog_color{0.5F, 0.5F, 0.5F};
    float fog_start = 0.0F;
    float fog_end = 100.0F;
    float fog_density = 0.01F;
    float exposure = 1.0F;
    bool filmic_tonemap = false;
    bool ao_enabled = false;
    float ao_radius = 1.0F;
    float ao_strength = 1.0F;
    float ao_power = 1.5F;
    float ao_fade_start = 80.0F;
    float ao_fade_end = 150.0F;
    bool hdr_enabled = false;
    bool bloom_enabled = false;
    float bloom_threshold = 1.0F;
    float bloom_intensity = 0.15F;
    float bloom_radius = 1.0F;
    bool ssr_enabled = false;
    float ssr_resolution_scale = 0.5F;
    std::uint16_t ssr_max_steps = 64;
    float ssr_max_distance = 80.0F;
    float ssr_thickness = 0.15F;
    float ssr_roughness_cutoff = 0.7F;
    bool ibl_enabled = false;
    float ibl_intensity = 1.0F;
    bool directional_shadows = false;
    std::uint16_t shadow_resolution = 2048;
    float shadow_bias = 0.001F;
    std::uint8_t shadow_cascades = 4;
    float shadow_distance = 150.0F;
    float shadow_split_lambda = 0.6F;
    float shadow_cascade_blend = 0.1F;
    std::uint32_t clear_color = 0x181818ff;
};

} // namespace engine
