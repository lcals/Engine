#include "plugins/runtime/character_controller/character_visual.hpp"

namespace engine::plugins::character_controller
{
namespace
{

schema::ComponentDataT make_data()
{
    schema::ComponentDataT data;
    data.payload.Set(schema::CharacterVisualDataT{});
    return data;
}

} // namespace

CharacterVisual::CharacterVisual(Node& node) : Component(node, make_data())
{
}

bool CharacterVisual::valid_settings(const schema::CharacterVisualDataT& value) noexcept
{
    return value.kind >= schema::CharacterVisualKind::MIN &&
           value.kind <= schema::CharacterVisualKind::MAX;
}

const schema::CharacterVisualDataT& CharacterVisual::settings() const noexcept
{
    return *data().payload.AsCharacterVisualData();
}

bool CharacterVisual::set_settings(const schema::CharacterVisualDataT& value) noexcept
{
    if (!valid_settings(value))
    {
        return false;
    }
    *mutable_data().payload.AsCharacterVisualData() = value;
    return true;
}

bool CharacterVisual::restore_data(const schema::ComponentDataT& value)
{
    const auto* settings = value.payload.AsCharacterVisualData();
    if (settings == nullptr || !set_settings(*settings))
    {
        return false;
    }
    set_enabled(value.enabled);
    return true;
}

} // namespace engine::plugins::character_controller
