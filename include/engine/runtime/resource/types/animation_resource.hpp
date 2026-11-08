#pragma once

#include "runtime/resource/resource.hpp"
#include "runtime/schema/animation_generated.h"

namespace engine
{
namespace resource_detail
{
template <> struct Payload<schema::AssetKind::animation>
{
    using Type = schema::AnimationDataT;
};
} // namespace resource_detail
using AnimationResource = ResourceValue<schema::AnimationDataT, schema::AssetKind::animation>;
} // namespace engine
