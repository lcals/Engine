#pragma once

#include "runtime/resource/resource.hpp"
#include "runtime/schema/scene_generated.h"

namespace engine
{
namespace resource_detail
{
template <> struct Payload<schema::AssetKind::scene>
{
    using Type = schema::SceneDataT;
};
} // namespace resource_detail
using SceneResource = ResourceValue<schema::SceneDataT, schema::AssetKind::scene>;
} // namespace engine
