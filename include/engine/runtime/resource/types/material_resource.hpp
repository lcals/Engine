#pragma once

#include "runtime/resource/resource.hpp"
#include "runtime/schema/material_generated.h"

namespace engine
{
namespace resource_detail
{
template <> struct Payload<schema::AssetKind::material>
{
    using Type = schema::MaterialDataT;
};
} // namespace resource_detail
using MaterialResource = ResourceValue<schema::MaterialDataT, schema::AssetKind::material>;
} // namespace engine
