#pragma once

#include "runtime/system/system.hpp"

namespace engine
{

class AnimationSystem final : public System
{
public:
    AnimationSystem() noexcept;
    ~AnimationSystem() noexcept override;

protected:
    [[nodiscard]] bool on_initialize(const EngineConfig& config) override;
    void on_shutdown() noexcept override;
};

} // namespace engine
