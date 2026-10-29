#pragma once

#include "plugins/editor/mcp/mcp_target.hpp"

#include <filesystem>
#include <string>

namespace engine::editor
{
class AssetPipelineSystem;
}

namespace engine::plugins::asset_build
{
class AssetBuildSystem;
}

namespace engine::plugins::asset_sync
{
class AssetSyncSystem;
}

namespace engine::plugins::mcp
{

class McpSystem;

class ProjectMcpTools final
{
public:
    // 借用系统须先初始化，并存活到模块及其注册回调销毁。
    ProjectMcpTools(engine::editor::AssetPipelineSystem& pipeline,
                    engine::plugins::asset_build::AssetBuildSystem& builder,
                    engine::plugins::asset_sync::AssetSyncSystem& sync,
                    std::filesystem::path project_root, McpTarget target);

    ProjectMcpTools(const ProjectMcpTools&) = delete;
    ProjectMcpTools& operator=(const ProjectMcpTools&) = delete;
    ProjectMcpTools(ProjectMcpTools&&) = delete;
    ProjectMcpTools& operator=(ProjectMcpTools&&) = delete;

    [[nodiscard]] bool initialize();
    [[nodiscard]] bool register_tools(McpSystem& mcp);

private:
    [[nodiscard]] bool register_resource_tools(McpSystem& mcp);
    [[nodiscard]] bool register_runtime_tools(McpSystem& mcp);
    [[nodiscard]] bool register_lifecycle_tools(McpSystem& mcp);
    [[nodiscard]] bool sync_android(const std::filesystem::path& source, std::string& reply);
    [[nodiscard]] bool set_android_hot_reload(bool enabled, std::string& reply);
    [[nodiscard]] bool read_android_status(std::string& reply);

    engine::editor::AssetPipelineSystem& pipeline_;
    engine::plugins::asset_build::AssetBuildSystem& builder_;
    engine::plugins::asset_sync::AssetSyncSystem& sync_;
    std::filesystem::path project_root_;
    McpTarget target_;
    bool initialized_ = false;
};

} // namespace engine::plugins::mcp
