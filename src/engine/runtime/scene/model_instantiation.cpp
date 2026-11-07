#include "runtime/scene/scene.hpp"

#include "runtime/resource/types/model_resource.hpp"
#include "runtime/scene/mesh_renderer.hpp"
#include "runtime/scene/resource_binding.hpp"
#include "runtime/scene/transform.hpp"

#include <algorithm>
#include <vector>

namespace engine
{
Node Scene::instantiate_model(std::string_view path, std::string_view name,
                              std::vector<Node>* created)
{
    if (created)
    {
        created->clear();
    }
    const auto first_order = order_sequence_ + 1;
    auto* resources = world_.resource_loader();
    resource_detail::ResourceReference resource;
    const auto owner = ecs_detail::ResourceBindingScope::owner(world_);
    if (!resources ||
        !(owner.id ? ecs_detail::acquire_dependency(resources, owner, path, resource)
                   : resource.acquire(resources, path)) ||
        !resource.data<ModelResource>())
    {
        return {};
    }
    ecs_detail::ResourceBindingScope binding(world_, resource.handle());
    auto root = create_node(name);
    if (!root)
    {
        return {};
    }
    const auto rollback = [this, first_order]() -> Node
    {
        rollback_created(first_order);
        return {};
    };
    std::vector<Node> nodes;
    for (const auto& source : resource.data<ModelResource>()->nodes)
    {
        auto node = create_node(source->name);
        if (!node)
        {
            return rollback();
        }
        // 先挂到候选根，保证后续任一步失败都能整体回滚。
        if (!node.set_parent(root))
        {
            const bool removed = destroy_node(node.id());
            (void)removed;
            return rollback();
        }
        if (source->parent >= 0 &&
            !node.set_parent(nodes[static_cast<std::size_t>(source->parent)]))
        {
            return rollback();
        }
        TransformSettings transform;
        transform.position = source->position;
        transform.rotation = source->rotation;
        transform.scale = source->scale;
        if (!node.get_component<Transform>().set_settings(transform))
        {
            return rollback();
        }
        if (!source->mesh_guid.empty())
        {
            std::string mesh_path;
            auto renderer = node.add_component<MeshRenderer>();
            if (!renderer || !ecs_detail::resource_path(node, source->mesh_guid, mesh_path) ||
                !renderer.set_mesh(mesh_path))
            {
                return rollback();
            }
            std::size_t slots = std::max<std::size_t>(1, source->material_guids.size());
            for (const auto& part : renderer.mesh_data()->submeshes)
            {
                if (!source->material_guids.empty() &&
                    part.material_slot() >= source->material_guids.size())
                {
                    return rollback();
                }
                slots = std::max<std::size_t>(slots,
                                              static_cast<std::size_t>(part.material_slot()) + 1);
            }
            if (!renderer.set_material_count(slots))
            {
                return rollback();
            }
            for (std::size_t slot = 0; slot < source->material_guids.size(); ++slot)
            {
                std::string material_path;
                if (!ecs_detail::resource_path(node, source->material_guids[slot], material_path) ||
                    !renderer.material(slot).set_type(material_path))
                {
                    return rollback();
                }
            }
        }
        nodes.push_back(node);
    }
    if (created && !collect_created(first_order, *created))
    {
        return rollback();
    }
    return root;
}
} // namespace engine
