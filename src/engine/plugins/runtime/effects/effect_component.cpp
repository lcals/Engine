#include "plugins/runtime/effects/effect_component.hpp"

#include "plugins/runtime/effects/effect_component_runtime.hpp"

#include "runtime/scene/ecs_storage.hpp"

#include "plugins/runtime/effects/effect_resource_path.hpp"
#include "plugins/runtime/effects/effect_validation.hpp"
#include "runtime/core/guid_map.hpp"
#include "runtime/scene/resource_binding.hpp"

#include <cmath>
#include <limits>
#include <utility>

namespace engine::plugins::effects
{

bool EffectComponent::register_type(ComponentRegistry& registry)
{
    return register_component_storage<EffectComponent, Data, Runtime>(registry);
}

EffectComponent::Runtime* EffectComponent::runtime() const noexcept
{
    return runtime_state<Runtime>();
}

bool EffectComponent::valid_settings(const schema::EffectDataT& settings) noexcept
{
    return (settings.effect_guid.empty() || GuidMap::valid_guid(settings.effect_guid)) &&
           std::isfinite(settings.speed) && settings.speed >= 0.0F && settings.speed <= 16.0F;
}

schema::EffectDataT EffectComponent::settings() const noexcept
{
    Data output{};
    (void)get_settings(output);
    return output;
}

bool EffectComponent::get_settings(Data& output) const noexcept
{
    const auto* value = read_data<Data>();
    output = value ? *value : Data{};
    return value != nullptr;
}

bool EffectComponent::set_settings(const schema::EffectDataT& settings)
{
    return apply_settings(settings, {});
}

bool EffectComponent::apply_settings(const Data& settings,
                                     resource_detail::ResourceReference prepared)
{
    if (!writable())
    {
        return false;
    }
    if (!valid_settings(settings))
    {
        return false;
    }
    auto* resources = node().world()->resource_loader();
    const bool changed = settings.effect_guid != this->settings().effect_guid;
    resource_detail::ResourceReference replacement;
    if (runtime()->effect_ && !replacement.retain(runtime()->effect_))
    {
        return false;
    }
    if (changed)
    {
        if (runtime()->revision_ == std::numeric_limits<std::uint64_t>::max())
        {
            return false;
        }
        replacement = std::move(prepared);
        if (!settings.effect_guid.empty())
        {
            if (!resources ||
                (!replacement &&
                 !ecs_detail::acquire_resource(node(), settings.effect_guid, replacement)) ||
                !replacement || replacement->info().kind != schema::AssetKind::effect ||
                !replacement.data<EffectResource>())
            {
                return false;
            }
        }
    }
    *write_data<Data>() = settings;
    modified();
    runtime()->effect_ = std::move(replacement);
    if (changed)
    {
        ++runtime()->revision_;
        runtime()->playing_ = false;
        runtime()->requested_ = Playback::stopped;
        runtime()->awake_pending_ = settings.play_on_awake;
    }
    if (!settings.play_on_awake)
    {
        runtime()->awake_pending_ = false;
    }
    return true;
}

bool EffectComponent::restore_data(const schema::ComponentDataT& data)
{
    if (!data.flags)
    {
        return false;
    }
    if (!writable())
    {
        return false;
    }
    const auto* settings = data.payload.AsEffectData();
    if (!settings || !set_settings(*settings))
    {
        return false;
    }
    if (!restore_flags(data))
    {
        return false;
    }
    return true;
}

bool EffectComponent::set_effect(std::string_view path)
{
    if (!writable())
    {
        return false;
    }
    resource_detail::ResourceReference prepared;
    auto value = settings();
    if (path.empty())
    {
        value.effect_guid.clear();
    }
    else if (!ecs_detail::acquire_resource(node(), path, prepared))
    {
        return false;
    }
    if (prepared)
    {
        value.effect_guid = prepared->info().guid;
    }
    return apply_settings(value, std::move(prepared));
}

std::string_view EffectComponent::effect_path() const noexcept
{
    if (!valid())
    {
        return {};
    }
    return runtime()->effect_ ? std::string_view(runtime()->effect_->info().path)
                              : std::string_view{};
}

bool EffectComponent::play() noexcept
{
    if (!writable())
    {
        return false;
    }
    if (!active() || !runtime()->effect_ ||
        runtime()->revision_ == std::numeric_limits<std::uint64_t>::max())
    {
        return false;
    }
    if (runtime()->requested_ == Playback::stopped)
    {
        ++runtime()->revision_;
    }
    runtime()->requested_ = Playback::playing;
    runtime()->awake_pending_ = false;
    return true;
}

bool EffectComponent::pause() noexcept
{
    if (!writable())
    {
        return false;
    }
    if (runtime()->requested_ != Playback::playing)
    {
        return false;
    }
    runtime()->requested_ = Playback::paused;
    runtime()->playing_ = false;
    runtime()->awake_pending_ = false;
    return true;
}

bool EffectComponent::stop() noexcept
{
    if (!writable())
    {
        return false;
    }
    runtime()->requested_ = Playback::stopped;
    runtime()->playing_ = false;
    runtime()->awake_pending_ = false;
    return true;
}

bool EffectComponent::playing() const noexcept
{
    if (!valid())
    {
        return false;
    }
    return active() && runtime()->playing_;
}

} // namespace engine::plugins::effects
