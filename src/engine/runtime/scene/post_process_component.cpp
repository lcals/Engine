#include "runtime/scene/post_process_component.hpp"

#include "runtime/scene/scene_settings_validation.hpp"

namespace engine
{

PostProcessComponent::PostProcessComponent(Node& node) : Component(node)
{
    mutable_data().payload.Set(PostProcessSettings{});
}

const PostProcessSettings& PostProcessComponent::settings() const noexcept
{
    return *data().payload.AsPostProcessData();
}

bool PostProcessComponent::set_settings(const PostProcessSettings& settings) noexcept
{
    if (!scene_detail::valid_post_process(settings))
    {
        return false;
    }
    *mutable_data().payload.AsPostProcessData() = settings;
    return true;
}

bool PostProcessComponent::restore_data(const schema::ComponentDataT& data)
{
    const auto* settings = data.payload.AsPostProcessData();
    if (settings == nullptr || !set_settings(*settings))
    {
        return false;
    }
    set_enabled(data.enabled);
    return true;
}

} // namespace engine
