#pragma once

#include "editor/asset_pipeline/import_context.hpp"
#include "runtime/schema/scene_generated.h"

namespace engine::editor::asset_pipeline
{
[[nodiscard]] bool
scan_node_dependencies(const std::vector<std::unique_ptr<schema::NodeDataT>>& nodes,
                       const ImportContext& context, std::vector<Guid>& dependencies,
                       std::string& error);
[[nodiscard]] bool scan_override_dependencies(
    const std::vector<std::unique_ptr<schema::PrefabNodeOverrideT>>& overrides,
    const ImportContext& context, std::vector<Guid>& dependencies, std::string& error);
[[nodiscard]] bool compose_scene(schema::SceneDataT& scene, const ImportContext& context,
                                 std::string& error);
} // namespace engine::editor::asset_pipeline
