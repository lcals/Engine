#include "editor/asset_pipeline/components/audio_source_codec.hpp"
#include "runtime/scene/audio_source.hpp"
#include <array>
#include <utility>
namespace engine::editor::asset_pipeline
{
schema::ComponentPayload AudioSourceCodec::type() const noexcept
{
    return schema::ComponentPayload::AudioSourceData;
}
std::string_view AudioSourceCodec::name() const noexcept
{
    return "audio_source";
}
bool AudioSourceCodec::validate(const schema::ComponentDataT& component, std::string& error) const
{
    const auto* value = component.payload.AsAudioSourceData();
    if (value == nullptr || !engine::AudioSource::valid_settings(*value))
    {
        error = "invalid audio_source settings";
        return false;
    }
    return true;
}
bool AudioSourceCodec::read(const toml::table& table, schema::ComponentDataT& component,
                            std::string& error) const
{
    schema::AudioSourceDataT value;
    if (table.contains("clip"))
    {
        const auto parsed = table["clip"].value<std::string>();
        if (!parsed)
        {
            error = "audio_source.clip has an invalid type";
            return false;
        }
        value.clip_guid = *parsed;
    }
    if (table.contains("volume"))
    {
        if (!float_number(table.get("volume"), value.volume))
        {
            error = "audio_source.volume must be a finite number";
            return false;
        }
    }
    if (table.contains("pitch"))
    {
        if (!float_number(table.get("pitch"), value.pitch))
        {
            error = "audio_source.pitch must be a finite number";
            return false;
        }
    }
    if (table.contains("loop"))
    {
        const auto parsed = table["loop"].value<bool>();
        if (!parsed)
        {
            error = "audio_source.loop has an invalid type";
            return false;
        }
        value.loop = *parsed;
    }
    if (table.contains("play_on_awake"))
    {
        const auto parsed = table["play_on_awake"].value<bool>();
        if (!parsed)
        {
            error = "audio_source.play_on_awake has an invalid type";
            return false;
        }
        value.play_on_awake = *parsed;
    }
    if (table.contains("spatial"))
    {
        const auto parsed = table["spatial"].value<bool>();
        if (!parsed)
        {
            error = "audio_source.spatial has an invalid type";
            return false;
        }
        value.spatial = *parsed;
    }
    if (table.contains("min_distance"))
    {
        if (!float_number(table.get("min_distance"), value.min_distance))
        {
            error = "audio_source.min_distance must be a finite number";
            return false;
        }
    }
    if (table.contains("max_distance"))
    {
        if (!float_number(table.get("max_distance"), value.max_distance))
        {
            error = "audio_source.max_distance must be a finite number";
            return false;
        }
    }
    component.payload.Set(std::move(value));
    return true;
}
void AudioSourceCodec::write(const schema::ComponentDataT& component, toml::table& table) const
{
    const auto* value = component.payload.AsAudioSourceData();
    table.insert("type", "audio_source");
    table.insert("clip", value->clip_guid);
    table.insert("volume", value->volume);
    table.insert("pitch", value->pitch);
    table.insert("loop", value->loop);
    table.insert("play_on_awake", value->play_on_awake);
    table.insert("spatial", value->spatial);
    table.insert("min_distance", value->min_distance);
    table.insert("max_distance", value->max_distance);
}
} // namespace engine::editor::asset_pipeline
