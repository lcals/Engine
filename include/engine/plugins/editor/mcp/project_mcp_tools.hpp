#pragma once

#include "plugins/editor/mcp/mcp_target.hpp"

#include <filesystem>
#include <string>

namespace engine
{

class McpSystem;
class AssetPipelineSystem;
class AssetBuildSystem;
class AssetSyncSystem;

class ProjectMcpTools final
{
public:
    // 借用系统须先初始化，并存活到模块及其注册回调销毁。
    ProjectMcpTools(AssetPipelineSystem& pipeline, AssetBuildSystem& builder, AssetSyncSystem& sync,
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

    AssetPipelineSystem& pipeline_;
    AssetBuildSystem& builder_;
    AssetSyncSystem& sync_;
    std::filesystem::path project_root_;
    McpTarget target_;
    bool initialized_ = false;
};

} // namespace engine
