#pragma once

#include "runtime/resource/resource.hpp"

namespace engine
{
namespace resource_detail
{
template <> struct Payload<schema::AssetKind::script>
{
    using Type = ByteBuffer;
};
} // namespace resource_detail
using ScriptResource = ResourceValue<ByteBuffer, schema::AssetKind::script>;
} // namespace engine
