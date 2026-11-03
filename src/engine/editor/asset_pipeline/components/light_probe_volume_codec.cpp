#include "editor/asset_pipeline/components/light_probe_volume_codec.hpp"
#include "runtime/core/guid_map.hpp"

namespace engine::editor::asset_pipeline
{
schema::ComponentPayload LightProbeVolumeCodec::type() const noexcept
{
    return schema::ComponentPayload::LightProbeVolumeData;
}
std::string_view LightProbeVolumeCodec::name() const noexcept
{
    return "light_probe_volume";
}
bool LightProbeVolumeCodec::validate(const schema::ComponentDataT& component,
                                     std::string& error) const
{
    const auto* settings = component.payload.AsLightProbeVolumeData();
    if (!settings ||
        (!settings->probes_guid.empty() && !GuidMap::valid_guid(settings->probes_guid)))
    {
        error = "light_probe_volume probes must be a resource GUID";
        return false;
    }
    return true;
}
bool LightProbeVolumeCodec::read(const toml::table& table, schema::ComponentDataT& component,
                                 std::string& error) const
{
    schema::LightProbeVolumeDataT settings;
    if (table.contains("probes"))
    {
        const auto guid = table["probes"].value_exact<std::string>();
        if (!guid)
        {
            error = "light_probe_volume probes must be a resource GUID string";
            return false;
        }
        settings.probes_guid = *guid;
    }
    component.payload.Set(settings);
    return validate(component, error);
}
void LightProbeVolumeCodec::write(const schema::ComponentDataT& component, toml::table& table) const
{
    table.insert("type", "light_probe_volume");
    table.insert("probes", component.payload.AsLightProbeVolumeData()->probes_guid);
}
} // namespace engine::editor::asset_pipeline
