#pragma once

#include "system.hpp"

#include <filesystem>
#include <string>

namespace engine
{

class AssetPipelineSystem final : public System
{
public:
    AssetPipelineSystem() noexcept;
    ~AssetPipelineSystem() noexcept override;

    [[nodiscard]] bool build_scripts(const std::filesystem::path& project_root);
    [[nodiscard]] const std::string& last_error() const noexcept;

protected:
    [[nodiscard]] bool on_initialize(const EngineConfig& config) override;
    void on_shutdown() noexcept override;

private:
    std::string last_error_;
};

} // namespace engine
