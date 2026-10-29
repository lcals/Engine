#include "editor/editor_environment.hpp"
#include "editor/project/project_system.hpp"
#include "runtime/app/app.hpp"
#include "runtime/engine_config.hpp"
#include "runtime/resource/resource_system.hpp"
#include "runtime/utils/path_utils.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace
{
#include <cstddef>
#include <cstdint>
#include <memory>
#include <system_error>
#include <utility>

namespace fs = std::filesystem;

bool expect(bool condition, const char* message)
{
    if (!condition)
    {
        std::fprintf(stderr, "editor environment test: %s\n", message);
    }
    return condition;
}

bool directory(const fs::path& path)
{
    std::error_code error;
    fs::create_directories(path, error);
    return !error;
}

bool absent(const fs::path& path)
{
    std::error_code error;
    const bool exists = fs::exists(path, error);
    return !error && !exists;
}

bool write(const fs::path& path, const std::string& text)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << text;
    output.close();
    return static_cast<bool>(output);
}

std::string read(const fs::path& path)
{
    std::ifstream input(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

bool run_cases(const fs::path& workspace)
{
    const auto root = workspace / "project";
    const auto persistent = workspace / "persistent";
    if (!directory(root / "settings") || !directory(root / "cache") || !directory(root / "assets"))
    {
        return false;
    }
    engine::ProjectSystem project(engine::PathUtils::to_utf8(root));
    engine::EditorEnvironment environment(project, persistent);
    const engine::EngineConfig config;
    if (!expect(!environment.initialize() && !engine::App::is_initialized(),
                "uninitialized project cannot acquire App") ||
        !expect(project.initialize(config) && environment.initialize(),
                "initialize editor paths") ||
        !expect(engine::App::builtin_assets_path() == root / "cache",
                "editor maps compiled cache directly without a package snapshot") ||
        !expect(engine::App::persistent_data_path() == persistent, "map persistent storage") ||
        !expect(!environment.initialize() && environment.is_initialized(),
                "duplicate initialization preserves the current owner") ||
        !write(root / "cache" / "keep", "compiled") || !write(persistent / "keep", "persistent"))
    {
        return false;
    }
    project.shutdown();
    if (!expect(!environment.is_initialized(), "project lifetime covers environment use"))
    {
        return false;
    }
    environment.shutdown();
    environment.shutdown();
    if (!expect(!engine::App::is_initialized(), "repeated shutdown is safe") ||
        !expect(read(root / "cache" / "keep") == "compiled" &&
                    read(persistent / "keep") == "persistent",
                "closing environment preserves compiled and persistent resources") ||
        !expect(absent(root / "cache" / "editor-builtin"),
                "no byte snapshot directory is created") ||
        !expect(project.initialize(config), "restart project"))
    {
        return false;
    }
    engine::AppConfig external;
    external.application_name = "engine-environment-test";
    external.builtin_assets_path = root / "assets";
    external.persistent_data_path = workspace / "external-persistent";
    external.cache_path = workspace / "external-cache";
    if (!expect(engine::App::initialize(external), "initialize another App owner") ||
        !expect(!environment.initialize(), "reject a competing App owner"))
    {
        return false;
    }
    environment.shutdown();
    if (!expect(engine::App::is_initialized(), "failed initialization cannot close another owner"))
    {
        return false;
    }
    engine::App::shutdown();
    const auto invalid_persistent = workspace / "persistent-file";
    if (!write(invalid_persistent, "file"))
    {
        return false;
    }
    engine::EditorEnvironment invalid(project, invalid_persistent);
    if (!expect(!invalid.initialize() && !engine::App::is_initialized(),
                "path failure leaves App unowned"))
    {
        return false;
    }
    {
        engine::EditorEnvironment scoped(project, persistent);
        if (!expect(scoped.initialize(), "initialize scoped environment"))
        {
            return false;
        }
        engine::App::shutdown();
        if (!expect(!scoped.is_initialized(), "premature App shutdown invalidates environment"))
        {
            return false;
        }
    }
    return expect(!engine::App::is_initialized() && read(root / "cache" / "keep") == "compiled",
                  "destructor only releases owned environment state");
}
} // namespace

int main(int argc, char* argv[])
{
    if (argc != 2 || argv[1][0] == '\0')
    {
        return 2;
    }
    std::error_code error;
    const auto workspace = fs::absolute(fs::path(argv[1]) / "editor-environment-cases", error);
    if (error)
    {
        return 1;
    }
    fs::remove_all(workspace, error);
    if (error || !directory(workspace))
    {
        return 1;
    }
    const bool passed = run_cases(workspace);
    engine::App::shutdown();
    fs::remove_all(workspace, error);
    return passed && !error ? 0 : 1;
}
