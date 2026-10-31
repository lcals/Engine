#include "runtime/render/scene_render_query.hpp"

#include "runtime/scene/camera.hpp"
#include "runtime/scene/mesh_renderer.hpp"
#include "runtime/scene/scene.hpp"
#include "runtime/scene/text_component.hpp"

#include <algorithm>

namespace engine::render_detail
{

bool drawable_scene(const Scene& scene, const Camera* camera)
{
    camera = camera != nullptr ? camera : scene.active_camera();
    if (camera == nullptr || !camera->enabled())
    {
        return false;
    }
    return std::any_of(scene.nodes().begin(), scene.nodes().end(),
                       [](const std::unique_ptr<Node>& node)
                       {
                           const MeshRenderer* mesh = node->get_component<MeshRenderer>();
                           const auto* text = node->get_component<TextComponent>();
                           return (mesh && mesh->enabled()) || (text && text->enabled());
                       });
}

} // namespace engine::render_detail
