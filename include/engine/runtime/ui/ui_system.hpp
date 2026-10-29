#pragma once

#include "runtime/system/system.hpp"

namespace engine
{

class UiSystem final : public System
{
public:
    UiSystem() noexcept;
    ~UiSystem() noexcept override;

protected:
    [[nodiscard]] bool on_initialize(const EngineConfig& config) override;
    void on_shutdown() noexcept override;
};

} // namespace engine
