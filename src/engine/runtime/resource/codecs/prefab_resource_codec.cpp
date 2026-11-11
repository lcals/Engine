#include "runtime/resource/codecs/prefab_resource_codec.hpp"
#include "runtime/resource/codecs/resource_decode.hpp"

#include "runtime/resource/codecs/scene_resource_codec.hpp"
#include <set>

#include <map>
#include <utility>

namespace engine
{
bool PrefabResourceCodec::validate(const schema::PrefabDataT& data)
{
    if (data.schema_version != 3 || data.root_id == 0 || data.nodes.empty())
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
    const auto validate = [](ByteView payload, std::string& error)
    {
        return resource_decode_detail::verify_flatbuffer(payload, schema::VerifyPrefabDataBuffer,
                                                         "Invalid Prefab FlatBuffer", error);
    };
    return resource_decode_detail::decode_flatbuffer<ResourceType>(
        entry, payload, 64U * 1024U * 1024U, validate,
        [](const std::uint8_t* bytes) { return schema::UnPackPrefabData(bytes); }, output, error);
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
                                                std::string&) const
{
    const auto* prefab = resource_detail::data<PrefabResource>(&resource);
    return prefab && SceneResourceCodec::validate_node_dependencies(prefab->nodes, dependencies);
}
} // namespace engine
