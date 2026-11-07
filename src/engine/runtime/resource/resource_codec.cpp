#include "runtime/resource/resource_codec.hpp"

#include "runtime/core/guid_map.hpp"

#include <algorithm>

namespace engine
{
bool ResourceCodec::valid_path(std::string_view path) noexcept
{
    return GuidMap::valid_path(path) && !path.starts_with("editor://");
}
bool ResourceCodec::declares(const Resource& resource, std::string_view guid) noexcept
{
    const auto& declared = resource.info().dependencies;
    return GuidMap::valid_guid(guid) &&
           std::find(declared.begin(), declared.end(), guid) != declared.end();
}
const Resource* ResourceCodec::dependency(ResourceView resources,
                                          std::string_view path_or_guid) noexcept
{
    const auto found = std::find_if(resources.begin(), resources.end(),
                                    [path_or_guid](const auto* value)
                                    {
                                        return value && (value->info().guid == path_or_guid ||
                                                         value->info().path == path_or_guid);
                                    });
    return found == resources.end() ? nullptr : *found;
}
} // namespace engine
