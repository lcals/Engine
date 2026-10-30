#include "editor/asset_pipeline/editor_tools.hpp"

#include "runtime/utils/path_utils.hpp"

#include <SDL3/SDL_filesystem.h>

#include <system_error>

namespace engine::editor::asset_pipeline
{
bool editor_tool_directories(std::filesystem::path& executables, std::filesystem::path& data,
                             std::string& error)
{
    executables.clear();
    data.clear();
    const char* base = SDL_GetBasePath();
    if (base == nullptr)
    {
        error = "cannot locate editor executable directory";
        return false;
    }
    const auto directory = PathUtils::from_utf8(base) / "tools";
    std::error_code code;
    const bool exists = std::filesystem::exists(directory, code);
    if (code)
    {
        error = "cannot inspect deployed editor tools: " + PathUtils::to_utf8(directory);
        return false;
    }
    if (exists)
    {
        executables = directory;
        data = directory;
    }
    return true;
}
} // namespace engine::editor::asset_pipeline
