#pragma once

#include "runtime/system/system.hpp"
#include "runtime/resource/resource.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace engine
{

class WorldSystem;

struct AudioVoiceHandle
{
    std::uint64_t id = 0;
    std::uint64_t generation = 0;
};

enum class AudioVoiceState
{
    playing,
    paused,
    completed,
    stopped,
    failed
};

struct AudioPlaybackStatus
{
    AudioVoiceState state = AudioVoiceState::stopped;
    double position_seconds = 0.0;
    double duration_seconds = 0.0;
    std::string error;
};

class AudioSystem final : public System
{
public:
    // 可空借用，非空世界须存活至本系统关闭。
    explicit AudioSystem(WorldSystem* world = nullptr) noexcept;
    ~AudioSystem() noexcept override;
    [[nodiscard]] bool set_world(WorldSystem* world);
    [[nodiscard]] bool play(ResourcePtr clip, AudioVoiceHandle& output);
    [[nodiscard]] bool set_paused(AudioVoiceHandle handle, bool paused);
    [[nodiscard]] bool stop(AudioVoiceHandle handle);
    [[nodiscard]] bool playback(AudioVoiceHandle handle, AudioPlaybackStatus& output) const;
    [[nodiscard]] bool release(AudioVoiceHandle handle);
    void clear_scene() noexcept;
    void clear() noexcept;
    [[nodiscard]] bool set_paused(bool paused);
    [[nodiscard]] bool paused() const noexcept;
    [[nodiscard]] std::size_t voice_count() const noexcept;
    [[nodiscard]] std::string_view last_error() const noexcept;

protected:
    [[nodiscard]] bool on_initialize(const EngineConfig& config) override;
    [[nodiscard]] bool on_tick(double delta_time) override;
    void on_shutdown() noexcept override;

private:
    struct Impl;
    WorldSystem* world_ = nullptr;
    std::unique_ptr<Impl> impl_;
};

} // namespace engine
