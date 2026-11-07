#include "runtime/resource/codecs/prefab_resource_codec.hpp"

#include "runtime/resource/codecs/scene_resource_codec.hpp"
#include <set>

#include <flatbuffers/verifier.h>

#include <map>
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
        if (!node || !node->info || !node->info->properties || node->info->properties->id == 0 ||
            !nodes.emplace(node->info->properties->id, node.get()).second)
        {
            return false;
        }
        std::set<schema::ComponentPayload> types;
        for (const auto& component : node->components)
        {
            if (!component || !component->flags || !component->payload.value ||
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

schema::AssetKind PrefabResourceCodec::kind() const noexcept
{
    return schema::AssetKind::prefab;
}

bool PrefabResourceCodec::decode(const schema::AssetEntryT& entry, ByteView payload,
                                 std::unique_ptr<Resource>& output, std::string& error) const
{
    if (output)
    {
        error = "Resource output must be empty";
        return false;
    }
    error.clear();
    if (entry.kind != kind() || payload.empty() || payload.size() > 64U * 1024U * 1024U)
    {
        error = "Invalid resource kind or payload size";
        return false;
    }
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(payload.data());
    flatbuffers::Verifier verifier(bytes, payload.size());
    if (!schema::VerifyPrefabDataBuffer(verifier))
    {
        error = "Invalid Prefab FlatBuffer";
        return false;
    }
    auto data = schema::UnPackPrefabData(bytes);
    output = std::make_unique<ResourceType>(entry, std::move(*data));
    return true;
}

bool PrefabResourceCodec::validate_data(const Resource& resource, std::string& error) const
{
    const auto* data = resource_detail::data<ResourceType>(&resource);
    if (!data || !(validate(*data) && SceneResourceCodec::validate_nodes(resource, data->nodes)))
    {
        error = "Invalid Prefab data or declared dependencies";
        return false;
    }
    error.clear();
    return true;
}

bool PrefabResourceCodec::validate_dependencies(const Resource& resource, ResourceView dependencies,
                                                std::string& error) const
{
    const auto* prefab = resource_detail::data<PrefabResource>(&resource);
    return prefab && SceneResourceCodec::validate_node_dependencies(prefab->nodes, dependencies);
}
} // namespace engine
