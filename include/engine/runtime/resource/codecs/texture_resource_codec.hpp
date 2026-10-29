#pragma once

#include "runtime/resource/resource_codec.hpp"

namespace engine
{

class TextureResourceCodec final : public ResourceCodec
{
public:
    [[nodiscard]] bool decode(const ResourceCatalog& catalog, ByteView payload,
                              Resource& output) const override;
    [[nodiscard]] static bool validate(const schema::TextureDataT& data);
};

} // namespace engine
