#pragma once

#include "runtime/resource/resource_codec.hpp"

namespace engine
{
class AnimationRetargetResourceCodec final : public ResourceCodec
{
public:
    [[nodiscard]] bool decode(const ResourceCatalog& catalog, ByteView payload,
                              Resource& output) const override;
    [[nodiscard]] bool
    validate_dependencies(const ResourceCatalog& catalog, const Resource& resource,
                          std::span<const ResourcePtr> dependencies) const override;
    [[nodiscard]] static bool validate(const schema::AnimationRetargetProfileDataT& data);
};
} // namespace engine
