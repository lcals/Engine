#include "runtime/scene/scene.hpp"

#include "runtime/scene/camera.hpp"
#include "runtime/scene/component_registry.hpp"
#include "runtime/scene/transform.hpp"

#include <flecs.h>

#include <algorithm>
#include <limits>
#include <new>
#include <set>
#include <utility>

namespace engine
{

Scene::Scene(ecs_world_t& world, std::uint64_t id, std::string_view name) : world_(world), id_(id)
{
    info_.name = name;
}

Scene::~Scene() noexcept
{
    info_.active_camera_node = 0;
    nodes_.clear();
}

std::uint64_t Scene::id() const noexcept
{
    return id_;
}

std::string_view Scene::name() const noexcept
{
    return info_.name;
}

Node* Scene::create_node(std::string_view name)
{
    if (node_sequence_ == std::numeric_limits<std::uint64_t>::max())
    {
        return nullptr;
    }
    const ecs_entity_t entity = ecs_new(&world_);
    if (entity == 0)
    {
        return nullptr;
    }
    std::unique_ptr<Node> node(new (std::nothrow) Node(*this, entity, name));
    if (node == nullptr)
    {
        ecs_delete(&world_, entity);
        return nullptr;
    }
    if (node->add_component<Transform>() == nullptr)
    {
        return nullptr;
    }
    Node* result = node.get();
    nodes_.push_back(std::move(node));
    return result;
}

Node* Scene::find_node(std::uint64_t id) noexcept
{
    return const_cast<Node*>(std::as_const(*this).find_node(id));
}

const Node* Scene::find_node(std::uint64_t id) const noexcept
{
    const auto iterator = std::find_if(nodes_.begin(), nodes_.end(),
                                       [id](const auto& node) { return node->id() == id; });
    return iterator == nodes_.end() ? nullptr : iterator->get();
}

bool Scene::destroy_node(std::uint64_t id)
{
    Node* root = find_node(id);
    if (root == nullptr)
    {
        return false;
    }
    // 先收集整个子树，再销毁对象，避免遍历已经失效的父指针。
    std::vector<const Node*> removed;
    for (const auto& node : nodes_)
    {
        for (const Node* ancestor = node.get(); ancestor != nullptr; ancestor = ancestor->parent())
        {
            if (ancestor == root)
            {
                removed.push_back(node.get());
                break;
            }
        }
    }
    const auto should_remove = [&removed](const Node* node)
    { return std::find(removed.begin(), removed.end(), node) != removed.end(); };
    if (should_remove(find_local_node(info_.active_camera_node)))
    {
        info_.active_camera_node = 0;
    }
    std::erase_if(nodes_, [&should_remove](const auto& node) { return should_remove(node.get()); });
    return true;
}

const std::vector<std::unique_ptr<Node>>& Scene::nodes() const noexcept
{
    return nodes_;
}

Camera* Scene::active_camera() noexcept
{
    Node* node = find_local_node(info_.active_camera_node);
    return node == nullptr ? nullptr : node->get_component<Camera>();
}

const Camera* Scene::active_camera() const noexcept
{
    const Node* node = find_local_node(info_.active_camera_node);
    return node == nullptr ? nullptr : node->get_component<Camera>();
}

bool Scene::set_active_camera(Camera* camera) noexcept
{
    if (camera != nullptr && &camera->node().scene() != this)
    {
        return false;
    }
    info_.active_camera_node = camera == nullptr ? 0 : camera->node().info().id;
    return true;
}

bool Scene::build_data(schema::SceneDataT& output) const
{
    output = {};
    schema::SceneDataT snapshot;
    snapshot.info = std::make_unique<schema::SceneInfoT>(info_);
    snapshot.nodes.reserve(nodes_.size());
    for (const auto& node : nodes_)
    {
        auto node_data = std::make_unique<schema::NodeDataT>();
        node_data->info = std::make_unique<schema::NodeInfoT>(node->info());
        node_data->components.reserve(node->components().size());
        for (const auto& component : node->components())
        {
            auto component_data = std::make_unique<schema::ComponentDataT>();
            if (!component->build_data(*component_data))
            {
                return false;
            }
            node_data->components.push_back(std::move(component_data));
        }
        snapshot.nodes.push_back(std::move(node_data));
    }
    output = std::move(snapshot);
    return true;
}

Node* Scene::find_local_node(std::uint64_t local_id) const noexcept
{
    const auto item = std::find_if(nodes_.begin(), nodes_.end(), [local_id](const auto& node)
                                   { return node->info().id == local_id; });
    return item == nodes_.end() ? nullptr : item->get();
}

bool Scene::restore_data(const schema::SceneDataT& data)
{
    if (!nodes_.empty() || data.schema_version != 1 || !data.info)
    {
        return false;
    }
    ComponentRegistry registry;
    if (!register_scene_components(registry))
    {
        return false;
    }
    std::set<std::uint64_t> identities;
    for (const auto& source : data.nodes)
    {
        if (!source || !source->info || source->info->id == 0 ||
            !identities.insert(source->info->id).second)
        {
            return false;
        }
        Node* node = create_node(source->info->name);
        if (!node)
        {
            return false;
        }
        node->info_.id = source->info->id;
        std::set<ComponentType> types;
        for (const auto& component : source->components)
        {
            if (!component || component->payload.value == nullptr ||
                !types.insert(component->payload.type).second)
            {
                return false;
            }
            Component* restored = node->get_component(component->payload.type);
            if (restored == nullptr)
            {
                restored = registry.create(component->payload.type, *node);
            }
            if (!restored || !restored->restore_data(*component))
            {
                return false;
            }
        }
        if (!types.contains(ComponentType::TransformData))
        {
            return false;
        }
    }
    node_sequence_ = identities.empty() ? 0 : *identities.rbegin();
    for (const auto& source : data.nodes)
    {
        auto* node = find_local_node(source->info->id);
        auto* parent = find_local_node(source->info->parent_id);
        if ((source->info->parent_id != 0 && parent == nullptr) || !node->set_parent(parent))
        {
            return false;
        }
    }
    Camera* camera = nullptr;
    if (data.info->active_camera_node != 0)
    {
        auto* node = find_local_node(data.info->active_camera_node);
        camera = node == nullptr ? nullptr : node->get_component<Camera>();
        if (camera == nullptr)
        {
            return false;
        }
    }
    info_.name = data.info->name;
    return set_active_camera(camera);
}

void Scene::destroy_entity(std::uint64_t id) noexcept
{
    if (ecs_is_alive(&world_, id))
    {
        ecs_delete(&world_, id);
    }
}

} // namespace engine
