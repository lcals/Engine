#include "editor/asset_pipeline/components/post_process_codec.hpp"

#include "editor/asset_pipeline/toml_helpers.hpp"
#include "runtime/scene/scene_settings_validation.hpp"

#include <utility>

namespace engine::editor::asset_pipeline
{

schema::ComponentPayload PostProcessCodec::type() const noexcept
{
    return schema::ComponentPayload::PostProcessData;
}

std::string_view PostProcessCodec::name() const noexcept
{
    return "post_process";
}

bool PostProcessCodec::validate(const schema::ComponentDataT& component, std::string& error) const
{
    const auto* settings = component.payload.AsPostProcessData();
    if (settings == nullptr || !scene_detail::valid_post_process(*settings))
    {
        error = "invalid scene post_process";
        return false;
    }
    return true;
}

bool PostProcessCodec::read(const toml::table& table, schema::ComponentDataT& component,
                            std::string& error) const
{
    PostProcessSettings settings;
    if (table.contains("exposure") && !float_number(table.get("exposure"), settings.exposure))
    {
        error = "post_process exposure must be a finite number";
        return false;
    }
    if (table.contains("filmic_tonemap"))
    {
        const auto enabled = table["filmic_tonemap"].value_exact<bool>();
        if (!enabled)
        {
            error = "post_process filmic_tonemap must be boolean";
            return false;
        }
        settings.filmic_tonemap = *enabled;
    }
    if (!scene_detail::valid_post_process(settings))
    {
        error = "post_process exposure must be in (0, 100]";
        return false;
    }
    component.payload.Set(std::move(settings));
    return true;
}

void PostProcessCodec::write(const schema::ComponentDataT& component, toml::table& table) const
{
    const auto& settings = *component.payload.AsPostProcessData();
    table.insert("type", "post_process");
    table.insert("exposure", settings.exposure);
    table.insert("filmic_tonemap", settings.filmic_tonemap);
}

} // namespace engine::editor::asset_pipeline
