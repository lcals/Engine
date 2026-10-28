#pragma once

#include "system.hpp"

namespace engine
{

class ResourceSystem final : public System
{
public:
    ResourceSystem() noexcept;
    ~ResourceSystem() noexcept override;

protected:
    [[nodiscard]] bool on_initialize(const EngineConfig& config) override;
    void on_shutdown() noexcept override;
};

} // namespace engine
