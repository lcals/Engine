#pragma once

#include "runtime/resource/resource.hpp"
#include "runtime/schema/texture_generated.h"

namespace engine
{
namespace resource_detail
{
template <> struct Payload<schema::AssetKind::texture>
{
    using Type = schema::TextureDataT;
};
} // namespace resource_detail
using TextureResource = ResourceValue<schema::TextureDataT, schema::AssetKind::texture>;
} // namespace engine
