#include "runtime/resource/codecs/mesh_resource_codec.hpp"

#include "runtime/core/guid_map.hpp"
#include "runtime/resource/codecs/model_value_utils.hpp"
#include "runtime/resource/types/skeleton_resource.hpp"
#include "runtime/utils/mesh_utils.hpp"
#include <set>

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

schema::AssetKind MeshResourceCodec::kind() const noexcept
{
    return schema::AssetKind::mesh;
}

bool MeshResourceCodec::decode(const schema::AssetEntryT& entry, ByteView payload,
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
    if (!schema::VerifyMeshDataBuffer(verifier))
    {
        error = "Invalid Mesh FlatBuffer";
        return false;
    }
    auto data = schema::UnPackMeshData(bytes);
    output = std::make_unique<ResourceType>(entry, std::move(*data));
    return true;
}

bool MeshResourceCodec::validate_data(const Resource& resource, std::string& error) const
{
    const auto* data = resource_detail::data<ResourceType>(&resource);
    if (!data || !(data->path == resource.info().path && validate(*data) &&
                   resource.info().dependencies.size() == (data->skeleton_guid.empty() ? 0U : 1U) &&
                   (data->skeleton_guid.empty() || declares(resource, data->skeleton_guid))))
    {
        error = "Invalid Mesh data or declared dependencies";
        return false;
    }
    error.clear();
    return true;
}

bool MeshResourceCodec::validate_dependencies(const Resource& resource, ResourceView dependencies,
                                              std::string& error) const
{
    const auto* data = resource_detail::data<MeshResource>(&resource);
    if (!data)
    {
        return false;
    }
    if (data->skeleton_guid.empty())
    {
        return true;
    }
    const auto* skeleton = dependency(dependencies, data->skeleton_guid);
    if (!skeleton || !resource_detail::data<SkeletonResource>(skeleton))
    {
        return false;
    }
    const auto count = resource_detail::data<SkeletonResource>(skeleton)->joints.size();
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
