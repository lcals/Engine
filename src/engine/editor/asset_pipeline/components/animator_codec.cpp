#include "editor/asset_pipeline/components/animator_codec.hpp"

#include "runtime/scene/animator.hpp"

#include <utility>

namespace engine::editor::asset_pipeline
{
schema::ComponentPayload AnimatorCodec::type() const noexcept
{
    return schema::ComponentPayload::AnimatorData;
}
std::string_view AnimatorCodec::name() const noexcept
{
    return "animator";
}
bool AnimatorCodec::validate(const schema::ComponentDataT& component, std::string& error) const
{
    const auto* value = component.payload.AsAnimatorData();
    if (!value || !engine::Animator::valid_settings(*value))
    {
        error = "invalid animator settings";
        return false;
    }
    return true;
}
bool AnimatorCodec::read(const toml::table& table, schema::ComponentDataT& component,
                         std::string& error) const
{
    schema::AnimatorDataT value;
    if (table.contains("animation"))
    {
        const auto parsed = table["animation"].value<std::string>();
        if (!parsed)
        {
            error = "animator.animation must be a GUID";
            return false;
        }
        value.animation_guid = *parsed;
    }
    if (table.contains("speed") && !float_number(table.get("speed"), value.speed))
    {
        error = "animator.speed must be finite";
        return false;
    }
    for (const auto* key : {"loop", "play_on_awake"})
    {
        if (table.contains(key))
        {
            const auto parsed = table[key].value<bool>();
            if (!parsed)
            {
                error = std::string("animator.") + key + " must be boolean";
                return false;
            }
            (std::string_view(key) == "loop" ? value.loop : value.play_on_awake) = *parsed;
        }
    }
    component.payload.Set(std::move(value));
    return validate(component, error);
}
void AnimatorCodec::write(const schema::ComponentDataT& component, toml::table& table) const
{
    const auto* value = component.payload.AsAnimatorData();
    table.insert("type", "animator");
    table.insert("animation", value->animation_guid);
    table.insert("speed", value->speed);
    table.insert("loop", value->loop);
    table.insert("play_on_awake", value->play_on_awake);
}
} // namespace engine::editor::asset_pipeline
