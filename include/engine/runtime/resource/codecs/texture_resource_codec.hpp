#pragma once

#include "runtime/resource/resource_codec.hpp"
#include "runtime/resource/types/texture_resource.hpp"

namespace engine
{

class TextureResourceCodec final : public ResourceCodec
{
public:
    using ResourceType = TextureResource;
    [[nodiscard]] schema::AssetKind kind() const noexcept override;
    [[nodiscard]] bool decode(const schema::AssetEntryT& entry, ByteView payload,
                              std::unique_ptr<Resource>& output, std::string& error) const override;
    [[nodiscard]] bool validate_data(const Resource& resource, std::string& error) const override;
    [[nodiscard]] bool validate_dependencies(const Resource& resource, ResourceView dependencies,
                                             std::string& error) const override;

    // 各模块共用一次性初始化；允许从多个线程调用。
    static void initialize_transcoder();

    [[nodiscard]] static bool validate(const schema::TextureDataT& data);
};

} // namespace engine
