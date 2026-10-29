#pragma once

#include "runtime/system/system.hpp"

#include <filesystem>

namespace engine
{

class AssetPipelineSystem;
class AssetSyncSystem;

class McpSystem final : public System
{
public:
    McpSystem(AssetPipelineSystem& pipeline, AssetSyncSystem& sync,
              std::filesystem::path project_root, std::filesystem::path runtime_dir);
    ~McpSystem() noexcept override;

    [[nodiscard]] bool run_stdio();

protected:
    [[nodiscard]] bool on_initialize(const EngineConfig& config) override;
    void on_shutdown() noexcept override;

private:
    // 两个借用系统必须先初始化，并存活到 McpSystem 关闭。
    AssetPipelineSystem& pipeline_;
    AssetSyncSystem& sync_;
    std::filesystem::path project_root_;
    std::filesystem::path runtime_dir_;
};

} // namespace engine
