#include "project_system.hpp"

#include "engine_config.hpp"

#include <toml++/toml.hpp>

#include <cstdio>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>

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

ProjectSystem::ProjectSystem() noexcept = default;

ProjectSystem::~ProjectSystem() noexcept
{
    shutdown();
}

bool ProjectSystem::on_initialize(const EngineConfig& config)
{
    if (config.project.empty())
    {
        std::fputs("Project root directory must not be empty\n", stderr);
        return false;
    }

    const std::filesystem::path root(config.project);
    if (!check_directory(root, config.project))
    {
        return false;
    }
    for (const char* directory : {"settings", "cache", "assets"})
    {
        if (!check_directory(root / directory, config.project + "/" + directory))
        {
            return false;
        }
    }

    // 目录校验通过后接入配置数据依赖；本阶段不读写项目文件。
    toml::table project;
    project.insert("project", config.project);
    const auto identifier = project["project"].value<std::string_view>();
    return identifier && *identifier == config.project;
}

void ProjectSystem::on_shutdown() noexcept
{
}

} // namespace engine
