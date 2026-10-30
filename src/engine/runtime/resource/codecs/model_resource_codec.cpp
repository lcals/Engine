#include "runtime/resource/codecs/model_resource_codec.hpp"

#include "runtime/core/guid_map.hpp"
#include "runtime/resource/codecs/model_value_utils.hpp"

#include <flatbuffers/verifier.h>
#include <set>
#include <string>
#include <utility>

namespace engine
{
bool ModelResourceCodec::validate(const schema::ModelDataT& data)
{
    if (data.schema_version != 2 || !valid_path(data.path) || data.nodes.empty() ||
        data.nodes.size() > 65536)
    {
        return false;
    }
    for (std::size_t index = 0; index < data.nodes.size(); ++index)
    {
        const auto& node = data.nodes[index];
        if (!node || node->name.empty() || node->parent < -1 ||
            node->parent >= static_cast<int>(index) || !model_value_utils::vector(node->position) ||
            !model_value_utils::vector(node->rotation) || !model_value_utils::scale(node->scale) ||
            (!node->mesh_guid.empty() && !GuidMap::valid_guid(node->mesh_guid)) ||
            node->material_guids.size() > 16 ||
            (node->mesh_guid.empty() && !node->material_guids.empty()))
        {
            return false;
        }
        for (const auto& guid : node->material_guids)
        {
            if (!GuidMap::valid_guid(guid))
            {
                return false;
            }
        }
    }
    return true;
}
bool ModelResourceCodec::decode(const ResourceCatalog& catalog, ByteView payload,
                                Resource& output) const
{
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(payload.data());
    flatbuffers::Verifier verifier(bytes, payload.size());
    if (!schema::VerifyModelDataBuffer(verifier))
    {
        return false;
    }
    auto data = schema::UnPackModelData(bytes);
    if (data->path != output.info().path || !validate(*data))
    {
        return false;
    }
    std::set<Guid> references;
    for (const auto& node : data->nodes)
    {
        if (!node->mesh_guid.empty() &&
            !declares(catalog, output, node->mesh_guid, schema::AssetKind::mesh))
        {
            return false;
        }
        if (!node->mesh_guid.empty())
        {
            references.insert(node->mesh_guid);
        }
        for (const auto& guid : node->material_guids)
        {
            if (!declares(catalog, output, guid, schema::AssetKind::material))
            {
                return false;
            }
            references.insert(guid);
        }
    }
    if (output.info().dependencies.size() != references.size())
    {
        return false;
    }
    for (const auto& guid : output.info().dependencies)
    {
        if (!references.contains(guid))
        {
            return false;
        }
    }
    store(output, std::move(data));
    return true;
}
bool ModelResourceCodec::validate_dependencies(const ResourceCatalog&, const Resource& resource,
                                               std::span<const ResourcePtr> dependencies) const
{
    const auto* data = resource.model();
    if (!data)
    {
        return false;
    }
    for (const auto& node : data->nodes)
    {
        if (!node->mesh_guid.empty())
        {
            const auto* item = dependency(dependencies, node->mesh_guid);
            if (!item || !item->mesh())
            {
                return false;
            }
            for (const auto& part : item->mesh()->submeshes)
            {
                if (!node->material_guids.empty() &&
                    part.material_slot() >= node->material_guids.size())
                {
                    return false;
                }
            }
        }
        for (const auto& guid : node->material_guids)
        {
            const auto* item = dependency(dependencies, guid);
            if (!item || !item->material())
            {
                return false;
            }
        }
    }
    return true;
}
} // namespace engine
