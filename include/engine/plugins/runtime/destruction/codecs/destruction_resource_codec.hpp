#pragma once
#include "plugins/runtime/destruction/destruction_resource.hpp"
#include "runtime/resource/resource_codec.hpp"
namespace engine::plugins::destruction
{
class DestructionResourceCodec final : public ResourceCodec
{
public:
    using ResourceType = DestructionResource;
    [[nodiscard]] schema::AssetKind kind() const noexcept override;
    [[nodiscard]] bool decode(const schema::AssetEntryT& entry, ByteView payload,
                              std::unique_ptr<Resource>& output, std::string& error) const override;
    [[nodiscard]] bool validate_data(const Resource& resource, std::string& error) const override;
    [[nodiscard]] bool validate_dependencies(const Resource& resource, ResourceView dependencies,
                                             std::string& error) const override;
};
} // namespace engine::plugins::destruction
