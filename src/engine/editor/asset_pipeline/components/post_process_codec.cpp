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
    if ((table.contains("exposure") && !float_number(table.get("exposure"), settings.exposure)) ||
        (table.contains("ao_radius") &&
         !float_number(table.get("ao_radius"), settings.ao_radius)) ||
        (table.contains("ao_strength") &&
         !float_number(table.get("ao_strength"), settings.ao_strength)) ||
        (table.contains("ao_power") && !float_number(table.get("ao_power"), settings.ao_power)) ||
        (table.contains("ao_fade_start") &&
         !float_number(table.get("ao_fade_start"), settings.ao_fade_start)) ||
        (table.contains("ao_fade_end") &&
         !float_number(table.get("ao_fade_end"), settings.ao_fade_end)) ||
        (table.contains("bloom_threshold") &&
         !float_number(table.get("bloom_threshold"), settings.bloom_threshold)) ||
        (table.contains("bloom_intensity") &&
         !float_number(table.get("bloom_intensity"), settings.bloom_intensity)) ||
        (table.contains("bloom_radius") &&
         !float_number(table.get("bloom_radius"), settings.bloom_radius)))
    {
        error = "post_process numeric fields must be finite numbers";
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
    if (table.contains("ao_enabled"))
    {
        const auto enabled = table["ao_enabled"].value_exact<bool>();
        if (!enabled)
        {
            error = "post_process ao_enabled must be boolean";
            return false;
        }
        settings.ao_enabled = *enabled;
    }
    if (table.contains("hdr_enabled"))
    {
        const auto enabled = table["hdr_enabled"].value_exact<bool>();
        if (!enabled)
        {
            error = "post_process hdr_enabled must be boolean";
            return false;
        }
        settings.hdr_enabled = *enabled;
    }
    if (table.contains("bloom_enabled"))
    {
        const auto enabled = table["bloom_enabled"].value_exact<bool>();
        if (!enabled)
        {
            error = "post_process bloom_enabled must be boolean";
            return false;
        }
        settings.bloom_enabled = *enabled;
    }
    if (table.contains("ssr_enabled"))
    {
        const auto enabled = table["ssr_enabled"].value_exact<bool>();
        if (!enabled)
        {
            error = "post_process ssr_enabled must be boolean";
            return false;
        }
        settings.ssr_enabled = *enabled;
    }
    if (table.contains("ssr_max_steps"))
    {
        const auto steps = table["ssr_max_steps"].value_exact<std::int64_t>();
        if (!steps || *steps < 8 || *steps > 256)
        {
            error = "post_process ssr_max_steps must be an integer in [8, 256]";
            return false;
        }
        settings.ssr_max_steps = static_cast<std::uint16_t>(*steps);
    }
    if ((table.contains("ssr_resolution_scale") &&
         !float_number(table.get("ssr_resolution_scale"), settings.ssr_resolution_scale)) ||
        (table.contains("ssr_max_distance") &&
         !float_number(table.get("ssr_max_distance"), settings.ssr_max_distance)) ||
        (table.contains("ssr_thickness") &&
         !float_number(table.get("ssr_thickness"), settings.ssr_thickness)) ||
        (table.contains("ssr_roughness_cutoff") &&
         !float_number(table.get("ssr_roughness_cutoff"), settings.ssr_roughness_cutoff)))
    {
        error = "post_process SSR fields must be finite numbers";
        return false;
    }
    if (!scene_detail::valid_post_process(settings))
    {
        error = "post_process parameters are invalid or Bloom is enabled without HDR";
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
    table.insert("ao_enabled", settings.ao_enabled);
    table.insert("ao_radius", settings.ao_radius);
    table.insert("ao_strength", settings.ao_strength);
    table.insert("ao_power", settings.ao_power);
    table.insert("ao_fade_start", settings.ao_fade_start);
    table.insert("ao_fade_end", settings.ao_fade_end);
    table.insert("hdr_enabled", settings.hdr_enabled);
    table.insert("bloom_enabled", settings.bloom_enabled);
    table.insert("bloom_threshold", settings.bloom_threshold);
    table.insert("bloom_intensity", settings.bloom_intensity);
    table.insert("bloom_radius", settings.bloom_radius);
    table.insert("ssr_enabled", settings.ssr_enabled);
    table.insert("ssr_resolution_scale", settings.ssr_resolution_scale);
    table.insert("ssr_max_steps", settings.ssr_max_steps);
    table.insert("ssr_max_distance", settings.ssr_max_distance);
    table.insert("ssr_thickness", settings.ssr_thickness);
    table.insert("ssr_roughness_cutoff", settings.ssr_roughness_cutoff);
}

} // namespace engine::editor::asset_pipeline
