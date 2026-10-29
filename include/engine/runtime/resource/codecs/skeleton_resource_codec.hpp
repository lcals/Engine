#pragma once

#include "runtime/resource/resource_codec.hpp"

namespace engine
{
class SkeletonResourceCodec final : public ResourceCodec
{
public:
    [[nodiscard]] bool decode(const ResourceCatalog& catalog, ByteView payload,
                              Resource& output) const override;
    [[nodiscard]] static bool validate(const schema::SkeletonDataT& data);
};
} // namespace engine
