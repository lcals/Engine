#pragma once

#include "plugins/runtime/effects/effect_resource.hpp"
#include "runtime/resource/resource_reference.hpp"

#include "plugins/runtime/effects/effect_component.hpp"
#include "runtime/scene/component_runtime.hpp"

namespace engine::plugins::effects
{
struct EffectComponentRuntime
{

    resource_detail::ResourceReference effect_;
    std::uint64_t revision_ = 0;
    EffectComponent::Playback requested_ = EffectComponent::Playback::stopped;
    bool awake_pending_ = true;
    bool playing_ = false;
};
} // namespace engine::plugins::effects
