#pragma once

#include "runtime/resource/resource_codec.hpp"
#include "runtime/resource/types/mesh_resource.hpp"

namespace engine
{

class MeshResourceCodec final : public ResourceCodec
{
public:
    using ResourceType = MeshResource;
    [[nodiscard]] schema::AssetKind kind() const noexcept override;
    [[nodiscard]] bool decode(const schema::AssetEntryT& entry, ByteView payload,
                              std::unique_ptr<Resource>& output, std::string& error) const override;
    [[nodiscard]] bool validate_data(const Resource& resource, std::string& error) const override;
    [[nodiscard]] bool validate_dependencies(const Resource& resource, ResourceView dependencies,
                                             std::string& error) const override;

    [[nodiscard]] static bool validate(const schema::MeshDataT& data);
};

} // namespace engine
