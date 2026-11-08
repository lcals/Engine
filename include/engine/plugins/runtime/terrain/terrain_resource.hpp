#pragma once

#include "runtime/resource/resource.hpp"
#include "runtime/schema/terrain_generated.h"

namespace engine::resource_detail
{
template <> struct Payload<schema::AssetKind::terrain>
{
    using Type = schema::TerrainAssetDataT;
};
} // namespace engine::resource_detail
namespace engine::plugins::terrain
{
using TerrainResource = ResourceValue<schema::TerrainAssetDataT, schema::AssetKind::terrain>;
}
