#pragma once

#include "runtime/resource/resource_reference.hpp"

#include <set>

namespace engine::editor::resource_access
{
using engine::resource_detail::ResourceReference;

// 编辑器命令是加载流程的调用方；组件本身只获取已加载资源。
[[nodiscard]] inline bool load(ResourceLoader* loader, std::string_view path_or_guid,
                               ResourceReference& output)
{
    if (!loader)
    {
        return false;
    }
    ResourceHandle handle;
    if (!loader->load(path_or_guid, handle))
    {
        return false;
    }
    ResourceReference candidate;
    if (!candidate.adopt(loader, handle))
    {
        (void)loader->release(handle);
        return false;
    }
    output = std::move(candidate);
    return true;
}

[[nodiscard]] inline bool load_versions(ResourceLoader* loader,
                                        std::span<const schema::AssetEntryT> versions,
                                        std::vector<ResourceReference>& output)
{
    std::vector<ResourceReference> candidate;
    for (const auto& version : versions)
    {
        ResourceReference resource;
        if (!load(loader, version.guid, resource) || resource->info() != version)
        {
            return false;
        }
        candidate.push_back(std::move(resource));
    }
    output = std::move(candidate);
    return true;
}

[[nodiscard]] inline bool collect_versions(const ResourceReference& root,
                                           std::vector<schema::AssetEntryT>& output)
{
    if (!root)
    {
        return false;
    }
    std::set<std::string> visited;
    for (const auto& version : output)
    {
        visited.insert(version.guid);
    }
    std::vector<ResourceReference> pending;
    ResourceReference first;
    if (!first.retain(root))
    {
        return false;
    }
    pending.push_back(std::move(first));
    while (!pending.empty())
    {
        auto resource = std::move(pending.back());
        pending.pop_back();
        if (!visited.insert(resource->info().guid).second)
        {
            continue;
        }
        output.push_back(resource->info());
        for (const auto& guid : resource->info().dependencies)
        {
            ResourceReference dependency;
            if (!dependency.dependency(root.loader(), resource.handle(), guid))
            {
                return false;
            }
            pending.push_back(std::move(dependency));
        }
    }
    return true;
}

[[nodiscard]] inline const Resource* get(ResourceLoader* loader, ResourceHandle handle) noexcept
{
    const Resource* resource = nullptr;
    if (loader)
    {
        (void)loader->get(handle, resource);
    }
    return resource;
}

// 只查询已准备的身份，绝不隐式加载。
[[nodiscard]] inline bool guid(ResourceLoader* loader, std::string_view path, std::string& output)
{
    output.clear();
    ResourceReference resource;
    if (!resource.acquire(loader, path))
    {
        return false;
    }
    output = resource->info().guid;
    return true;
}

[[nodiscard]] inline bool load_guid(ResourceLoader* loader, std::string_view path,
                                    std::string& output)
{
    output.clear();
    ResourceReference resource;
    if (!load(loader, path, resource))
    {
        return false;
    }
    output = resource->info().guid;
    return true;
}

[[nodiscard]] inline bool path(ResourceLoader* loader, std::string_view guid, std::string& output)
{
    output.clear();
    ResourceReference resource;
    if (!resource.acquire(loader, guid))
    {
        return false;
    }
    output = resource->info().path;
    return true;
}
} // namespace engine::editor::resource_access
