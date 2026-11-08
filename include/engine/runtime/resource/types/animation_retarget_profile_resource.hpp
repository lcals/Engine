#pragma once

#include "runtime/resource/resource.hpp"
#include "runtime/schema/animation_retarget_generated.h"

namespace engine
{
namespace resource_detail
{
template <> struct Payload<schema::AssetKind::animation_retarget_profile>
{
    using Type = schema::AnimationRetargetProfileDataT;
};
} // namespace resource_detail
using AnimationRetargetResource = ResourceValue<schema::AnimationRetargetProfileDataT,
                                                schema::AssetKind::animation_retarget_profile>;
} // namespace engine
