#pragma once

namespace engine
{
class WorldSystem;
class Camera;
} // namespace engine

namespace engine::render_detail
{

[[nodiscard]] bool drawable_scene(const WorldSystem& world, const Camera* camera,
                                  bool has_extensions);

} // namespace engine::render_detail
