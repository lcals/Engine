#include "project_system.hpp"

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
        std::fprintf(stderr, "Cannot access required project directory '%s': %s\n",
                     display_path.c_str(), error.message().c_str());
        return false;
    }
    if (!is_directory)
    {
        std::fprintf(stderr, "Required project path '%s' is not a directory\n",
                     display_path.c_str());
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
        std::fputs("Project root directory must not be empty\n", stderr);
        return false;
    }

    const std::filesystem::path root(project_root_);
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

    // 目录校验通过后接入配置数据依赖；本阶段不读写项目文件。
    toml::table project;
    project.insert("project", project_root_);
    const auto identifier = project["project"].value<std::string_view>();
    return identifier && *identifier == project_root_;
}

void ProjectSystem::on_shutdown() noexcept
{
}

} // namespace engine
