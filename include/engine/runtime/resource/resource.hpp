#pragma once

#include "runtime/schema/asset_catalog_generated.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

namespace engine
{
using ByteBuffer = std::vector<std::byte>;
using ByteView = std::span<const std::byte>;

class Resource;
class ResourceSystem;
class ResourceLoader;
class ResourceCodec;
using ResourceView = std::span<const Resource* const>;

namespace resource_detail
{
template <schema::AssetKind Kind> struct Payload;
template <typename T> inline constexpr char type_identity = 0;
template <typename T> const T* cast(const Resource* resource) noexcept;
} // namespace resource_detail

template <typename DataT, schema::AssetKind Kind> class ResourceValue;

class Resource
{
public:
    virtual ~Resource() noexcept;
    Resource(const Resource&) = delete;
    Resource& operator=(const Resource&) = delete;
    Resource(Resource&&) = delete;
    Resource& operator=(Resource&&) = delete;

    [[nodiscard]] const schema::AssetEntryT& info() const noexcept;
    [[nodiscard]] virtual schema::AssetKind kind() const noexcept = 0;
    [[nodiscard]] std::uint64_t generation() const noexcept;

protected:
    explicit Resource(schema::AssetEntryT info);

private:
    friend class ResourceSystem;
    template <typename DataT, schema::AssetKind Kind> friend class ResourceValue;
    template <typename T> friend const T* resource_detail::cast(const Resource*) noexcept;

    schema::AssetEntryT info_;
    std::uint64_t generation_ = 0;
    const void* type_identity_ = nullptr;
};

template <typename DataT, schema::AssetKind Kind> class ResourceValue final : public Resource
{
    static_assert(std::is_same_v<DataT, typename resource_detail::Payload<Kind>::Type>);

public:
    using Data = DataT;
    static constexpr schema::AssetKind asset_kind = Kind;

    ResourceValue(schema::AssetEntryT info, DataT data)
        : Resource(std::move(info)), data_(std::move(data))
    {
        type_identity_ = &resource_detail::type_identity<ResourceValue>;
    }

    [[nodiscard]] schema::AssetKind kind() const noexcept override
    {
        return Kind;
    }
    [[nodiscard]] const DataT& data() const noexcept
    {
        return data_;
    }

private:
    DataT data_;
};

namespace resource_detail
{
// Canonical type identity prevents a custom subclass from impersonating a payload layout.
template <typename T> const T* cast(const Resource* resource) noexcept
{
    return resource && resource->type_identity_ == &type_identity<T>
               ? static_cast<const T*>(resource)
               : nullptr;
}

template <typename T> const typename T::Data* data(const Resource* resource) noexcept
{
    const auto* value = cast<T>(resource);
    return value ? &value->data() : nullptr;
}
} // namespace resource_detail
} // namespace engine
