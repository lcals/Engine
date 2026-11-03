#pragma once

#include "runtime/resource/resource.hpp"
#include "runtime/scene/component.hpp"

#include <cstdint>
#include <string_view>

namespace engine::plugins::effects
{

class EffectSystem;

class EffectComponent final : public Component
{
public:
    static constexpr ComponentType component_type = ComponentType::EffectData;
    [[nodiscard]] static bool valid_settings(const schema::EffectDataT& settings) noexcept;
    [[nodiscard]] const schema::EffectDataT& settings() const noexcept;
    [[nodiscard]] bool set_settings(const schema::EffectDataT& settings);
    [[nodiscard]] bool restore_data(const schema::ComponentDataT& data) override;
    [[nodiscard]] bool set_effect(std::string_view path);
    [[nodiscard]] std::string_view effect_path() const noexcept;
    [[nodiscard]] bool play() noexcept;
    [[nodiscard]] bool pause() noexcept;
    [[nodiscard]] bool stop() noexcept;
    [[nodiscard]] bool playing() const noexcept;

private:
    friend class engine::Node;
    friend class EffectSystem;
    explicit EffectComponent(Node& node);

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

} // namespace engine::plugins::effects
