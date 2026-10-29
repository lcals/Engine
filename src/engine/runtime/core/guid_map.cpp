#include "runtime/core/guid_map.hpp"

namespace engine
{
namespace
{
bool valid_path(std::string_view path) noexcept
{
    if ((!path.starts_with("builtin/") && !path.starts_with("project/")) || path.back() == '/')
    {
        return false;
    }
    for (const unsigned char character : path)
    {
        if (character < 32 || character == 127 || character == '\\' || character == ':')
        {
            return false;
        }
    }
    std::size_t offset = 0;
    while (offset < path.size())
    {
        const auto separator = path.find('/', offset);
        const auto segment =
            path.substr(offset, separator == std::string_view::npos ? path.size() - offset
                                                                    : separator - offset);
        if (segment.empty() || segment == "." || segment == "..")
        {
            return false;
        }
        if (separator == std::string_view::npos)
        {
            break;
        }
        offset = separator + 1;
    }
    return true;
}
} // namespace

bool GuidMap::valid_guid(std::string_view guid) noexcept
{
    if (guid.size() != 36)
    {
        return false;
    }
    bool nonzero = false;
    for (std::size_t index = 0; index < guid.size(); ++index)
    {
        const char character = guid[index];
        if (index == 8 || index == 13 || index == 18 || index == 23)
        {
            if (character != '-')
            {
                return false;
            }
        }
        else if ((character >= '0' && character <= '9') || (character >= 'a' && character <= 'f'))
        {
            nonzero = nonzero || character != '0';
        }
        else
        {
            return false;
        }
    }
    return nonzero;
}

bool GuidMap::add(std::string_view path, std::string_view guid)
{
    if (!valid_path(path) || !valid_guid(guid) || paths_.contains(path) || guids_.contains(guid))
    {
        return false;
    }
    paths_.emplace(path, guid);
    guids_.emplace(guid, path);
    return true;
}

bool GuidMap::path_to_guid(std::string_view path, Guid& output) const
{
    const auto found = paths_.find(path);
    if (found == paths_.end())
    {
        output.clear();
        return false;
    }
    output = found->second;
    return true;
}

bool GuidMap::guid_to_path(std::string_view guid, std::string& output) const
{
    const auto found = guids_.find(guid);
    if (found == guids_.end())
    {
        output.clear();
        return false;
    }
    output = found->second;
    return true;
}

void GuidMap::clear() noexcept
{
    paths_.clear();
    guids_.clear();
}

std::size_t GuidMap::size() const noexcept
{
    return paths_.size();
}

} // namespace engine
