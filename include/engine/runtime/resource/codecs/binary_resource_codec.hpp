#pragma once

#include "runtime/resource/resource_codec.hpp"

namespace engine
{

class BinaryResourceCodec final : public ResourceCodec
{
public:
    [[nodiscard]] bool decode(const ResourceCatalog& catalog, ByteView payload,
                              Resource& output) const override;
};

} // namespace engine
