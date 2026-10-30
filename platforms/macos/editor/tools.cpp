#include "editor/asset_pipeline/editor_tools.hpp"

#include "runtime/utils/path_utils.hpp"

#include <mach-o/dyld.h>

#include <cstdint>
#include <system_error>
#include <vector>

namespace engine::editor::asset_pipeline
{
bool editor_tool_directories(std::filesystem::path& executables, std::filesystem::path& data,
                             std::string& error)
{
    executables.clear();
    data.clear();
    std::uint32_t size = 0;
    static_cast<void>(_NSGetExecutablePath(nullptr, &size));
    std::vector<char> buffer(size);
    if (size == 0 || _NSGetExecutablePath(buffer.data(), &size) != 0)
    {
        error = "cannot locate editor executable";
        return false;
    }
    std::error_code code;
    const auto executable = std::filesystem::canonical(PathUtils::from_utf8(buffer.data()), code);
    if (code)
    {
        error = "cannot resolve editor executable";
        return false;
    }
    const auto directory = executable.parent_path();
    const auto contents = directory.parent_path();
    if (directory.filename() == "MacOS" && contents.filename() == "Contents" &&
        contents.parent_path().extension() == ".app")
    {
        executables = contents / "Helpers";
        data = contents / "Resources" / "tools";
        return true;
    }
    const auto tools = directory / "tools";
    const bool exists = std::filesystem::exists(tools, code);
    if (code)
    {
        error = "cannot inspect deployed editor tools: " + PathUtils::to_utf8(tools);
        return false;
    }
    if (exists)
    {
        executables = tools;
        data = tools;
    }
    return true;
}
} // namespace engine::editor::asset_pipeline
