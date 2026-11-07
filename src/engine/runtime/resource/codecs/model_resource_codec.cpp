#include "runtime/resource/codecs/model_resource_codec.hpp"

#include "runtime/core/guid_map.hpp"
#include "runtime/resource/codecs/model_value_utils.hpp"
#include "runtime/resource/types/material_resource.hpp"
#include "runtime/resource/types/mesh_resource.hpp"
#include <set>

#include <flatbuffers/verifier.h>
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
schema::AssetKind ModelResourceCodec::kind() const noexcept
{
    return schema::AssetKind::model;
}

bool ModelResourceCodec::decode(const schema::AssetEntryT& entry, ByteView payload,
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
    if (!schema::VerifyModelDataBuffer(verifier))
    {
        error = "Invalid Model FlatBuffer";
        return false;
    }
    auto data = schema::UnPackModelData(bytes);
    output = std::make_unique<ResourceType>(entry, std::move(*data));
    return true;
}

bool ModelResourceCodec::validate_data(const Resource& resource, std::string& error) const
{
    const auto* data = resource_detail::data<ResourceType>(&resource);
    if (!data)
    {
        error = "Invalid canonical type";
        return false;
    }
    if (data->path != resource.info().path || !validate(*data))
    {
        return false;
    }
    std::set<Guid> references;
    for (const auto& node : data->nodes)
    {
        if (!node->mesh_guid.empty() && !declares(resource, node->mesh_guid))
        {
            return false;
        }
        if (!node->mesh_guid.empty())
        {
            references.insert(node->mesh_guid);
        }
        for (const auto& guid : node->material_guids)
        {
            if (!declares(resource, guid))
            {
                return false;
            }
            references.insert(guid);
        }
    }
    if (resource.info().dependencies.size() != references.size())
    {
        return false;
    }
    for (const auto& guid : resource.info().dependencies)
    {
        if (!references.contains(guid))
        {
            return false;
        }
    }
    error.clear();
    return true;
}

bool ModelResourceCodec::validate_dependencies(const Resource& resource, ResourceView dependencies,
                                               std::string& error) const
{
    const auto* data = resource_detail::data<ModelResource>(&resource);
    if (!data)
    {
        return false;
    }
    for (const auto& node : data->nodes)
    {
        if (!node->mesh_guid.empty())
        {
            const auto* item = dependency(dependencies, node->mesh_guid);
            if (!item || !resource_detail::data<MeshResource>(item))
            {
                return false;
            }
            for (const auto& part : resource_detail::data<MeshResource>(item)->submeshes)
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
            if (!item || !resource_detail::data<MaterialResource>(item))
            {
                return false;
            }
        }
    }
    return true;
}
} // namespace engine
