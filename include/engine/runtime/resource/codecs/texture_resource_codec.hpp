#pragma once

#include "runtime/resource/resource_codec.hpp"

namespace engine
{

class TextureResourceCodec final : public ResourceCodec
{
public:
    // 各模块共用一次性初始化；允许从多个线程调用。
    static void initialize_transcoder();
    [[nodiscard]] bool decode(const ResourceCatalog& catalog, ByteView payload,
                              Resource& output) const override;
    [[nodiscard]] static bool validate(const schema::TextureDataT& data);
};

} // namespace engine
