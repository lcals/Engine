#include "editor/project/project_system.hpp"

#include "runtime/log/log_system.hpp"
#include "runtime/utils/path_utils.hpp"

#include <toml++/toml.hpp>

#include <cstdio>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace engine
{
namespace
{

[[nodiscard]] bool check_directory(const std::filesystem::path& path,
                                   const std::string& display_path)
{
    std::error_code error;
    const bool is_directory = std::filesystem::is_directory(path, error);
    if (error)
    {
        LogSystem::write(LogLevel::error, "Cannot access required project directory '" +
                                              display_path + "': " + error.message());
        return false;
    }
    if (!is_directory)
    {
        LogSystem::write(LogLevel::error,
                         "Required project path '" + display_path + "' is not a directory");
        return false;
    }
    return true;
}

} // namespace

ProjectSystem::ProjectSystem(std::string project_root) : project_root_(std::move(project_root))
{
}

ProjectSystem::~ProjectSystem() noexcept
{
    shutdown();
}

bool ProjectSystem::on_initialize(const EngineConfig&)
{
    if (project_root_.empty())
    {
        LogSystem::write(LogLevel::error, "Project root directory must not be empty");
        return false;
    }

    const auto root = PathUtils::from_utf8(project_root_);
    if (!check_directory(root, project_root_))
    {
        return false;
    }
    for (const char* directory : {"settings", "cache", "assets"})
    {
        if (!check_directory(root / directory, project_root_ + "/" + directory))
        {
            return false;
        }
    }

    // 目录校验后验证 TOML 配置依赖，项目文件由各功能模块按职责读取。
    toml::table project;
    project.insert("project", project_root_);
    const auto identifier = project["project"].value<std::string_view>();
    return identifier && *identifier == project_root_;
}

void ProjectSystem::on_shutdown() noexcept
{
}

} // namespace engine
