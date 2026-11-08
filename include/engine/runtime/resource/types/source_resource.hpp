#pragma once

#include "runtime/resource/resource.hpp"

namespace engine
{
namespace resource_detail
{
template <> struct Payload<schema::AssetKind::source>
{
    using Type = ByteBuffer;
};
} // namespace resource_detail
using SourceResource = ResourceValue<ByteBuffer, schema::AssetKind::source>;
} // namespace engine
