#pragma once

#include "system.hpp"

namespace engine
{

class ScriptSystem final : public System
{
public:
    ScriptSystem() noexcept;
    ~ScriptSystem() noexcept override;

protected:
    [[nodiscard]] bool on_initialize(const EngineConfig& config) override;
    void on_shutdown() noexcept override;
};

} // namespace engine
