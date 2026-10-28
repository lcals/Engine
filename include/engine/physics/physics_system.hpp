#pragma once

#include "system.hpp"

namespace engine
{

class PhysicsSystem final : public System
{
public:
    PhysicsSystem() noexcept;
    ~PhysicsSystem() noexcept override;

protected:
    [[nodiscard]] bool on_initialize(const EngineConfig& config) override;
    void on_shutdown() noexcept override;
};

} // namespace engine
