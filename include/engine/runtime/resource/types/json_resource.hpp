#pragma once

#include "runtime/resource/resource.hpp"

namespace engine
{
namespace resource_detail
{
template <> struct Payload<schema::AssetKind::json>
{
    using Type = ByteBuffer;
};
} // namespace resource_detail
using JsonResource = ResourceValue<ByteBuffer, schema::AssetKind::json>;
} // namespace engine
