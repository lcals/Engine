#include "runtime/storage/storage_file.hpp"

#include "runtime/utils/path_utils.hpp"

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

} // namespace engine::storage_detail
