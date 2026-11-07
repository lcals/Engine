#pragma once

#include "runtime/resource/resource.hpp"

#include <string>
#include <string_view>

namespace engine
{
// Stateless single-item conversion and validation. Never reads files or retains dependencies.
class ResourceCodec
{
public:
    virtual ~ResourceCodec() noexcept = default;
    [[nodiscard]] virtual schema::AssetKind kind() const noexcept = 0;
    [[nodiscard]] virtual bool decode(const schema::AssetEntryT& entry, ByteView payload,
                                      std::unique_ptr<Resource>& output,
                                      std::string& error) const = 0;
    [[nodiscard]] virtual bool validate_data(const Resource& resource,
                                             std::string& error) const = 0;
    [[nodiscard]] virtual bool validate_dependencies(const Resource& resource,
                                                     ResourceView dependencies,
                                                     std::string& error) const = 0;

protected:
    [[nodiscard]] static bool valid_path(std::string_view path) noexcept;
    [[nodiscard]] static bool declares(const Resource& resource, std::string_view guid) noexcept;
    [[nodiscard]] static const Resource* dependency(ResourceView resources,
                                                    std::string_view path_or_guid) noexcept;
};
} // namespace engine
