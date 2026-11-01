#include "runtime/storage/storage_file.hpp"

namespace engine::storage_detail
{

std::FILE* open_exclusive_file(const std::filesystem::path& path, std::error_code& error)
{
    error.clear();
    std::FILE* file = nullptr;
    const auto result = _wfopen_s(&file, path.c_str(), L"wbx");
    if (result != 0)
    {
        error = {result, std::generic_category()};
    }
    return file;
}

} // namespace engine::storage_detail
