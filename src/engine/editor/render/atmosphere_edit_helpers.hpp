#pragma once

#include "editor/commands/components/component_edit.hpp"

#include <array>
#include <charconv>

namespace engine::editor::render_edit_detail
{
struct AtmosphereFloat
{
    const char* name;
    float EnvironmentSettings::* member;
};
inline constexpr std::array atmosphere_floats{
    AtmosphereFloat{"atmosphereGroundHeight", &EnvironmentSettings::atmosphere_ground_height},
    AtmosphereFloat{"atmosphereRayleighScaleHeight",
                    &EnvironmentSettings::atmosphere_rayleigh_scale_height},
    AtmosphereFloat{"atmosphereMieScattering", &EnvironmentSettings::atmosphere_mie_scattering},
    AtmosphereFloat{"atmosphereMieAbsorption", &EnvironmentSettings::atmosphere_mie_absorption},
    AtmosphereFloat{"atmosphereMieScaleHeight", &EnvironmentSettings::atmosphere_mie_scale_height},
    AtmosphereFloat{"atmosphereMieAnisotropy", &EnvironmentSettings::atmosphere_mie_anisotropy},
    AtmosphereFloat{"atmosphereOzoneCenterHeight",
                    &EnvironmentSettings::atmosphere_ozone_center_height},
    AtmosphereFloat{"atmosphereOzoneHalfWidth", &EnvironmentSettings::atmosphere_ozone_half_width},
    AtmosphereFloat{"atmospherePlanetRadius", &EnvironmentSettings::atmosphere_planet_radius},
    AtmosphereFloat{"atmosphereHeight", &EnvironmentSettings::atmosphere_height},
    AtmosphereFloat{"atmosphereMultiscatterStrength",
                    &EnvironmentSettings::atmosphere_multiscatter_strength}};

struct AtmosphereVector
{
    const char* name;
    Vec3 EnvironmentSettings::* member;
};
inline constexpr std::array atmosphere_vectors{
    AtmosphereVector{"atmosphereRayleighScattering",
                     &EnvironmentSettings::atmosphere_rayleigh_scattering},
    AtmosphereVector{"atmosphereOzoneAbsorption",
                     &EnvironmentSettings::atmosphere_ozone_absorption},
    AtmosphereVector{"atmosphereGroundAlbedo", &EnvironmentSettings::atmosphere_ground_albedo}};

inline void append_atmosphere(const EnvironmentSettings& settings, nlohmann::json& result)
{
    result["atmosphereEnabled"] = settings.atmosphere_enabled;
    result["atmosphereSunNodeId"] = std::to_string(settings.atmosphere_sun_node_id);
    for (const auto& field : atmosphere_floats)
    {
        result[field.name] = settings.*field.member;
    }
    for (const auto& field : atmosphere_vectors)
    {
        const auto& value = settings.*field.member;
        result[field.name] = {value.x(), value.y(), value.z()};
    }
}

inline bool read_atmosphere(std::string_view name, const nlohmann::json& value,
                            EnvironmentSettings& settings)
{
    if (name == "atmosphereEnabled")
    {
        if (!value.is_boolean())
        {
            return false;
        }
        settings.atmosphere_enabled = value.get<bool>();
        return true;
    }
    if (name == "atmosphereSunNodeId")
    {
        if (!value.is_string())
        {
            return false;
        }
        const auto& text = value.get_ref<const std::string&>();
        std::uint64_t id = 0;
        const auto parsed = std::from_chars(text.data(), text.data() + text.size(), id);
        if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() ||
            text != std::to_string(id))
        {
            return false;
        }
        settings.atmosphere_sun_node_id = id;
        return true;
    }
    for (const auto& field : atmosphere_floats)
    {
        if (name == field.name)
        {
            return components::number(value, settings.*field.member);
        }
    }
    for (const auto& field : atmosphere_vectors)
    {
        if (name == field.name)
        {
            return components::vector(value, settings.*field.member);
        }
    }
    return false;
}
} // namespace engine::editor::render_edit_detail
