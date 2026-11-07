#include "plugins/runtime/effects/effect_resource_path.hpp"

#include "runtime/core/guid_map.hpp"
#include "runtime/scene/resource_binding.hpp"
#include "runtime/utils/path_utils.hpp"

#include <flatbuffers/flatbuffers.h>

#include <algorithm>
#include <filesystem>
#include <set>
#include <utility>

namespace engine::plugins::effects::detail
{

std::string effect_dependency_path(std::string_view path)
{
    std::string normalized(path);
    std::replace(normalized.begin(), normalized.end(), '\\', '/');
    return PathUtils::to_utf8(PathUtils::from_utf8(normalized).lexically_normal());
}

bool load_effect_bindings(const resource_detail::ResourceReference& resource,
                          const schema::EffectAssetDataT& asset, EffectBindings& output,
                          std::string& error)
{
    output.clear();
    EffectBindings candidate;
    for (const auto& binding : asset.bindings)
    {
        resource_detail::ResourceReference dependency;
        if (!binding ||
            !dependency.dependency(resource.loader(), resource.handle(), binding->guid) ||
            dependency->kind() != binding->kind)
        {
            error = "Effect dependency is not available in its loaded closure";
            return false;
        }
        candidate.emplace(binding->path, std::move(dependency));
    }
    output = std::move(candidate);
    return true;
}

std::string effect_utf8(const char16_t* path)
{
    return path != nullptr ? PathUtils::to_utf8(std::filesystem::path(path)) : std::string{};
}

} // namespace engine::plugins::effects::detail
