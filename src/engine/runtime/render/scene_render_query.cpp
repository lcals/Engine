#include "runtime/render/scene_render_query.hpp"

#include "runtime/scene/camera.hpp"
#include "runtime/scene/mesh_renderer.hpp"
#include "runtime/scene/scene.hpp"
#include "runtime/scene/text_component.hpp"
#include "runtime/world/world_system.hpp"

#include <algorithm>

namespace engine::render_detail
{

bool drawable_scene(const WorldSystem& world, const Camera* camera, bool has_extensions)
{
    const auto* scene = world.scene();
    camera = camera != nullptr ? camera : scene != nullptr ? scene->active_camera() : nullptr;
    if (camera == nullptr || !camera->active() || !camera->visible_in_hierarchy())
    {
        return false;
    }
    return has_extensions ||
           std::any_of(world.scenes().begin(), world.scenes().end(),
                       [](const auto& scene)
                       {
                           return std::any_of(
                               scene->nodes().begin(), scene->nodes().end(),
                               [](const std::unique_ptr<Node>& node)
                               {
                                   const auto* mesh = node->get_component<MeshRenderer>();
                                   const auto* text = node->get_component<TextComponent>();
                                   return (mesh && mesh->active() &&
                                           mesh->visible_in_hierarchy()) ||
                                          (text && text->active() && text->visible_in_hierarchy());
                               });
                       });
}

} // namespace engine::render_detail
