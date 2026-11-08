#pragma once

#include "runtime/resource/resource_codec.hpp"
#include "runtime/resource/types/model_resource.hpp"

namespace engine
{
class ModelResourceCodec final : public ResourceCodec
{
public:
    using ResourceType = ModelResource;
    [[nodiscard]] schema::AssetKind kind() const noexcept override;
    [[nodiscard]] bool decode(const schema::AssetEntryT& entry, ByteView payload,
                              std::unique_ptr<Resource>& output, std::string& error) const override;
    [[nodiscard]] bool validate_data(const Resource& resource, std::string& error) const override;
    [[nodiscard]] bool validate_dependencies(const Resource& resource, ResourceView dependencies,
                                             std::string& error) const override;

    [[nodiscard]] static bool validate(const schema::ModelDataT& data);
};
} // namespace engine
