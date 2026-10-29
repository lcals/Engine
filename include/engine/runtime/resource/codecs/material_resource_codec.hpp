#pragma once

#include "runtime/resource/resource_codec.hpp"

namespace engine
{

class MaterialResourceCodec final : public ResourceCodec
{
public:
    [[nodiscard]] bool decode(const ResourceCatalog& catalog, ByteView payload,
                              Resource& output) const override;
    [[nodiscard]] bool
    validate_dependencies(const ResourceCatalog& catalog, const Resource& resource,
                          std::span<const ResourcePtr> dependencies) const override;
    [[nodiscard]] static bool validate(const schema::MaterialDataT& data,
                                       const schema::ShaderDataT* shader);

private:
    [[nodiscard]] static bool valid_shape(const schema::MaterialDataT& data);
};

} // namespace engine
