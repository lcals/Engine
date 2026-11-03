#include "runtime/resource/codecs/prefab_resource_codec.hpp"

#include "runtime/resource/codecs/scene_resource_codec.hpp"

#include <flatbuffers/verifier.h>

#include <map>
#include <set>
#include <utility>

namespace engine
{
bool PrefabResourceCodec::validate(const schema::PrefabDataT& data)
{
    if (data.schema_version != 1 || data.root_id == 0 || data.nodes.empty())
    {
        return false;
    }
    std::map<std::uint64_t, const schema::NodeDataT*> nodes;
    for (const auto& node : data.nodes)
    {
        if (!node || !node->info || node->info->id == 0 ||
            !nodes.emplace(node->info->id, node.get()).second)
        {
            return false;
        }
        std::set<schema::ComponentPayload> types;
        for (const auto& component : node->components)
        {
            if (!component || !component->payload.value ||
                !types.insert(component->payload.type).second)
            {
                return false;
            }
        }
        if (!types.contains(schema::ComponentPayload::TransformData))
        {
            return false;
        }
    }
    const auto root = nodes.find(data.root_id);
    if (root == nodes.end() || root->second->info->parent_id != 0)
    {
        return false;
    }
    for (const auto& [id, node] : nodes)
    {
        std::set<std::uint64_t> visited;
        auto current = id;
        while (current != data.root_id)
        {
            const auto found = nodes.find(current);
            if (found == nodes.end() || !visited.insert(current).second)
            {
                return false;
            }
            current = found->second->info->parent_id;
        }
    }
    return true;
}

bool PrefabResourceCodec::decode(const ResourceCatalog& catalog, ByteView payload,
                                 Resource& output) const
{
    flatbuffers::Verifier verifier(reinterpret_cast<const std::uint8_t*>(payload.data()),
                                   payload.size());
    if (!schema::VerifyPrefabDataBuffer(verifier))
    {
        return false;
    }
    auto data = schema::UnPackPrefabData(payload.data());
    if (!data || !validate(*data) ||
        !SceneResourceCodec::validate_nodes(catalog, output, data->nodes))
    {
        return false;
    }
    store(output, std::move(data));
    return true;
}

bool PrefabResourceCodec::validate_dependencies(const ResourceCatalog& catalog,
                                                const Resource& resource,
                                                std::span<const ResourcePtr> dependencies) const
{
    const auto* prefab = resource.prefab();
    return prefab &&
           SceneResourceCodec::validate_node_dependencies(catalog, prefab->nodes, dependencies);
}
} // namespace engine
