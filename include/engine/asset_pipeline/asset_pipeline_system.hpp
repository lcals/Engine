#pragma once

#include "system.hpp"

namespace engine
{

class AssetPipelineSystem final : public System
{
public:
    AssetPipelineSystem() noexcept;
    ~AssetPipelineSystem() noexcept override;

protected:
    [[nodiscard]] bool on_initialize(const EngineConfig& config) override;
    void on_shutdown() noexcept override;
};

} // namespace engine
