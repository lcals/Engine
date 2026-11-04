#include "editor/asset_pipeline/components/environment_codec.hpp"

#include "editor/asset_pipeline/toml_helpers.hpp"
#include "runtime/scene/scene_settings_validation.hpp"

#include <array>
#include <limits>
#include <utility>

namespace engine::editor::asset_pipeline
{
namespace
{

bool read_color(const toml::node* source, Color& output)
{
    const auto* array = source == nullptr ? nullptr : source->as_array();
    if (array == nullptr || (array->size() != 3 && array->size() != 4))
    {
        return false;
    }
    std::array<float, 4> channels{0.0F, 0.0F, 0.0F, 1.0F};
    for (std::size_t index = 0; index < array->size(); ++index)
    {
        if (!float_number(array->get(index), channels[index]) || channels[index] < 0.0F ||
            channels[index] > 1.0F)
        {
            return false;
        }
    }
    output = Color(channels[0], channels[1], channels[2], channels[3]);
    return true;
}

toml::array write_color(const Color& color)
{
    return toml::array{color.r(), color.g(), color.b(), color.a()};
}

} // namespace

schema::ComponentPayload EnvironmentCodec::type() const noexcept
{
    return schema::ComponentPayload::EnvironmentData;
}

std::string_view EnvironmentCodec::name() const noexcept
{
    return "environment";
}

bool EnvironmentCodec::validate(const schema::ComponentDataT& component, std::string& error) const
{
    const auto* settings = component.payload.AsEnvironmentData();
    if (settings == nullptr || !scene_detail::valid_environment(*settings))
    {
        error = "invalid scene environment";
        return false;
    }
    return true;
}

bool EnvironmentCodec::read(const toml::table& table, schema::ComponentDataT& component,
                            std::string& error) const
{
    EnvironmentSettings settings;
    if ((table.contains("ambient_color") &&
         !read_color(table.get("ambient_color"), settings.ambient_color)) ||
        (table.contains("ambient_sky_color") &&
         !read_color(table.get("ambient_sky_color"), settings.ambient_sky_color)) ||
        (table.contains("ambient_equator_color") &&
         !read_color(table.get("ambient_equator_color"), settings.ambient_equator_color)) ||
        (table.contains("ambient_ground_color") &&
         !read_color(table.get("ambient_ground_color"), settings.ambient_ground_color)) ||
        (table.contains("fog_color") && !read_color(table.get("fog_color"), settings.fog_color)))
    {
        error = "environment colors require three or four channels in 0..1";
        return false;
    }
    if ((table.contains("ambient_intensity") &&
         !float_number(table.get("ambient_intensity"), settings.ambient_intensity)) ||
        (table.contains("fog_start") &&
         !float_number(table.get("fog_start"), settings.fog_start)) ||
        (table.contains("fog_end") && !float_number(table.get("fog_end"), settings.fog_end)) ||
        (table.contains("fog_density") &&
         !float_number(table.get("fog_density"), settings.fog_density)) ||
        (table.contains("ibl_intensity") &&
         !float_number(table.get("ibl_intensity"), settings.ibl_intensity)))
    {
        error = "environment numeric fields must be finite numbers";
        return false;
    }
    if (table.contains("ibl_enabled"))
    {
        const auto enabled = table["ibl_enabled"].value_exact<bool>();
        if (!enabled)
        {
            error = "environment ibl_enabled must be boolean";
            return false;
        }
        settings.ibl_enabled = *enabled;
    }
    const std::array references = {std::pair{"ibl_irradiance", &settings.ibl_irradiance_guid},
                                   std::pair{"ibl_prefilter", &settings.ibl_prefilter_guid},
                                   std::pair{"ibl_lut", &settings.ibl_lut_guid}};
    for (const auto& [key, guid] : references)
    {
        if (table.contains(key))
        {
            const auto parsed = table[key].value<std::string>();
            if (!parsed)
            {
                error = "environment IBL references must be GUID strings";
                return false;
            }
            *guid = *parsed;
        }
    }
    if (table.contains("ambient_tricolor"))
    {
        const auto enabled = table["ambient_tricolor"].value_exact<bool>();
        if (!enabled)
        {
            error = "environment ambient_tricolor must be boolean";
            return false;
        }
        settings.ambient_tricolor = *enabled;
    }
    if (table.contains("fog_mode"))
    {
        const auto mode = table["fog_mode"].value<std::string_view>();
        if (mode == "none")
        {
            settings.fog_mode = schema::FogMode::none;
        }
        else if (mode == "linear")
        {
            settings.fog_mode = schema::FogMode::linear;
        }
        else if (mode == "exponential")
        {
            settings.fog_mode = schema::FogMode::exponential;
        }
        else if (mode == "exponential_squared")
        {
            settings.fog_mode = schema::FogMode::exponential_squared;
        }
        else
        {
            error = "environment fog_mode must be none, linear, exponential or exponential_squared";
            return false;
        }
    }
    if (table.contains("clear_color"))
    {
        const auto value = table["clear_color"].value_exact<std::int64_t>();
        if (!value || *value < 0 || *value > std::numeric_limits<std::uint32_t>::max())
        {
            error = "environment clear_color must be an unsigned 32-bit RGBA integer";
            return false;
        }
        settings.clear_color = static_cast<std::uint32_t>(*value);
    }
    const std::array atmosphere_numbers = {
        std::pair{"atmosphere_ground_height", &settings.atmosphere_ground_height},
        std::pair{"atmosphere_rayleigh_scale_height", &settings.atmosphere_rayleigh_scale_height},
        std::pair{"atmosphere_mie_scattering", &settings.atmosphere_mie_scattering},
        std::pair{"atmosphere_mie_absorption", &settings.atmosphere_mie_absorption},
        std::pair{"atmosphere_mie_scale_height", &settings.atmosphere_mie_scale_height},
        std::pair{"atmosphere_mie_anisotropy", &settings.atmosphere_mie_anisotropy},
        std::pair{"atmosphere_ozone_center_height", &settings.atmosphere_ozone_center_height},
        std::pair{"atmosphere_ozone_half_width", &settings.atmosphere_ozone_half_width},
        std::pair{"atmosphere_planet_radius", &settings.atmosphere_planet_radius},
        std::pair{"atmosphere_height", &settings.atmosphere_height},
        std::pair{"atmosphere_multiscatter_strength", &settings.atmosphere_multiscatter_strength}};
    for (const auto& [key, value] : atmosphere_numbers)
    {
        if (table.contains(key) && !float_number(table.get(key), *value))
        {
            error = "atmosphere numeric fields must be finite numbers";
            return false;
        }
    }
    const std::array atmosphere_vectors = {
        std::pair{"atmosphere_rayleigh_scattering", &settings.atmosphere_rayleigh_scattering},
        std::pair{"atmosphere_ozone_absorption", &settings.atmosphere_ozone_absorption},
        std::pair{"atmosphere_ground_albedo", &settings.atmosphere_ground_albedo}};
    for (const auto& [key, value] : atmosphere_vectors)
    {
        std::array<double, 3> channels{};
        if (table.contains(key))
        {
            if (!numbers(table.get(key), channels))
            {
                error = "atmosphere vectors require three finite numbers";
                return false;
            }
            *value = Vec3(channels[0], channels[1], channels[2]);
        }
    }
    if (table.contains("atmosphere_enabled"))
    {
        const auto enabled = table["atmosphere_enabled"].value_exact<bool>();
        if (!enabled)
        {
            error = "atmosphere_enabled must be boolean";
            return false;
        }
        settings.atmosphere_enabled = *enabled;
    }
    if (table.contains("atmosphere_sun_node_id") &&
        !identifier(table.get("atmosphere_sun_node_id"), settings.atmosphere_sun_node_id))
    {
        error = "atmosphere_sun_node_id must be an unsigned node identity";
        return false;
    }
    if (!scene_detail::valid_environment(settings))
    {
        error = "environment intensity, fog range or complete IBL resource references are invalid";
        return false;
    }
    component.payload.Set(std::move(settings));
    return true;
}

std::span<const std::string_view> EnvironmentCodec::resource_fields() const noexcept
{
    static constexpr std::string_view fields[]{"ibl_irradiance", "ibl_prefilter", "ibl_lut"};
    return fields;
}

void EnvironmentCodec::write(const schema::ComponentDataT& component, toml::table& table) const
{
    const auto& settings = *component.payload.AsEnvironmentData();
    table.insert("type", "environment");
    table.insert("ambient_color", write_color(settings.ambient_color));
    table.insert("ambient_intensity", settings.ambient_intensity);
    table.insert("ambient_tricolor", settings.ambient_tricolor);
    table.insert("ambient_sky_color", write_color(settings.ambient_sky_color));
    table.insert("ambient_equator_color", write_color(settings.ambient_equator_color));
    table.insert("ambient_ground_color", write_color(settings.ambient_ground_color));
    table.insert("fog_mode", schema::EnumNameFogMode(settings.fog_mode));
    table.insert("fog_color", write_color(settings.fog_color));
    table.insert("fog_start", settings.fog_start);
    table.insert("fog_end", settings.fog_end);
    table.insert("fog_density", settings.fog_density);
    table.insert("clear_color", settings.clear_color);
    table.insert("ibl_enabled", settings.ibl_enabled);
    table.insert("ibl_intensity", settings.ibl_intensity);
    table.insert("ibl_irradiance", settings.ibl_irradiance_guid);
    table.insert("ibl_prefilter", settings.ibl_prefilter_guid);
    table.insert("ibl_lut", settings.ibl_lut_guid);
    table.insert("atmosphere_enabled", settings.atmosphere_enabled);
    table.insert("atmosphere_sun_node_id", std::to_string(settings.atmosphere_sun_node_id));
    table.insert("atmosphere_ground_height", settings.atmosphere_ground_height);
    table.insert("atmosphere_rayleigh_scale_height", settings.atmosphere_rayleigh_scale_height);
    table.insert("atmosphere_mie_scattering", settings.atmosphere_mie_scattering);
    table.insert("atmosphere_mie_absorption", settings.atmosphere_mie_absorption);
    table.insert("atmosphere_mie_scale_height", settings.atmosphere_mie_scale_height);
    table.insert("atmosphere_mie_anisotropy", settings.atmosphere_mie_anisotropy);
    table.insert("atmosphere_ozone_center_height", settings.atmosphere_ozone_center_height);
    table.insert("atmosphere_ozone_half_width", settings.atmosphere_ozone_half_width);
    table.insert("atmosphere_planet_radius", settings.atmosphere_planet_radius);
    table.insert("atmosphere_height", settings.atmosphere_height);
    table.insert("atmosphere_multiscatter_strength", settings.atmosphere_multiscatter_strength);
    table.insert("atmosphere_rayleigh_scattering",
                 toml::array{settings.atmosphere_rayleigh_scattering.x(),
                             settings.atmosphere_rayleigh_scattering.y(),
                             settings.atmosphere_rayleigh_scattering.z()});
    table.insert("atmosphere_ozone_absorption",
                 toml::array{settings.atmosphere_ozone_absorption.x(),
                             settings.atmosphere_ozone_absorption.y(),
                             settings.atmosphere_ozone_absorption.z()});
    table.insert("atmosphere_ground_albedo", toml::array{settings.atmosphere_ground_albedo.x(),
                                                         settings.atmosphere_ground_albedo.y(),
                                                         settings.atmosphere_ground_albedo.z()});
}

bool EnvironmentCodec::remap_node_references(schema::ComponentDataT& component,
                                             std::span<const schema::NodeIdMappingT> mapping,
                                             std::string& error) const
{
    auto* settings = component.payload.AsEnvironmentData();
    if (!settings)
    {
        return false;
    }
    if (settings->atmosphere_sun_node_id == 0)
    {
        return true;
    }
    for (const auto& item : mapping)
    {
        if (item.source_id == settings->atmosphere_sun_node_id && item.target_id != 0 &&
            !item.retired)
        {
            settings->atmosphere_sun_node_id = item.target_id;
            return true;
        }
    }
    error = "Prefab has an external or retired node reference: atmosphere_sun_node_id";
    return false;
}

} // namespace engine::editor::asset_pipeline
