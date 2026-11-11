#include "runtime/storage/storage_file.hpp"

#include "runtime/utils/path_utils.hpp"

#include <SDL3/SDL.h>

#include <cerrno>

namespace engine::storage_detail
{

std::FILE* open_exclusive_file(const std::filesystem::path& path, std::error_code& error)
{
    error.clear();
    const auto encoded = PathUtils::to_utf8(path);
    auto* file = std::fopen(encoded.c_str(), "wbx");
    if (file == nullptr)
    {
        error = {errno, std::generic_category()};
    }
    return file;
}

bool publish_storage_file(const std::filesystem::path& temporary,
                          const std::filesystem::path& target, std::string& error)
{
    error.clear();
    if (!SDL_RenamePath(PathUtils::to_utf8(temporary).c_str(), PathUtils::to_utf8(target).c_str()))
    {
        error = "Cannot publish persistent file: " + std::string(SDL_GetError());
        return false;
    }
    return true;
}

} // namespace engine::storage_detail
