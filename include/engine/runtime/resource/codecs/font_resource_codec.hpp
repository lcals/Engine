#pragma once

#include "runtime/resource/resource_codec.hpp"

namespace engine
{

class FontResourceCodec final : public ResourceCodec
{
public:
    [[nodiscard]] bool decode(const ResourceCatalog& catalog, ByteView payload,
                              Resource& output) const override;
    [[nodiscard]] static bool validate(const schema::FontDataT& data) noexcept;
};

} // namespace engine
