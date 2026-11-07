#include "plugins/runtime/character_controller/character_visual.hpp"

#include "runtime/scene/ecs_storage.hpp"

namespace engine::plugins::character_controller
{
bool CharacterVisual::register_type(ComponentRegistry& registry)
{
    return register_component_storage<CharacterVisual, Data>(registry);
}

bool CharacterVisual::valid_settings(const schema::CharacterVisualDataT& value) noexcept
{
    return value.kind >= schema::CharacterVisualKind::MIN &&
           value.kind <= schema::CharacterVisualKind::MAX;
}

schema::CharacterVisualDataT CharacterVisual::settings() const noexcept
{
    Data output{};
    (void)get_settings(output);
    return output;
}

bool CharacterVisual::get_settings(Data& output) const noexcept
{
    const auto* value = read_data<Data>();
    output = value ? *value : Data{};
    return value != nullptr;
}

bool CharacterVisual::set_settings(const schema::CharacterVisualDataT& value) noexcept
{
    if (!writable())
    {
        return false;
    }
    if (!valid_settings(value))
    {
        return false;
    }
    *write_data<Data>() = value;
    modified();
    return true;
}

bool CharacterVisual::restore_data(const schema::ComponentDataT& value)
{
    if (!value.flags)
    {
        return false;
    }
    if (!writable())
    {
        return false;
    }
    const auto* settings = value.payload.AsCharacterVisualData();
    if (settings == nullptr || !set_settings(*settings))
    {
        return false;
    }
    if (!restore_flags(value))
    {
        return false;
    }
    return true;
}

} // namespace engine::plugins::character_controller
