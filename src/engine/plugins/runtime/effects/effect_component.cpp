#include "plugins/runtime/effects/effect_component.hpp"

#include "plugins/runtime/effects/effect_resource_path.hpp"
#include "plugins/runtime/effects/effect_validation.hpp"
#include "runtime/core/guid_map.hpp"
#include "runtime/resource/resource_system.hpp"

#include <atomic>
#include <cmath>
#include <limits>
#include <utility>

namespace engine::plugins::effects
{
namespace
{

std::uint64_t next_instance_id() noexcept
{
    static std::atomic<std::uint64_t> sequence{0};
    auto current = sequence.load(std::memory_order_relaxed);
    while (current != std::numeric_limits<std::uint64_t>::max())
    {
        if (sequence.compare_exchange_weak(current, current + 1, std::memory_order_relaxed))
        {
            return current + 1;
        }
    }
    return 0;
}

} // namespace

EffectComponent::EffectComponent(Node& node) : Component(node), instance_id_(next_instance_id())
{
    mutable_data().payload.Set(schema::EffectDataT{});
}

bool EffectComponent::initialize()
{
    return instance_id_ != 0;
}

bool EffectComponent::valid_settings(const schema::EffectDataT& settings) noexcept
{
    return (settings.effect_guid.empty() || GuidMap::valid_guid(settings.effect_guid)) &&
           std::isfinite(settings.speed) && settings.speed >= 0.0F && settings.speed <= 16.0F;
}

const schema::EffectDataT& EffectComponent::settings() const noexcept
{
    return *data().payload.AsEffectData();
}

bool EffectComponent::set_settings(const schema::EffectDataT& settings)
{
    if (!valid_settings(settings))
    {
        return false;
    }
    auto* resources = ResourceSystem::instance();
    const bool identity_changed = settings.effect_guid != this->settings().effect_guid;
    const bool changed = identity_changed ||
                         (effect_ && resources && effect_->generation() != resources->generation());
    auto replacement = effect_;
    if (changed)
    {
        if (revision_ == std::numeric_limits<std::uint64_t>::max())
        {
            return false;
        }
        replacement.reset();
        if (!settings.effect_guid.empty())
        {
            std::string error;
            const schema::EffectAssetData* asset = nullptr;
            if (!resources || !resources->loader().load(settings.effect_guid, replacement) ||
                !replacement || replacement->info().kind != schema::AssetKind::effect ||
                !detail::read_effect_asset(replacement->bytes(), asset, error) ||
                !validate_effect({reinterpret_cast<const std::byte*>(asset->data()->data()),
                                  asset->data()->size()},
                                 error))
            {
                return false;
            }
        }
    }
    *mutable_data().payload.AsEffectData() = settings;
    effect_ = std::move(replacement);
    if (changed)
    {
        ++revision_;
        playing_ = false;
        if (identity_changed)
        {
            requested_ = Playback::stopped;
            awake_pending_ = settings.play_on_awake;
        }
    }
    if (!settings.play_on_awake)
    {
        awake_pending_ = false;
    }
    return true;
}

bool EffectComponent::restore_data(const schema::ComponentDataT& data)
{
    const auto* settings = data.payload.AsEffectData();
    if (!settings || !set_settings(*settings))
    {
        return false;
    }
    set_enabled(data.enabled);
    return true;
}

bool EffectComponent::set_effect(std::string_view path)
{
    auto value = settings();
    if (path.empty())
    {
        value.effect_guid.clear();
    }
    else if (!ResourceSystem::path_to_guid(path, value.effect_guid))
    {
        return false;
    }
    return set_settings(value);
}

std::string_view EffectComponent::effect_path() const noexcept
{
    return effect_ ? std::string_view(effect_->info().path) : std::string_view{};
}

std::uint64_t EffectComponent::instance_id() const noexcept
{
    return instance_id_;
}

bool EffectComponent::play() noexcept
{
    if (!enabled() || !effect_ || revision_ == std::numeric_limits<std::uint64_t>::max())
    {
        return false;
    }
    if (requested_ == Playback::stopped)
    {
        ++revision_;
    }
    requested_ = Playback::playing;
    awake_pending_ = false;
    return true;
}

bool EffectComponent::pause() noexcept
{
    if (requested_ != Playback::playing)
    {
        return false;
    }
    requested_ = Playback::paused;
    playing_ = false;
    awake_pending_ = false;
    return true;
}

bool EffectComponent::stop() noexcept
{
    requested_ = Playback::stopped;
    playing_ = false;
    awake_pending_ = false;
    return true;
}

bool EffectComponent::playing() const noexcept
{
    return enabled() && playing_;
}

} // namespace engine::plugins::effects
