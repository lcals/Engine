#include "engine_config.hpp"
#include "project_system.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

namespace
{

namespace fs = std::filesystem;

[[nodiscard]] bool expect(bool condition, const char* message)
{
    if (!condition)
    {
        std::fprintf(stderr, "project system test: %s\n", message);
    }
    return condition;
}

[[nodiscard]] bool make_directory(const fs::path& path)
{
    std::error_code error;
    fs::create_directories(path, error);
    if (error)
    {
        std::fprintf(stderr, "Cannot create test directory: %s\n", error.message().c_str());
        return false;
    }
    return true;
}

[[nodiscard]] bool remove_path(const fs::path& path)
{
    std::error_code error;
    fs::remove_all(path, error);
    if (error)
    {
        std::fprintf(stderr, "Cannot remove test path: %s\n", error.message().c_str());
        return false;
    }
    return true;
}

[[nodiscard]] bool create_project(const fs::path& root)
{
    return make_directory(root / "settings") && make_directory(root / "cache") &&
           make_directory(root / "assets");
}

[[nodiscard]] bool create_file(const fs::path& path)
{
    std::ofstream file(path, std::ios::binary);
    file << "This file must not be accepted as a project directory.\n";
    file.close();
    return expect(static_cast<bool>(file), "create ordinary file fixture");
}

[[nodiscard]] bool check_rejected(engine::ProjectSystem& project,
                                  const engine::EngineConfig& config)
{
    if (!expect(!project.initialize(config), "invalid project must fail initialization") ||
        !expect(!project.is_initialized(), "failed project must remain uninitialized"))
    {
        return false;
    }
    project.shutdown();
    project.shutdown();
    return expect(!project.tick(0.0), "failed project must reject updates");
}

[[nodiscard]] bool check_ready(engine::ProjectSystem& project, const engine::EngineConfig& config)
{
    if (!expect(project.initialize(config), "valid project must initialize") ||
        !expect(project.is_initialized(), "valid project must be initialized") ||
        !expect(!project.initialize(config), "initializing an active project must fail") ||
        !expect(project.is_initialized(), "rejected reinitialization must preserve ready state") ||
        !expect(project.tick(0.0), "ready project must accept updates"))
    {
        return false;
    }
    project.shutdown();
    project.shutdown();
    return expect(!project.is_initialized(), "repeated shutdown must leave project stopped") &&
           expect(!project.tick(0.0), "stopped project must reject updates");
}

[[nodiscard]] bool run_cases(const fs::path& workspace)
{
    engine::ProjectSystem project;
    engine::EngineConfig config;
    if (!check_rejected(project, config))
    {
        return false;
    }

    const fs::path valid_root = workspace / "valid";
    if (!create_project(valid_root))
    {
        return false;
    }
    const engine::EngineConfig valid_config{valid_root.string()};
    if (!check_ready(project, valid_config) || !check_ready(project, valid_config))
    {
        return false;
    }

    config.project = (workspace / "missing-root").string();
    if (!check_rejected(project, config) || !check_ready(project, valid_config))
    {
        return false;
    }
    const fs::path root_file = workspace / "root-file";
    config.project = root_file.string();
    if (!create_file(root_file) || !check_rejected(project, config) ||
        !check_ready(project, valid_config))
    {
        return false;
    }

    for (const char* directory : {"settings", "cache", "assets"})
    {
        const fs::path root = workspace / directory;
        const fs::path required = root / directory;
        config.project = root.string();
        if (!create_project(root) || !remove_path(required) || !check_rejected(project, config))
        {
            return false;
        }
        std::error_code error;
        const bool exists = fs::exists(required, error);
        if (!expect(!error && !exists, "validation must not create missing directories") ||
            !make_directory(required) || !check_ready(project, config))
        {
            return false;
        }

        if (!remove_path(required) || !create_file(required) || !check_rejected(project, config))
        {
            return false;
        }
        const bool is_file = fs::is_regular_file(required, error);
        if (!expect(!error && is_file, "validation must preserve a wrongly placed ordinary file") ||
            !remove_path(required) || !make_directory(required) || !check_ready(project, config))
        {
            return false;
        }
    }
    return true;
}

} // namespace

int main(int argc, char* argv[])
{
    if (argc != 2 || argv[1][0] == '\0')
    {
        std::fputs("Expected an isolated test output directory\n", stderr);
        return 2;
    }

    const fs::path workspace = fs::path(argv[1]) / "project-system-cases";
    if (!remove_path(workspace) || !make_directory(workspace))
    {
        return 1;
    }
    const bool passed = run_cases(workspace);
    const bool cleaned = remove_path(workspace);
    if (!passed || !cleaned)
    {
        return 1;
    }
    std::puts("Project directories, failed initialization recovery, and repeated lifecycle passed");
    return 0;
}
