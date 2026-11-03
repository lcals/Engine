#pragma once

#include "runtime/resource/resource.hpp"
#include "runtime/scene/component.hpp"

#include <cstdint>
#include <string_view>

namespace engine::plugins::effects
{

class EffectSystem;

struct EffectComponentRuntime
{
    enum class Playback
    {
        stopped,
        playing,
        paused
    };

    ResourcePtr effect_;
    std::uint64_t revision_ = 0;
    Playback requested_ = Playback::stopped;
    bool awake_pending_ = true;
    bool playing_ = false;
};

class EffectComponent final : public Component
{
public:
    using Data = schema::EffectDataT;
    EffectComponent() noexcept = default;
    explicit EffectComponent(Component component) noexcept : Component(component, component_type)
    {
    }
    [[nodiscard]] static bool register_type(ComponentRegistry& registry);
    using Runtime = EffectComponentRuntime;
    [[nodiscard]] Runtime* runtime() const noexcept;
    using Playback = Runtime::Playback;
    static constexpr ComponentType component_type = ComponentType::EffectData;
    [[nodiscard]] static bool valid_settings(const schema::EffectDataT& settings) noexcept;
    [[nodiscard]] schema::EffectDataT settings() const noexcept;
    [[nodiscard]] bool get_settings(Data& output) const noexcept;
    [[nodiscard]] bool set_settings(const schema::EffectDataT& settings);
    [[nodiscard]] bool restore_data(const schema::ComponentDataT& data);
    [[nodiscard]] bool set_effect(std::string_view path);
    [[nodiscard]] std::string_view effect_path() const noexcept;
    [[nodiscard]] bool play() noexcept;
    [[nodiscard]] bool pause() noexcept;
    [[nodiscard]] bool stop() noexcept;
    [[nodiscard]] bool playing() const noexcept;
};

} // namespace engine::plugins::effects
