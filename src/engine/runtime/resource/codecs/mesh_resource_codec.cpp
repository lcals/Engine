#include "runtime/resource/codecs/mesh_resource_codec.hpp"

#include "runtime/resource/resource_catalog.hpp"
#include "runtime/resource/resource_system.hpp"
#include "runtime/utils/mesh_utils.hpp"

#include <flatbuffers/verifier.h>

#include <utility>

namespace engine
{

bool MeshResourceCodec::validate(const schema::MeshDataT& data)
{
    return valid_path(data.path) && MeshUtils::validate(data);
}

bool MeshResourceCodec::decode(const ResourceCatalog&, ByteView payload, Resource& output) const
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
    if (data->path != entry.path || !validate(*data))
    {
        return false;
    }
    store(output, std::move(data));
    return true;
}

} // namespace engine
