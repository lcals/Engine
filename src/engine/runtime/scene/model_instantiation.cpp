#include "runtime/scene/scene.hpp"

#include "runtime/resource/resource_system.hpp"
#include "runtime/scene/mesh_renderer.hpp"
#include "runtime/scene/transform.hpp"

#include <algorithm>
#include <vector>

namespace engine
{
Node* Scene::instantiate_model(std::string_view path, std::string_view name)
{
    auto* resources = ResourceSystem::instance();
    Guid guid;
    ResourcePtr resource;
    if (!resources || !ResourceSystem::path_to_guid(path, guid) ||
        !resources->loader().load(guid, resource) || !resource->model())
    {
        return nullptr;
    }
    auto* root = create_node(name);
    if (!root)
    {
        return nullptr;
    }
    const auto rollback = [this, root]() -> Node*
    {
        const bool removed = destroy_node(root->id());
        (void)removed;
        return nullptr;
    };
    std::vector<Node*> nodes;
    for (const auto& source : resource->model()->nodes)
    {
        auto* node = create_node(source->name);
        if (!node)
        {
            return rollback();
        }
        // 先挂到候选根，保证后续任一步失败都能整体回滚。
        if (!node->set_parent(root))
        {
            const bool removed = destroy_node(node->id());
            (void)removed;
            return rollback();
        }
        if (source->parent >= 0 &&
            !node->set_parent(nodes[static_cast<std::size_t>(source->parent)]))
        {
            return rollback();
        }
        TransformSettings transform;
        transform.position = source->position;
        transform.rotation = source->rotation;
        transform.scale = source->scale;
        if (!node->get_component<Transform>()->set_settings(transform))
        {
            return rollback();
        }
        if (!source->mesh_guid.empty())
        {
            std::string mesh_path;
            auto* renderer = node->add_component<MeshRenderer>();
            if (!renderer || !ResourceSystem::guid_to_path(source->mesh_guid, mesh_path) ||
                !renderer->set_mesh(mesh_path))
            {
                return rollback();
            }
            std::size_t slots = std::max<std::size_t>(1, source->material_guids.size());
            for (const auto& part : renderer->mesh_data()->submeshes)
            {
                if (!source->material_guids.empty() &&
                    part.material_slot() >= source->material_guids.size())
                {
                    return rollback();
                }
                slots = std::max<std::size_t>(slots,
                                              static_cast<std::size_t>(part.material_slot()) + 1);
            }
            if (!renderer->set_material_count(slots))
            {
                return rollback();
            }
            for (std::size_t slot = 0; slot < source->material_guids.size(); ++slot)
            {
                std::string material_path;
                if (!ResourceSystem::guid_to_path(source->material_guids[slot], material_path) ||
                    !renderer->material(slot)->set_type(material_path))
                {
                    return rollback();
                }
            }
        }
        nodes.push_back(node);
    }
    return root;
}
} // namespace engine
