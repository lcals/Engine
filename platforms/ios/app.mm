#include "runtime/app/app.hpp"

#include "runtime/utils/path_utils.hpp"

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_stdinc.h>

#import <Foundation/Foundation.h>

#include <cstdlib>
#include <string_view>
#include <system_error>
#include <utility>

namespace engine
{
namespace
{

struct AppState
{
    std::filesystem::path assets_path;
    std::filesystem::path persistent_data_path;
    std::filesystem::path cache_path;
    std::string game_language;
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

bool default_directory(NSSearchPathDirectory directory, const std::string& name,
                       std::filesystem::path& output)
{
    @autoreleasepool
    {
        NSURL* root =
            [[[NSFileManager defaultManager] URLsForDirectory:directory
                                                    inDomains:NSUserDomainMask] firstObject];
        const char* utf8 = [[root path] UTF8String];
        if (utf8 == nullptr)
        {
            return false;
        }
        output = PathUtils::from_utf8(utf8) / PathUtils::from_utf8(name);
        return output.is_absolute();
    }
}

bool exclude_downloaded_assets(const std::filesystem::path& persistent)
{
    const auto downloaded = persistent / "assets";
    if (!prepare_directory(downloaded))
    {
        return false;
    }
    @autoreleasepool
    {
        NSString* path = [NSString stringWithUTF8String:PathUtils::to_utf8(downloaded).c_str()];
        if (path == nil)
        {
            return false;
        }
        NSURL* url = [NSURL fileURLWithPath:path isDirectory:YES];
        NSError* error = nil;
        return [url setResourceValue:@YES forKey:NSURLIsExcludedFromBackupKey error:&error];
    }
}

} // namespace

bool App::initialize(const AppConfig& config)
{
    if (state().initialized || !valid_application_name(config.application_name))
    {
        return false;
    }

    AppState candidate;
    auto assets = config.assets_path;
    if (assets.empty())
    {
        const char* base_path = SDL_GetBasePath();
        if (base_path == nullptr)
        {
            return false;
        }
        assets = PathUtils::from_utf8(base_path) / "assets";
    }

    auto persistent = config.persistent_data_path;
    if (persistent.empty() &&
        !default_directory(NSApplicationSupportDirectory, config.application_name, persistent))
    {
        return false;
    }

    auto cache = config.cache_path;
    if ((cache.empty() && !default_directory(NSCachesDirectory, config.application_name, cache)) ||
        !normalize_path(assets, candidate.assets_path) ||
        !normalize_path(persistent, candidate.persistent_data_path) ||
        !normalize_path(cache, candidate.cache_path) ||
        candidate.persistent_data_path == candidate.cache_path)
    {
        return false;
    }

    std::error_code error;
    const bool builtin_exists = std::filesystem::exists(candidate.assets_path, error);
    if (error ||
        (builtin_exists &&
         (!std::filesystem::is_directory(candidate.assets_path, error) || error)) ||
        !prepare_directory(candidate.persistent_data_path) ||
        !prepare_directory(candidate.cache_path) ||
        !exclude_downloaded_assets(candidate.persistent_data_path))
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
    value.assets_path.clear();
    value.persistent_data_path.clear();
    value.cache_path.clear();
    value.game_language.clear();
}

bool App::is_initialized() noexcept
{
    return state().initialized;
}

const std::filesystem::path& App::assets_path() noexcept
{
    return state().assets_path;
}

const std::filesystem::path& App::persistent_data_path() noexcept
{
    return state().persistent_data_path;
}

const std::filesystem::path& App::cache_path() noexcept
{
    return state().cache_path;
}

void App::set_game_language(std::string_view language)
{
    state().game_language = language;
}

const std::string& App::game_language() noexcept
{
    return state().game_language;
}

} // namespace engine
