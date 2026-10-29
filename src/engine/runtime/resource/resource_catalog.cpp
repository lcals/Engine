#include "runtime/resource/resource_catalog.hpp"

#include "runtime/resource/builtin_resources.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <set>
#include <vector>

namespace engine
{
namespace
{

bool valid_relative_path(std::string_view value)
{
    if (value.empty())
    {
        return false;
    }
    for (const unsigned char character : value)
    {
        if (character < 32 || character == 127 || character == '\\' || character == ':')
        {
            return false;
        }
    }
    for (;;)
    {
        const auto slash = value.find('/');
        const auto segment = value.substr(0, slash);
        if (segment.empty() || segment == "." || segment == "..")
        {
            return false;
        }
        if (slash == std::string_view::npos)
        {
            return true;
        }
        value.remove_prefix(slash + 1);
    }
}

bool valid_entry(const schema::AssetEntryT& entry)
{
    constexpr std::uint64_t maximum_size = 64U * 1024U * 1024U;
    constexpr auto maximum_offset =
        static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
    return entry.kind >= schema::AssetKind::source && entry.kind <= schema::AssetKind::MAX &&
           entry.path.starts_with("project/") && entry.format_version == 1 &&
           entry.content_hash.size() == 16 &&
           std::all_of(
               entry.content_hash.begin(), entry.content_hash.end(), [](char value)
               { return (value >= '0' && value <= '9') || (value >= 'a' && value <= 'f'); }) &&
           entry.header_size >= 16 && entry.header_size <= 65536 &&
           entry.header_size <= entry.content_size && entry.content_size <= maximum_size &&
           entry.location && valid_relative_path(entry.location->mount) &&
           entry.location->mount.find('/') == std::string::npos &&
           valid_relative_path(entry.location->path) &&
           entry.location->length == entry.content_size &&
           entry.location->offset <= maximum_offset &&
           entry.location->length <= maximum_offset - entry.location->offset;
}

} // namespace

bool ResourceCatalog::assign(const schema::AssetCatalogDataT& data)
{
    if (data.schema_version != 2 || data.revision.empty() || data.entries.size() > 65536 ||
        (data.target_platform != "macos" && data.target_platform != "ios" &&
         data.target_platform != "windows" && data.target_platform != "android" &&
         data.target_platform != "linux"))
    {
        return false;
    }
    ResourceCatalog candidate;
    for (const auto& entry : builtin_resource_catalog().entries)
    {
        if (!candidate.identities_.add(entry->path, entry->guid))
        {
            return false;
        }
    }
    std::set<std::string> modules;
    for (std::size_t index = 0; index < data.entries.size(); ++index)
    {
        const auto& entry = data.entries[index];
        if (!entry || !valid_entry(*entry) || !candidate.identities_.add(entry->path, entry->guid))
        {
            return false;
        }
        if (entry->kind == schema::AssetKind::script &&
            (!valid_relative_path(entry->module_path) || !entry->module_path.ends_with(".js") ||
             !modules.insert(entry->module_path).second))
        {
            return false;
        }
        candidate.entries_.emplace(entry->guid, index);
    }

    // 拓扑消除验证必要依赖：共享允许，缺失、自依赖和任何长度的环均拒绝。
    std::vector<std::size_t> remaining(data.entries.size());
    std::vector<std::vector<std::size_t>> users(data.entries.size());
    std::vector<std::size_t> ready;
    for (std::size_t index = 0; index < data.entries.size(); ++index)
    {
        std::set<std::string> dependencies;
        for (const auto& dependency : data.entries[index]->dependencies)
        {
            if (!dependencies.insert(dependency).second)
            {
                return false;
            }
            const auto found = candidate.entries_.find(dependency);
            if (found != candidate.entries_.end())
            {
                ++remaining[index];
                users[found->second].push_back(index);
            }
            else if (builtin_resource_info(dependency) == nullptr)
            {
                return false;
            }
        }
        if (remaining[index] == 0)
        {
            ready.push_back(index);
        }
    }
    for (std::size_t next = 0; next < ready.size(); ++next)
    {
        for (const auto user : users[ready[next]])
        {
            if (--remaining[user] == 0)
            {
                ready.push_back(user);
            }
        }
    }
    if (ready.size() != data.entries.size())
    {
        return false;
    }
    candidate.data_ = data;
    *this = std::move(candidate);
    return true;
}

const schema::AssetEntryT* ResourceCatalog::find(std::string_view guid) const noexcept
{
    const auto found = entries_.find(guid);
    return found == entries_.end() ? builtin_resource_info(guid)
                                   : data_.entries[found->second].get();
}

const GuidMap& ResourceCatalog::identities() const noexcept
{
    return identities_;
}

const schema::AssetCatalogDataT& ResourceCatalog::data() const noexcept
{
    return data_;
}

std::string_view ResourceCatalog::revision() const noexcept
{
    return data_.revision;
}

} // namespace engine
