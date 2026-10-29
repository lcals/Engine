#include "runtime/app/app.hpp"

#include "runtime/utils/path_utils.hpp"

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_stdinc.h>

#include <cstdlib>
#include <string_view>
#include <system_error>
#include <utility>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

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

bool normalize_path(const std::filesystem::path& input, std::filesystem::path& output)
{
    if (input.empty() || input.native().find(std::filesystem::path::value_type{}) !=
                             std::filesystem::path::string_type::npos)
    {
        return false;
    }
    std::error_code error;
    auto absolute_path = std::filesystem::absolute(input, error);
    if (error)
    {
        return false;
    }
    output = std::filesystem::weakly_canonical(absolute_path, error);
    return !error;
}

bool prepare_directory(const std::filesystem::path& path)
{
    std::error_code error;
    std::filesystem::create_directories(path, error);
    if (error)
    {
        return false;
    }
    return std::filesystem::is_directory(path, error) && !error;
}

bool default_cache_path(const std::string& application_name, std::filesystem::path& output)
{
#if defined(_WIN32)
    // 使用宽字符环境值，避免用户目录中的非 ASCII 名称丢失。
    const DWORD required = GetEnvironmentVariableW(L"LOCALAPPDATA", nullptr, 0);
    if (required == 0)
    {
        return false;
    }
    std::wstring local_data(required, L'\0');
    const DWORD written = GetEnvironmentVariableW(L"LOCALAPPDATA", local_data.data(), required);
    if (written == 0 || written >= required)
    {
        return false;
    }
    local_data.resize(written);
    output = std::filesystem::path(local_data) / PathUtils::from_utf8(application_name.c_str()) /
             "cache";
#elif defined(__APPLE__)
    const char* home_path = SDL_GetUserFolder(SDL_FOLDER_HOME);
    if (home_path == nullptr)
    {
        return false;
    }
    output = PathUtils::from_utf8(home_path) / "Library" / "Caches" /
             PathUtils::from_utf8(application_name.c_str());
#else
    const char* xdg_cache = std::getenv("XDG_CACHE_HOME");
    if (xdg_cache != nullptr && *xdg_cache != '\0' && PathUtils::from_utf8(xdg_cache).is_absolute())
    {
        output = PathUtils::from_utf8(xdg_cache) / PathUtils::from_utf8(application_name.c_str());
        return true;
    }
    const char* home_path = SDL_GetUserFolder(SDL_FOLDER_HOME);
    if (home_path == nullptr)
    {
        return false;
    }
    output =
        PathUtils::from_utf8(home_path) / ".cache" / PathUtils::from_utf8(application_name.c_str());
#endif
    return output.is_absolute();
}

} // namespace

bool App::initialize(const AppConfig& config)
{
    if (state().initialized || !valid_application_name(config.application_name))
    {
        return false;
    }

    AppState candidate;
    auto builtin = config.builtin_assets_path;
    if (builtin.empty())
    {
        const char* base_path = SDL_GetBasePath();
        if (base_path == nullptr)
        {
            return false;
        }
        builtin = PathUtils::from_utf8(base_path);
    }

    auto persistent = config.persistent_data_path;
    if (persistent.empty())
    {
        char* pref_path = SDL_GetPrefPath(nullptr, config.application_name.c_str());
        if (pref_path == nullptr)
        {
            return false;
        }
        persistent = PathUtils::from_utf8(pref_path);
        SDL_free(pref_path);
    }

    auto cache = config.cache_path;
    if ((cache.empty() && !default_cache_path(config.application_name, cache)) ||
        !normalize_path(builtin, candidate.builtin_assets_path) ||
        !normalize_path(persistent, candidate.persistent_data_path) ||
        !normalize_path(cache, candidate.cache_path) ||
        candidate.persistent_data_path == candidate.cache_path)
    {
        return false;
    }

    std::error_code error;
    const bool builtin_exists = std::filesystem::exists(candidate.builtin_assets_path, error);
    if (error ||
        (builtin_exists &&
         (!std::filesystem::is_directory(candidate.builtin_assets_path, error) || error)) ||
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
