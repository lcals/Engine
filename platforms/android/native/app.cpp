#include "runtime/app/app.hpp"

#include "runtime/utils/path_utils.hpp"

#include <SDL3/SDL_system.h>

#include <string_view>
#include <system_error>
#include <utility>

namespace engine
{
namespace
{

struct AppState
{
    std::filesystem::path builtin_assets_path;
    std::filesystem::path persistent_data_path;
    std::filesystem::path cache_path;
    bool initialized = false;
};

AppState& state()
{
    static AppState value;
    return value;
}

bool valid_application_name(std::string_view name)
{
    if (name.empty() || name == "." || name == "..")
    {
        return false;
    }
    for (const unsigned char character : name)
    {
        if (character < 32 || character == '/' || character == '\\' || character == ':' ||
            character == '*' || character == '?' || character == '"' || character == '<' ||
            character == '>' || character == '|')
        {
            return false;
        }
    }
    return name.back() != ' ' && name.back() != '.';
}

bool valid_apk_root(std::string_view path)
{
    constexpr std::string_view root = "apk:/assets";
    if (path == root)
    {
        return true;
    }
    if (!path.starts_with(root) || path.size() <= root.size() + 1 || path[root.size()] != '/')
    {
        return false;
    }
    auto relative = path.substr(root.size() + 1);
    if (relative.find_first_of("\\:") != std::string_view::npos ||
        relative.find('\0') != std::string_view::npos)
    {
        return false;
    }
    for (;;)
    {
        const auto separator = relative.find('/');
        const auto component = relative.substr(0, separator);
        if (component.empty() || component == "." || component == "..")
        {
            return false;
        }
        if (separator == std::string_view::npos)
        {
            return true;
        }
        relative.remove_prefix(separator + 1);
    }
}

bool normalize_disk_path(const std::filesystem::path& input, std::filesystem::path& output)
{
    // 虚拟 APK 根只能作为内置资源来源，不能作为可写目录。
    const auto text = PathUtils::to_utf8(input);
    if (text.empty() || text.starts_with("apk:") || text.find('\0') != std::string::npos)
    {
        return false;
    }
    std::error_code error;
    const auto absolute = std::filesystem::absolute(input, error);
    if (error)
    {
        return false;
    }
    output = std::filesystem::weakly_canonical(absolute, error);
    return !error;
}

bool prepare_directory(const std::filesystem::path& path)
{
    std::error_code error;
    std::filesystem::create_directories(path, error);
    return !error && std::filesystem::is_directory(path, error) && !error;
}

} // namespace

bool App::initialize(const AppConfig& config)
{
    if (state().initialized || !valid_application_name(config.application_name))
    {
        return false;
    }

    AppState candidate;
    const auto builtin = config.builtin_assets_path.empty()
                             ? PathUtils::from_utf8("apk:/assets/project")
                             : config.builtin_assets_path;
    // Android 的原生路径分隔符为 '/'，保留原始内容，不能先规范化不合法的 APK 输入。
    const auto encoded_builtin = builtin.u8string();
    const std::string builtin_text(encoded_builtin.begin(), encoded_builtin.end());
    if (builtin_text.starts_with("apk:"))
    {
        if (!valid_apk_root(builtin_text))
        {
            return false;
        }
        candidate.builtin_assets_path = builtin;
    }
    else
    {
        if (!normalize_disk_path(builtin, candidate.builtin_assets_path))
        {
            return false;
        }
        std::error_code error;
        const bool exists = std::filesystem::exists(candidate.builtin_assets_path, error);
        if (error ||
            (exists &&
             (!std::filesystem::is_directory(candidate.builtin_assets_path, error) || error)))
        {
            return false;
        }
    }

    auto persistent = config.persistent_data_path;
    if (persistent.empty())
    {
        const char* path = SDL_GetAndroidInternalStoragePath();
        if (path == nullptr || *path == '\0')
        {
            return false;
        }
        persistent = PathUtils::from_utf8(path);
    }
    auto cache = config.cache_path;
    if (cache.empty())
    {
        const char* path = SDL_GetAndroidCachePath();
        if (path == nullptr || *path == '\0')
        {
            return false;
        }
        cache = PathUtils::from_utf8(path);
    }
    if (!normalize_disk_path(persistent, candidate.persistent_data_path) ||
        !normalize_disk_path(cache, candidate.cache_path) ||
        candidate.persistent_data_path == candidate.cache_path ||
        !prepare_directory(candidate.persistent_data_path) ||
        !prepare_directory(candidate.cache_path))
    {
        return false;
    }

    candidate.initialized = true;
    state() = std::move(candidate);
    return true;
}

void App::shutdown() noexcept
{
    auto& value = state();
    value.initialized = false;
    value.builtin_assets_path.clear();
    value.persistent_data_path.clear();
    value.cache_path.clear();
}

bool App::is_initialized() noexcept
{
    return state().initialized;
}

const std::filesystem::path& App::builtin_assets_path() noexcept
{
    return state().builtin_assets_path;
}

const std::filesystem::path& App::persistent_data_path() noexcept
{
    return state().persistent_data_path;
}

const std::filesystem::path& App::cache_path() noexcept
{
    return state().cache_path;
}

} // namespace engine
