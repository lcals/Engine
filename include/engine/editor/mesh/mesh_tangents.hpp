#pragma once

#include "runtime/schema/mesh_generated.h"

#include <string>

namespace engine::editor
{
// Editor-only offline generation; callers supply validated mesh data.
[[nodiscard]] bool generate_mesh_tangents(schema::MeshDataT& mesh, std::string& error);
} // namespace engine::editor
