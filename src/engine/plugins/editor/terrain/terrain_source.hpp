#pragma once

#include "runtime/schema/terrain_generated.h"

#include <string>
#include <string_view>

namespace engine::plugins::terrain
{

[[nodiscard]] bool read_terrain_source(std::string_view text, schema::TerrainGeneratorDataT& output,
                                       std::string& error);

} // namespace engine::plugins::terrain
