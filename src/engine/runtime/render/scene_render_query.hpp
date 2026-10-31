#pragma once

namespace engine
{
class Scene;
class Camera;
} // namespace engine

namespace engine::render_detail
{

[[nodiscard]] bool drawable_scene(const Scene& scene, const Camera* camera, bool has_extensions);

} // namespace engine::render_detail
