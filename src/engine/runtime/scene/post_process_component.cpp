#include "runtime/scene/post_process_component.hpp"

#include "runtime/scene/ecs_storage.hpp"

#include "runtime/scene/scene_settings_validation.hpp"

namespace engine
{

bool PostProcessComponent::register_type(ComponentRegistry& registry)
{
    return register_component_storage<PostProcessComponent, Data>(registry);
}

bool PostProcessComponent::get_settings(Data& output) const noexcept
{
    const auto* value = read_data<Data>();
    output = value ? *value : Data{};
    return value != nullptr;
}

bool PostProcessComponent::set_settings(const PostProcessSettings& settings) noexcept
{
    if (!writable())
    {
        return false;
    }
    if (!scene_detail::valid_post_process(settings))
    {
        return false;
    }
    *write_data<Data>() = settings;
    modified();
    return true;
}

bool PostProcessComponent::restore_data(const schema::ComponentDataT& data)
{
    if (!data.flags)
    {
        return false;
    }
    if (!writable())
    {
        return false;
    }
    const auto* settings = data.payload.AsPostProcessData();
    if (settings == nullptr || !set_settings(*settings))
    {
        return false;
    }
    if (!restore_flags(data))
    {
        return false;
    }
    return true;
}

} // namespace engine
