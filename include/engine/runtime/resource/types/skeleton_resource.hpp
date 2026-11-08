#pragma once

#include "runtime/resource/resource.hpp"
#include "runtime/schema/skeleton_generated.h"

namespace engine
{
namespace resource_detail
{
template <> struct Payload<schema::AssetKind::skeleton>
{
    using Type = schema::SkeletonDataT;
};
} // namespace resource_detail
using SkeletonResource = ResourceValue<schema::SkeletonDataT, schema::AssetKind::skeleton>;
} // namespace engine
