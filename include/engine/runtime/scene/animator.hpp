#pragma once

#include "runtime/scene/component.hpp"

#include <cstdint>
#include <memory>
#include <string_view>

namespace engine
{

class AnimationSystem;
class Resource;

enum class AnimationPlayback
{
    stopped,
    playing,
    paused
};

class Animator final : public Component
{
public:
    static constexpr ComponentType component_type = ComponentType::AnimatorData;
    [[nodiscard]] static bool valid_settings(const schema::AnimatorDataT& settings) noexcept;
    [[nodiscard]] const schema::AnimatorDataT& settings() const noexcept;
    [[nodiscard]] bool set_settings(const schema::AnimatorDataT& settings);
    [[nodiscard]] bool restore_data(const schema::ComponentDataT& data) override;
    [[nodiscard]] bool set_animation(std::string_view path);
    [[nodiscard]] std::string_view animation_path() const noexcept;
    [[nodiscard]] bool play() noexcept;
    [[nodiscard]] bool pause() noexcept;
    [[nodiscard]] bool stop() noexcept;
    [[nodiscard]] bool seek(double seconds) noexcept;
    [[nodiscard]] AnimationPlayback playback() const noexcept;
    [[nodiscard]] double time() const noexcept;

private:
    friend class Node;
    friend class AnimationSystem;
    explicit Animator(Node& node);

    std::shared_ptr<const Resource> animation_;
    std::shared_ptr<const Resource> target_skeleton_;
    std::uint64_t revision_ = 1;
    std::uint64_t prepared_revision_ = 0;
    double time_ = 0.0;
    AnimationPlayback playback_ = AnimationPlayback::stopped;
    bool awake_pending_ = true;
    bool pose_dirty_ = false;
};

} // namespace engine
