#pragma once

#include "runtime/system/system.hpp"

namespace engine
{

class PerformanceSystem final : public System
{
public:
    PerformanceSystem() noexcept;
    ~PerformanceSystem() noexcept override;

protected:
    [[nodiscard]] bool on_initialize(const EngineConfig& config) override;
    void on_shutdown() noexcept override;
};

} // namespace engine
