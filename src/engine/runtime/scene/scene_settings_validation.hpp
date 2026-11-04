#pragma once

#include "runtime/core/guid_map.hpp"
#include "runtime/scene/scene_types.hpp"

#include <cmath>

namespace engine::scene_detail
{

[[nodiscard]] inline bool valid_color(const Color& color) noexcept
{
    const auto channel = [](float value)
    { return std::isfinite(value) && value >= 0.0F && value <= 1.0F; };
    return channel(color.r()) && channel(color.g()) && channel(color.b()) && channel(color.a());
}

[[nodiscard]] inline bool valid_environment(const EnvironmentSettings& settings) noexcept
{
    const auto bounded = [](double value, double low, double high)
    { return std::isfinite(value) && value >= low && value <= high; };
    const auto vector = [&](const Vec3& value, double high)
    {
        return bounded(value.x(), 0, high) && bounded(value.y(), 0, high) &&
               bounded(value.z(), 0, high);
    };
    // Units follow bgfx 53-sky2: all atmospheric distances are km, coefficients km^-1.
    // Finite Earth-like bounds protect the LUT mapping from degenerate media/radii.
    if ((settings.atmosphere_enabled && settings.atmosphere_sun_node_id == 0) ||
        !bounded(settings.atmosphere_ground_height, -10000000, 10000000) ||
        !vector(settings.atmosphere_rayleigh_scattering, 1) ||
        !bounded(settings.atmosphere_rayleigh_scale_height, 0.001, 1000) ||
        !bounded(settings.atmosphere_mie_scattering, 0, 1) ||
        !bounded(settings.atmosphere_mie_absorption, 0, 1) ||
        !bounded(settings.atmosphere_mie_scale_height, 0.001, 1000) ||
        !bounded(settings.atmosphere_mie_anisotropy, 0, 0.99F) ||
        !vector(settings.atmosphere_ozone_absorption, 1) ||
        !bounded(settings.atmosphere_ozone_center_height, 0, 1000) ||
        !bounded(settings.atmosphere_ozone_half_width, 0.001, 1000) ||
        !bounded(settings.atmosphere_planet_radius, 100, 100000) ||
        !bounded(settings.atmosphere_height, 1, 1000) ||
        settings.atmosphere_ozone_center_height - settings.atmosphere_ozone_half_width >=
            settings.atmosphere_height ||
        !vector(settings.atmosphere_ground_albedo, 1) ||
        !bounded(settings.atmosphere_multiscatter_strength, 0, 5))
    {
        return false;
    }
    const bool no_ibl_resources = settings.ibl_irradiance_guid.empty() &&
                                  settings.ibl_prefilter_guid.empty() &&
                                  settings.ibl_lut_guid.empty();
    const bool complete_ibl_resources = GuidMap::valid_guid(settings.ibl_irradiance_guid) &&
                                        GuidMap::valid_guid(settings.ibl_prefilter_guid) &&
                                        GuidMap::valid_guid(settings.ibl_lut_guid);
    return valid_color(settings.ambient_color) && valid_color(settings.ambient_sky_color) &&
           valid_color(settings.ambient_equator_color) &&
           valid_color(settings.ambient_ground_color) && valid_color(settings.fog_color) &&
           std::isfinite(settings.ambient_intensity) && settings.ambient_intensity >= 0.0F &&
           settings.ambient_intensity <= 1000.0F && settings.fog_mode >= schema::FogMode::MIN &&
           settings.fog_mode <= schema::FogMode::MAX && std::isfinite(settings.fog_start) &&
           settings.fog_start >= 0.0F && std::isfinite(settings.fog_end) &&
           settings.fog_end > settings.fog_start && std::isfinite(settings.fog_density) &&
           settings.fog_density >= 0.0F && std::isfinite(settings.ibl_intensity) &&
           settings.ibl_intensity >= 0.0F && settings.ibl_intensity <= 1000.0F &&
           (no_ibl_resources || complete_ibl_resources);
}

[[nodiscard]] inline bool valid_post_process(const PostProcessSettings& settings) noexcept
{
    return std::isfinite(settings.exposure) && settings.exposure > 0.0F &&
           settings.exposure <= 100.0F && std::isfinite(settings.ao_radius) &&
           settings.ao_radius > 0.0F && settings.ao_radius <= 100.0F &&
           std::isfinite(settings.ao_strength) && settings.ao_strength >= 0.0F &&
           settings.ao_strength <= 2.0F && std::isfinite(settings.ao_power) &&
           settings.ao_power >= 0.1F && settings.ao_power <= 8.0F &&
           std::isfinite(settings.ao_fade_start) && settings.ao_fade_start >= 0.0F &&
           std::isfinite(settings.ao_fade_end) && settings.ao_fade_end > settings.ao_fade_start &&
           settings.ao_fade_end <= 100000.0F && (!settings.bloom_enabled || settings.hdr_enabled) &&
           std::isfinite(settings.bloom_threshold) && settings.bloom_threshold >= 0.0F &&
           settings.bloom_threshold <= 100.0F && std::isfinite(settings.bloom_intensity) &&
           settings.bloom_intensity >= 0.0F && settings.bloom_intensity <= 10.0F &&
           std::isfinite(settings.bloom_radius) && settings.bloom_radius >= 0.25F &&
           settings.bloom_radius <= 4.0F && std::isfinite(settings.ssr_resolution_scale) &&
           settings.ssr_resolution_scale >= 0.25F && settings.ssr_resolution_scale <= 1.0F &&
           settings.ssr_max_steps >= 8 && settings.ssr_max_steps <= 256 &&
           std::isfinite(settings.ssr_max_distance) && settings.ssr_max_distance > 0.0F &&
           settings.ssr_max_distance <= 100000.0F && std::isfinite(settings.ssr_thickness) &&
           settings.ssr_thickness > 0.0F && settings.ssr_thickness <= 10.0F &&
           std::isfinite(settings.ssr_roughness_cutoff) && settings.ssr_roughness_cutoff >= 0.0F &&
           settings.ssr_roughness_cutoff <= 1.0F;
}

[[nodiscard]] inline bool valid_planar_reflection(const PlanarReflectionSettings& settings) noexcept
{
    return std::isfinite(settings.resolution_scale) && settings.resolution_scale >= 0.25F &&
           settings.resolution_scale <= 1.0F && std::isfinite(settings.clip_bias) &&
           settings.clip_bias >= 0.0F && settings.clip_bias <= 1.0F;
}

[[nodiscard]] inline bool valid_light(const LightSettings& settings) noexcept
{
    return valid_color(settings.color) && std::isfinite(settings.intensity) &&
           settings.intensity >= 0.0F && settings.kind >= schema::LightKind::MIN &&
           settings.kind <= schema::LightKind::MAX && std::isfinite(settings.range) &&
           settings.range > 0.0F && std::isfinite(settings.inner_angle) &&
           std::isfinite(settings.outer_angle) && settings.inner_angle >= 0.0F &&
           settings.inner_angle < settings.outer_angle && settings.outer_angle < 90.0F &&
           settings.shadow_resolution >= 128 && settings.shadow_resolution <= 8192 &&
           (settings.shadow_resolution & (settings.shadow_resolution - 1)) == 0 &&
           std::isfinite(settings.shadow_bias) && settings.shadow_bias >= 0.0F &&
           settings.shadow_bias <= 0.1F &&
           (settings.shadow_cascades == 1 || settings.shadow_cascades == 2 ||
            settings.shadow_cascades == 4) &&
           std::isfinite(settings.shadow_distance) && settings.shadow_distance > 0.0F &&
           settings.shadow_distance <= 100000.0F && std::isfinite(settings.shadow_split_lambda) &&
           settings.shadow_split_lambda >= 0.0F && settings.shadow_split_lambda <= 1.0F &&
           std::isfinite(settings.shadow_cascade_blend) && settings.shadow_cascade_blend >= 0.0F &&
           settings.shadow_cascade_blend <= 0.3F;
}

} // namespace engine::scene_detail
