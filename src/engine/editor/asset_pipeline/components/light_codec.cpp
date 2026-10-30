#include "editor/asset_pipeline/components/light_codec.hpp"

#include "runtime/scene/scene_settings_validation.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <utility>

namespace engine::editor::asset_pipeline
{

schema::ComponentPayload LightCodec::type() const noexcept
{
    return schema::ComponentPayload::LightData;
}

std::string_view LightCodec::name() const noexcept
{
    return "light";
}

bool LightCodec::validate(const schema::ComponentDataT& component, std::string& error) const
{

    const auto* value = component.payload.AsLightData();
    if (value == nullptr || !scene_detail::valid_light(*value))
    {
        error = "invalid scene light";
        return false;
    }

    return true;
}

bool LightCodec::read(const toml::table& table, schema::ComponentDataT& component,
                      std::string& error) const
{

    schema::LightDataT value;
    std::array<double, 4> color{1, 1, 1, 1};
    if ((table.contains("color") && !numbers(table.get("color"), color)) ||
        (table.contains("intensity") && !float_number(table.get("intensity"), value.intensity)) ||
        (table.contains("range") && !float_number(table.get("range"), value.range)) ||
        (table.contains("inner_angle") &&
         !float_number(table.get("inner_angle"), value.inner_angle)) ||
        (table.contains("outer_angle") &&
         !float_number(table.get("outer_angle"), value.outer_angle)) ||
        (table.contains("shadow_bias") &&
         !float_number(table.get("shadow_bias"), value.shadow_bias)))
    {
        error = "light numeric fields must be finite numbers";
        return false;
    }
    if (table.contains("cast_shadows"))
    {
        const auto cast_shadows = table["cast_shadows"].value_exact<bool>();
        if (!cast_shadows)
        {
            error = "light cast_shadows must be boolean";
            return false;
        }
        value.cast_shadows = *cast_shadows;
    }
    if (table.contains("shadow_resolution"))
    {
        const auto resolution = table["shadow_resolution"].value_exact<std::int64_t>();
        if (!resolution || *resolution < 128 || *resolution > 8192 ||
            (*resolution & (*resolution - 1)) != 0)
        {
            error = "light shadow_resolution must be a power of two in 128..8192";
            return false;
        }
        value.shadow_resolution = static_cast<std::uint16_t>(*resolution);
    }
    if (table.contains("kind"))
    {
        const auto kind = table["kind"].value<std::string_view>();
        if (kind != "directional" && kind != "point" && kind != "spot")
        {
            error = "light kind must be directional, point or spot";
            return false;
        }
        value.kind = kind == "point"  ? schema::LightKind::point
                     : kind == "spot" ? schema::LightKind::spot
                                      : schema::LightKind::directional;
    }
    for (const auto channel : color)
    {
        if (channel < 0 || channel > std::numeric_limits<float>::max())
        {
            error = "light color channel is out of range";
            return false;
        }
    }
    value.color = {static_cast<float>(color[0]), static_cast<float>(color[1]),
                   static_cast<float>(color[2]), static_cast<float>(color[3])};
    component.payload.Set(std::move(value));

    return true;
}

void LightCodec::write(const schema::ComponentDataT& component, toml::table& value) const
{

    const auto* light = component.payload.AsLightData();
    value.insert("type", "light");
    value.insert("color", toml::array{light->color.r(), light->color.g(), light->color.b(),
                                      light->color.a()});
    value.insert("intensity", light->intensity);
    value.insert("kind", schema::EnumNameLightKind(light->kind));
    value.insert("range", light->range);
    value.insert("inner_angle", light->inner_angle);
    value.insert("outer_angle", light->outer_angle);
    value.insert("cast_shadows", light->cast_shadows);
    value.insert("shadow_resolution", light->shadow_resolution);
    value.insert("shadow_bias", light->shadow_bias);
}

} // namespace engine::editor::asset_pipeline
