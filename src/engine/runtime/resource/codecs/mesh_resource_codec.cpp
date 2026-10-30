#include "runtime/resource/codecs/mesh_resource_codec.hpp"

#include "runtime/core/guid_map.hpp"
#include "runtime/resource/codecs/model_value_utils.hpp"
#include "runtime/resource/resource_catalog.hpp"
#include "runtime/resource/resource_system.hpp"
#include "runtime/utils/mesh_utils.hpp"

#include <flatbuffers/verifier.h>

#include <utility>

namespace engine
{

bool MeshResourceCodec::validate(const schema::MeshDataT& data)
{
    if (!valid_path(data.path) || !MeshUtils::validate(data))
    {
        return false;
    }
    if (data.skeleton_guid.empty())
    {
        return data.skin.empty() && data.mesh_to_skeleton.empty();
    }
    if (!GuidMap::valid_guid(data.skeleton_guid) || data.skin.size() != data.vertices.size() ||
        !model_value_utils::affine(data.mesh_to_skeleton))
    {
        return false;
    }
    for (const auto& skin : data.skin)
    {
        const float weights[] = {skin.w0(), skin.w1(), skin.w2(), skin.w3()};
        double sum = 0;
        for (float weight : weights)
        {
            if (!std::isfinite(weight) || weight < 0 || weight > 1)
            {
                return false;
            }
            sum += weight;
        }
        if (std::abs(sum - 1) > 0.0001 || skin.j0() >= 1024 || skin.j1() >= 1024 ||
            skin.j2() >= 1024 || skin.j3() >= 1024)
        {
            return false;
        }
    }
    return true;
}

bool MeshResourceCodec::decode(const ResourceCatalog& catalog, ByteView payload,
                               Resource& output) const
{
    const auto& entry = output.info();
    std::unique_ptr<schema::MeshDataT> data;
    if (entry.path.starts_with("builtin/"))
    {
        const auto* builtin = ResourceSystem::find_mesh(entry.path);
        if (!builtin)
        {
            return false;
        }
        data = std::make_unique<schema::MeshDataT>(*builtin);
    }
    else
    {
        const auto* bytes = reinterpret_cast<const std::uint8_t*>(payload.data());
        flatbuffers::Verifier verifier(bytes, payload.size());
        if (!schema::VerifyMeshDataBuffer(verifier))
        {
            return false;
        }
        data = schema::UnPackMeshData(bytes);
    }
    if (data->path != entry.path || !validate(*data) ||
        entry.dependencies.size() != (data->skeleton_guid.empty() ? 0U : 1U) ||
        (!data->skeleton_guid.empty() &&
         !declares(catalog, output, data->skeleton_guid, schema::AssetKind::skeleton)))
    {
        return false;
    }
    store(output, std::move(data));
    return true;
}

bool MeshResourceCodec::validate_dependencies(const ResourceCatalog&, const Resource& resource,
                                              std::span<const ResourcePtr> dependencies) const
{
    const auto* data = resource.mesh();
    if (!data)
    {
        return false;
    }
    if (data->skeleton_guid.empty())
    {
        return true;
    }
    const auto* skeleton = dependency(dependencies, data->skeleton_guid);
    if (!skeleton || !skeleton->skeleton())
    {
        return false;
    }
    const auto count = skeleton->skeleton()->joints.size();
    for (const auto& skin : data->skin)
    {
        if (skin.j0() >= count || skin.j1() >= count || skin.j2() >= count || skin.j3() >= count)
        {
            return false;
        }
    }
    return true;
}

} // namespace engine
