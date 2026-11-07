#pragma once
#include "plugins/runtime/effects/effect_resource.hpp"
#include "runtime/resource/resource_codec.hpp"
namespace engine::plugins::effects
{
class EffectResourceCodec final : public ResourceCodec
{
public:
    using ResourceType = EffectResource;
    [[nodiscard]] schema::AssetKind kind() const noexcept override;
    [[nodiscard]] bool decode(const schema::AssetEntryT& entry, ByteView payload,
                              std::unique_ptr<Resource>& output, std::string& error) const override;
    [[nodiscard]] bool validate_data(const Resource& resource, std::string& error) const override;
    [[nodiscard]] bool validate_dependencies(const Resource& resource, ResourceView dependencies,
                                             std::string& error) const override;
};
} // namespace engine::plugins::effects
