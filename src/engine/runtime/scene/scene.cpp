#include "runtime/scene/scene.hpp"

#include "runtime/log/log_system.hpp"
#include "runtime/scene/camera.hpp"
#include "runtime/scene/component_registry.hpp"
#include "runtime/scene/transform.hpp"

#include <flecs.h>

#include <algorithm>
#include <limits>
#include <new>
#include <set>
#include <string>
#include <unordered_set>
#include <utility>

namespace engine
{

Scene::Scene(ecs_world_t& world, std::uint64_t id, std::string_view name,
             const ComponentRegistry& components)
    : world_(world), components_(components), id_(id)
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
    return create_node(name, node_sequence_ + 1);
}

Node* Scene::create_node(std::string_view name, std::uint64_t local_id)
{
    if (local_id == 0 || local_node_index_.contains(local_id))
    {
        return nullptr;
    }
    const ecs_entity_t entity = ecs_new(&world_);
    if (entity == 0)
    {
        return nullptr;
    }
    std::unique_ptr<Node> node(new (std::nothrow) Node(*this, entity, local_id, name));
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
    node_index_.emplace(entity, result);
    local_node_index_.emplace(local_id, result);
    node_sequence_ = std::max(node_sequence_, local_id);
    return result;
}

Node* Scene::find_node(std::uint64_t id) noexcept
{
    return const_cast<Node*>(std::as_const(*this).find_node(id));
}

const Node* Scene::find_node(std::uint64_t id) const noexcept
{
    if (id == 0)
    {
        return nullptr;
    }
    const auto iterator = node_index_.find(id);
    return iterator == node_index_.end() ? nullptr : iterator->second;
}

bool Scene::destroy_node(std::uint64_t id)
{
    Node* root = find_node(id);
    if (root == nullptr)
    {
        return false;
    }
    // Classify each ancestor once before destruction, including children created before parents.
    // Node addresses are stable; erase keeps the relative order of surviving owners.
    std::unordered_map<const Node*, bool> removed;
    removed.emplace(root, true);
    std::vector<const Node*> branch;
    for (const auto& node : nodes_)
    {
        if (node->info().parent_id == 0)
        {
            continue;
        }
        branch.clear();
        bool remove = false;
        for (const Node* ancestor = node.get(); ancestor != nullptr; ancestor = ancestor->parent())
        {
            const auto known = removed.find(ancestor);
            if (known != removed.end())
            {
                remove = known->second;
                break;
            }
            if (ancestor->info().parent_id == 0)
            {
                break;
            }
            branch.push_back(ancestor);
        }
        for (const Node* ancestor : branch)
        {
            removed.emplace(ancestor, remove);
        }
    }
    const auto should_remove = [&removed](const Node* node)
    {
        const auto found = removed.find(node);
        return found != removed.end() && found->second;
    };
    if (should_remove(find_local_node(info_.active_camera_node)))
    {
        info_.active_camera_node = 0;
    }
    std::erase_if(prefab_instances_, [this, &should_remove](const auto& instance)
                  { return should_remove(find_local_node(instance->root_id)); });
    std::erase_if(nodes_, [&should_remove](const auto& node) { return should_remove(node.get()); });
    return true;
}

const std::vector<std::unique_ptr<Node>>& Scene::nodes() const noexcept
{
    return nodes_;
}

void Scene::retain_persistent()
{
    // 在销毁任何父节点前确定完整保留集合，节点创建顺序不要求父节点在前。
    std::unordered_set<const Node*> retained;
    for (const auto& node : nodes_)
    {
        if (node->persistent())
        {
            retained.insert(node.get());
        }
    }
    info_.active_camera_node = 0;
    std::erase_if(prefab_instances_, [this, &retained](const auto& instance)
                  { return !retained.contains(find_local_node(instance->root_id)); });
    std::erase_if(nodes_, [&retained](const auto& node) { return !retained.contains(node.get()); });
    resource_.reset();
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
    if (const auto* camera = active_camera(); camera != nullptr && camera->node().transient())
    {
        return false;
    }
    schema::SceneDataT snapshot;
    snapshot.info = std::make_unique<schema::SceneInfoT>(info_);
    snapshot.nodes.reserve(nodes_.size());
    for (const auto& node : nodes_)
    {
        if (node->transient())
        {
            continue;
        }
        auto node_data = std::make_unique<schema::NodeDataT>();
        node_data->info = std::make_unique<schema::NodeInfoT>(node->info());
        node_data->components.reserve(node->components().size());
        for (const auto& component : node->components())
        {
            if (component->transient())
            {
                continue;
            }
            auto component_data = std::make_unique<schema::ComponentDataT>();
            if (!component->build_data(*component_data))
            {
                return false;
            }
            node_data->components.push_back(std::move(component_data));
        }
        snapshot.nodes.push_back(std::move(node_data));
    }
    for (const auto& instance : prefab_instances_)
    {
        const auto* root = find_local_node(instance->root_id);
        if (root && root->transient())
        {
            continue;
        }
        if (!valid_prefab_instance(*instance))
        {
            return false;
        }
        for (const auto& mapping : instance->node_mappings)
        {
            const auto* node = find_local_node(mapping->target_id);
            if (node && node->transient())
            {
                return false;
            }
        }
        snapshot.prefab_instances.push_back(
            std::make_unique<schema::PrefabInstanceDataT>(*instance));
    }
    output = std::move(snapshot);
    return true;
}

Node* Scene::find_local_node(std::uint64_t local_id) const noexcept
{
    if (local_id == 0)
    {
        return nullptr;
    }
    const auto item = local_node_index_.find(local_id);
    return item == local_node_index_.end() ? nullptr : item->second;
}

bool Scene::restore_data(const schema::SceneDataT& data)
{
    if (!nodes_.empty() || data.schema_version != 1 || !data.info)
    {
        return false;
    }
    nodes_.reserve(data.nodes.size());
    node_index_.reserve(data.nodes.size());
    local_node_index_.reserve(data.nodes.size());
    for (const auto& source : data.nodes)
    {
        if (!source || !restore_node(*source))
        {
            return false;
        }
    }
    for (const auto& source : data.nodes)
    {
        if (!restore_parent(*source))
        {
            return false;
        }
    }
    return finish_restore(data);
}

bool Scene::restore_node(const schema::NodeDataT& source,
                         std::span<const schema::NodeIdMappingT> mapping)
{
    if (!source.info || source.info->id == 0 || find_local_node(source.info->id))
    {
        return false;
    }
    Node* node = create_node(source.info->name, source.info->id);
    if (!node)
    {
        return false;
    }
    node->set_enabled(source.info->enabled);
    node->set_visible(source.info->visible);
    std::set<ComponentType> types;
    for (const auto& component : source.components)
    {
        if (!component || component->payload.value == nullptr ||
            !types.insert(component->payload.type).second)
        {
            return false;
        }
        Component* restored = node->get_component(component->payload.type);
        if (restored == nullptr)
        {
            restored = components_.create(component->payload.type, *node);
        }
        bool success = restored != nullptr;
        if (success && !mapping.empty())
        {
            auto candidate = *component;
            success = restored->remap_node_references(candidate, mapping) &&
                      restored->restore_data(candidate);
        }
        else if (success)
        {
            success = restored->restore_data(*component);
        }
        if (!success)
        {
            LogSystem::write(LogLevel::error, "Cannot restore component " +
                                                  std::string(schema::EnumNameComponentPayload(
                                                      component->payload.type)) +
                                                  " on scene node: " + source.info->name);
            return false;
        }
    }
    return types.contains(ComponentType::TransformData);
}

bool Scene::restore_parent(const schema::NodeDataT& source)
{
    auto* node = find_local_node(source.info->id);
    auto* parent = find_local_node(source.info->parent_id);
    return node != nullptr && (source.info->parent_id == 0 || parent != nullptr) &&
           node->set_parent(parent);
}

bool Scene::finish_restore(const schema::SceneDataT& data)
{
    std::unordered_set<std::uint64_t> roots;
    for (const auto& instance : data.prefab_instances)
    {
        if (!instance || !roots.insert(instance->root_id).second || !set_prefab_instance(*instance))
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

void Scene::destroy_entity(std::uint64_t id, std::uint64_t local_id) noexcept
{
    node_index_.erase(id);
    local_node_index_.erase(local_id);
    if (ecs_is_alive(&world_, id))
    {
        ecs_delete(&world_, id);
    }
}

} // namespace engine
