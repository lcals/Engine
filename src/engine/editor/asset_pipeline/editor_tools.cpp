#include "editor/asset_pipeline/editor_tools.hpp"

#include "runtime/utils/path_utils.hpp"

#include <system_error>
#include <utility>

namespace engine::editor::asset_pipeline
{
namespace
{
bool resolve_path(const std::filesystem::path& root, std::string_view relative,
                  const std::filesystem::path& fallback, std::filesystem::path& path,
                  std::string& error)
{
    path.clear();
    auto candidate = root.empty() ? fallback : root / PathUtils::from_utf8(relative);
    std::error_code code;
    if (candidate.empty() || !std::filesystem::exists(candidate, code) || code)
    {
        error = "editor import tool or data is missing: " + PathUtils::to_utf8(candidate);
        return false;
    }
    path = std::move(candidate);
    return true;
}
} // namespace

bool editor_tool_path(std::string_view name, const std::filesystem::path& fallback,
                      std::filesystem::path& path, std::string& error)
{
    std::filesystem::path executables;
    std::filesystem::path data;
    path.clear();
    // 与配置时选定的宿主工具保持相同后缀，不把平台判断散到各导入器。
    const auto filename = std::string(name) + PathUtils::to_utf8(fallback.extension());
    return editor_tool_directories(executables, data, error) &&
           resolve_path(executables, filename, fallback, path, error);
}

bool editor_tool_data_path(std::string_view relative, const std::filesystem::path& fallback,
                           std::filesystem::path& path, std::string& error)
{
    std::filesystem::path executables;
    std::filesystem::path data;
    path.clear();
    return editor_tool_directories(executables, data, error) &&
           resolve_path(data, relative, fallback, path, error);
}
} // namespace engine::editor::asset_pipeline
