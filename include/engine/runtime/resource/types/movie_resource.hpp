#pragma once

#include "runtime/resource/resource.hpp"
#include "runtime/schema/movie_generated.h"

namespace engine
{
namespace resource_detail
{
template <> struct Payload<schema::AssetKind::movie>
{
    using Type = schema::MovieDataT;
};
} // namespace resource_detail
using MovieResource = ResourceValue<schema::MovieDataT, schema::AssetKind::movie>;
} // namespace engine
