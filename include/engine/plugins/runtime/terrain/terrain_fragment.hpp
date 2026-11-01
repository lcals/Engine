#pragma once

#include "runtime/schema/mesh_generated.h"
#include "runtime/schema/scene_generated.h"

namespace engine::plugins::terrain
{

// Candidate-only geometry; not a save format. The runtime owns the resulting scene nodes.
struct TerrainFragment
{
    schema::MeshDataT mesh;
    schema::ColliderDataT collision;
    double volume = 0;
};

} // namespace engine::plugins::terrain
