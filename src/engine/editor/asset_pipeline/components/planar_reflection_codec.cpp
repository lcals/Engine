#include "editor/asset_pipeline/components/planar_reflection_codec.hpp"

#include "editor/asset_pipeline/toml_helpers.hpp"
#include "runtime/scene/scene_settings_validation.hpp"

namespace engine::editor::asset_pipeline
{
schema::ComponentPayload PlanarReflectionCodec::type() const noexcept
{
    return schema::ComponentPayload::PlanarReflectionData;
}

std::string_view PlanarReflectionCodec::name() const noexcept
{
    return "planar_reflection";
}

bool PlanarReflectionCodec::validate(const schema::ComponentDataT& component,
                                     std::string& error) const
{
    const auto* settings = component.payload.AsPlanarReflectionData();
    if (!settings || !scene_detail::valid_planar_reflection(*settings))
    {
        error = "invalid planar_reflection settings";
        return false;
    }
    return true;
}

bool PlanarReflectionCodec::read(const toml::table& table, schema::ComponentDataT& component,
                                 std::string& error) const
{
    PlanarReflectionSettings settings;
    if ((table.contains("resolution_scale") &&
         !float_number(table.get("resolution_scale"), settings.resolution_scale)) ||
        (table.contains("clip_bias") && !float_number(table.get("clip_bias"), settings.clip_bias)))
    {
        error = "planar_reflection settings must be finite numbers";
        return false;
    }
    component.payload.Set(settings);
    return validate(component, error);
}

void PlanarReflectionCodec::write(const schema::ComponentDataT& component, toml::table& table) const
{
    const auto& settings = *component.payload.AsPlanarReflectionData();
    table.insert("type", "planar_reflection");
    table.insert("resolution_scale", settings.resolution_scale);
    table.insert("clip_bias", settings.clip_bias);
}
} // namespace engine::editor::asset_pipeline
