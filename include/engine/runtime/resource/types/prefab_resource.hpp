#pragma once

#include "runtime/resource/resource.hpp"
#include "runtime/schema/prefab_generated.h"

namespace engine
{
namespace resource_detail
{
template <> struct Payload<schema::AssetKind::prefab>
{
    using Type = schema::PrefabDataT;
};
} // namespace resource_detail
using PrefabResource = ResourceValue<schema::PrefabDataT, schema::AssetKind::prefab>;
} // namespace engine
