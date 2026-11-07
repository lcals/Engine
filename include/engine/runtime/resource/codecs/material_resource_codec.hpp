#pragma once

#include "runtime/resource/resource_codec.hpp"
#include "runtime/resource/types/material_resource.hpp"
#include "runtime/schema/shader_generated.h"

namespace engine
{

class MaterialResourceCodec final : public ResourceCodec
{
public:
    using ResourceType = MaterialResource;
    [[nodiscard]] schema::AssetKind kind() const noexcept override;
    [[nodiscard]] bool decode(const schema::AssetEntryT& entry, ByteView payload,
                              std::unique_ptr<Resource>& output, std::string& error) const override;
    [[nodiscard]] bool validate_data(const Resource& resource, std::string& error) const override;
    [[nodiscard]] bool validate_dependencies(const Resource& resource, ResourceView dependencies,
                                             std::string& error) const override;

    [[nodiscard]] static bool validate(const schema::MaterialDataT& data,
                                       const schema::ShaderDataT* shader);

private:
    [[nodiscard]] static bool valid_shape(const schema::MaterialDataT& data);
};

} // namespace engine
