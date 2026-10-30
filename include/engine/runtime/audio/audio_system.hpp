#pragma once

#include "runtime/system/system.hpp"

#include <cstddef>
#include <memory>
#include <string_view>

namespace engine
{

class WorldSystem;

class AudioSystem final : public System
{
public:
    // 可空借用，非空世界须存活至本系统关闭。
    explicit AudioSystem(WorldSystem* world = nullptr) noexcept;
    ~AudioSystem() noexcept override;
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
