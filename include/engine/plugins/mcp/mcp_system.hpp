#pragma once

#include "runtime/system/system.hpp"

#include <filesystem>
#include <string>

namespace engine
{

class AssetPipelineSystem;
class AssetSyncSystem;

struct McpTarget
{
    std::filesystem::path runtime_dir;
    std::filesystem::path adb_executable = "adb";
    std::string android_serial;
    std::string android_package;
};

class McpSystem final : public System
{
public:
    McpSystem(AssetPipelineSystem& pipeline, AssetSyncSystem& sync,
              std::filesystem::path project_root, McpTarget target);
    ~McpSystem() noexcept override;

    [[nodiscard]] bool run_stdio();

protected:
    [[nodiscard]] bool on_initialize(const EngineConfig& config) override;
    void on_shutdown() noexcept override;

private:
    [[nodiscard]] bool sync_android(std::string& reply);
    [[nodiscard]] bool set_android_hot_reload(bool enabled, std::string& reply);
    [[nodiscard]] bool read_android_status(std::string& reply);

    // 两个借用系统必须先初始化，并存活到 McpSystem 关闭。
    AssetPipelineSystem& pipeline_;
    AssetSyncSystem& sync_;
    std::filesystem::path project_root_;
    McpTarget target_;
};

} // namespace engine
