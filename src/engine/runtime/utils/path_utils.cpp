#include "runtime/utils/path_utils.hpp"

namespace engine
{

std::filesystem::path PathUtils::from_utf8(std::string_view text)
{
    if (text.empty())
    {
        return {};
    }
    return std::filesystem::path(
        std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size()));
}

std::string PathUtils::to_utf8(const std::filesystem::path& path)
{
    const auto text = path.generic_u8string();
    return std::string(reinterpret_cast<const char*>(text.data()), text.size());
}

} // namespace engine
