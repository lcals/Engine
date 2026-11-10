#pragma once
#include "runtime/scene/node.hpp"
#include <cstdint>

namespace engine
{
class WorldSystem;
class Camera;
class Scene;
} // namespace engine

namespace engine::render_detail
{

[[nodiscard]] bool drawable_scene(const WorldSystem& world, Camera camera, bool has_extensions,
                                  const Scene* candidate = nullptr);

[[nodiscard]] Node scope_node(const Scene& scene, std::uint64_t id);
[[nodiscard]] std::uint64_t target_scope(const Scene& scene);

} // namespace engine::render_detail
