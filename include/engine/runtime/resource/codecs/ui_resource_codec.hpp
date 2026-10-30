#pragma once
#include "runtime/resource/resource_codec.hpp"
namespace engine
{
class UiResourceCodec final : public ResourceCodec
{
public:
    [[nodiscard]] static bool valid_family(std::string_view family) noexcept;
    [[nodiscard]] bool decode(const ResourceCatalog& catalog, ByteView payload,
                              Resource& output) const override;
    [[nodiscard]] bool
    validate_dependencies(const ResourceCatalog& catalog, const Resource& resource,
                          std::span<const ResourcePtr> dependencies) const override;
};
} // namespace engine
