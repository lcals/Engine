#include "runtime/app/app.hpp"

#include <system_error>
#include <utility>

namespace engine
{
namespace
{
AppConfig paths;
std::string language;
bool initialized = false;

bool directory(const std::filesystem::path& path)
{
    std::error_code error;
    std::filesystem::create_directories(path, error);
    return !error && std::filesystem::is_directory(path, error) && !error;
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
} // namespace

bool App::initialize(const AppConfig& config)
{
    if (initialized || !valid_application_name(config.application_name))
    {
        return false;
    }
    auto candidate = config;
    if (candidate.assets_path.empty())
    {
        candidate.assets_path = "/assets";
    }
    if (candidate.persistent_data_path.empty())
    {
        candidate.persistent_data_path =
            std::filesystem::path("/persistent") / config.application_name;
    }
    if (candidate.cache_path.empty())
    {
        candidate.cache_path = std::filesystem::path("/cache") / config.application_name;
    }
    for (auto* path :
         {&candidate.assets_path, &candidate.persistent_data_path, &candidate.cache_path})
    {
        if (path->native().find('\0') != std::string::npos)
        {
            return false;
        }
        std::error_code error;
        *path = std::filesystem::absolute(*path, error).lexically_normal();
        if (error)
        {
            return false;
        }
    }
    if (candidate.persistent_data_path == candidate.cache_path ||
        !directory(candidate.persistent_data_path) || !directory(candidate.cache_path))
    {
        return false;
    }
    std::error_code error;
    const bool exists = std::filesystem::exists(candidate.assets_path, error);
    if (error || (exists && !std::filesystem::is_directory(candidate.assets_path, error)) || error)
    {
        return false;
    }
    paths = std::move(candidate);
    language.clear();
    initialized = true;
    return true;
}

void App::shutdown() noexcept
{
    initialized = false;
    paths = {};
    language.clear();
}
bool App::is_initialized() noexcept
{
    return initialized;
}
const std::filesystem::path& App::assets_path() noexcept
{
    return paths.assets_path;
}
const std::filesystem::path& App::persistent_data_path() noexcept
{
    return paths.persistent_data_path;
}
const std::filesystem::path& App::cache_path() noexcept
{
    return paths.cache_path;
}
void App::set_game_language(std::string_view value)
{
    language = value;
}
const std::string& App::game_language() noexcept
{
    return language;
}
} // namespace engine
