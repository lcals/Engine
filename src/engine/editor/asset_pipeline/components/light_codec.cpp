#include "editor/asset_pipeline/components/light_codec.hpp"

#include "runtime/core/guid_map.hpp"

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
    if (value == nullptr || !std::isfinite(value->intensity) || value->intensity < 0 ||
        !std::isfinite(value->color.r()) || !std::isfinite(value->color.g()) ||
        !std::isfinite(value->color.b()) || !std::isfinite(value->color.a()) ||
        value->color.r() < 0 || value->color.g() < 0 || value->color.b() < 0 ||
        value->color.a() < 0 || value->color.a() > 1 || value->color.r() > 1 ||
        value->color.g() > 1 || value->color.b() > 1 || value->kind < schema::LightKind::MIN ||
        value->kind > schema::LightKind::MAX || !std::isfinite(value->range) || value->range <= 0 ||
        !std::isfinite(value->inner_angle) || !std::isfinite(value->outer_angle) ||
        value->inner_angle < 0 || value->outer_angle <= 0 || value->outer_angle >= 90 ||
        value->inner_angle > value->outer_angle)
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
         !float_number(table.get("outer_angle"), value.outer_angle)))
    {
        error = "light color and intensity must be finite numbers";
        return false;
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
}

} // namespace engine::editor::asset_pipeline
