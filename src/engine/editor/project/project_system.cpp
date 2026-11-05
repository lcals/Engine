#include "editor/project/project_system.hpp"

#include "runtime/log/log_system.hpp"
#include "runtime/utils/path_utils.hpp"

#include <cstdio>
#include <filesystem>
#include <string>
#include <system_error>
#include <utility>

namespace engine::editor
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

    return true;
}

void ProjectSystem::on_shutdown() noexcept
{
}

} // namespace engine::editor
