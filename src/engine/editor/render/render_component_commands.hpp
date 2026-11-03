#pragma once

#include <nlohmann/json_fwd.hpp>

namespace engine
{
class WorldSystem;
class PlanarReflectionComponent;
class LightProbeVolumeComponent;
class MeshRenderer;
}

namespace engine::editor
{
class EditorCommands;
class SceneCommands;
[[nodiscard]] bool register_render_component_commands(WorldSystem& world,
                                                      SceneCommands& scene_commands,
                                                      EditorCommands& commands);
[[nodiscard]] nlohmann::json planar_reflection_json(const PlanarReflectionComponent& component);
[[nodiscard]] nlohmann::json light_probe_volume_json(const LightProbeVolumeComponent& component);
[[nodiscard]] nlohmann::json mesh_lighting_json(const MeshRenderer& component);
}
