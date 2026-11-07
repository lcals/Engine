#include "runtime/scene/scene.hpp"

#include "runtime/log/log_system.hpp"
#include "runtime/resource/resource_loader.hpp"
#include "runtime/scene/camera.hpp"
#include "runtime/scene/ecs_storage.hpp"
#include "runtime/scene/transform.hpp"

#include <algorithm>
#include <limits>
#include <set>
#include <unordered_set>

namespace engine
{
Scene::Scene(WorldSystem& world, std::uint64_t id) noexcept : world_(world), id_(id)
{
}

bool Scene::initialize(std::string_view name)
{
    if (!world_.can_modify() || metadata_entity_ != 0)
    {
        return false;
    }
    flecs::world current(world_.native_world());
    schema::SceneInfoT info;
    info.name = name;
    auto entity = current.entity();
    metadata_entity_ = entity.id();
    entity.set<schema::SceneInfoT>(std::move(info));
    return metadata_entity_ != 0;
}
Scene::~Scene() noexcept
{
    if (world_.native_world() && metadata_entity_ != 0 &&
        ecs_is_alive(world_.native_world(), metadata_entity_))
    {
        ecs_delete(world_.native_world(), metadata_entity_);
    }
    if (auto* loader = world_.resource_loader())
    {
        (void)loader->release(resource_);
    }
}
std::uint64_t Scene::id() const noexcept
{
    return id_;
}
WorldSystem& Scene::world() const noexcept
{
    return world_;
}
bool Scene::empty() const noexcept
{
    return local_node_index_.empty();
}
schema::SceneInfoT* Scene::mutable_info() const noexcept
{
    if (!world_.native_world() || metadata_entity_ == 0 ||
        !ecs_is_alive(world_.native_world(), metadata_entity_))
    {
        return nullptr;
    }
    flecs::world current(world_.native_world());
    return current.entity(metadata_entity_).try_get_mut<schema::SceneInfoT>();
}
std::string Scene::name() const
{
    const auto* info = mutable_info();
    return info ? info->name : std::string{};
}
Node Scene::create_node(std::string_view name)
{
    if (node_sequence_ == std::numeric_limits<std::uint64_t>::max())
    {
        return {};
    }
    return create_node(name, node_sequence_ + 1);
}
Node Scene::create_node(std::string_view name, std::uint64_t local_id)
{
    if (!world_.can_modify() || !mutable_info() || local_id == 0 ||
        local_node_index_.contains(local_id) ||
        order_sequence_ == std::numeric_limits<std::uint64_t>::max())
    {
        return {};
    }
    flecs::world current(world_.native_world());
    auto entity = current.entity();
    schema::NodePropertiesT properties;
    properties.id = local_id;
    properties.name = name;
    ecs_detail::NodeRuntime state;
    state.scene = this;
    state.order = ++order_sequence_;
    entity.set<schema::NodePropertiesT>(std::move(properties))
        .set<ecs_detail::NodeRuntime>(state)
        .add<ecs_detail::WorldTransform>()
        .add<ecs_detail::SceneMember>(metadata_entity_);
    Node node(world_, world_.epoch(), entity.id());
    local_node_index_.emplace(local_id, entity.id());
    node_sequence_ = std::max(node_sequence_, local_id);
    if (!node.add_component<Transform>())
    {
        entity.destruct();
        return {};
    }
    if (published_)
    {
        entity.add<ecs_detail::Published>();
    }
    world_.invalidate_scene_state();
    return node;
}
Node Scene::find_node(std::uint64_t id) const noexcept
{
    Node node(world_, world_.epoch(), id);
    return node && node.scene() == this ? node : Node{};
}
Node Scene::find_local_node(std::uint64_t local_id) const noexcept
{
    const auto item = local_node_index_.find(local_id);
    return item == local_node_index_.end() ? Node{} : find_node(item->second);
}
Node Scene::find_node(std::string_view name) const noexcept
{
    if (!world_.native_world())
    {
        return {};
    }
    flecs::world current(world_.native_world());
    Node found;
    std::uint64_t first_order = 0;
    for (const auto& [local_id, entity_id] : local_node_index_)
    {
        (void)local_id;
        const auto node = find_node(entity_id);
        if (!node)
        {
            continue;
        }
        const auto entity = current.entity(entity_id);
        const auto* properties = entity.try_get<schema::NodePropertiesT>();
        const auto* state = entity.try_get<ecs_detail::NodeRuntime>();
        if (properties && state && properties->name == name &&
            (!found || state->order < first_order))
        {
            found = node;
            first_order = state->order;
        }
    }
    return found;
}
bool Scene::destroy_node(std::uint64_t id)
{
    if (!world_.can_modify() || !find_node(id))
    {
        return false;
    }
    ecs_delete(world_.native_world(), id);
    world_.invalidate_scene_state();
    return true;
}
void Scene::unregister_entity(std::uint64_t entity, std::uint64_t local_id) noexcept
{
    const auto found = local_node_index_.find(local_id);
    if (found != local_node_index_.end() && found->second == entity)
    {
        local_node_index_.erase(found);
    }
    auto* info = mutable_info();
    if (info && info->active_camera_node == local_id)
    {
        info->active_camera_node = 0;
    }
    world_.invalidate_scene_state();
}
bool Scene::collect_nodes(std::vector<Node>& output) const
{
    output.clear();
    if (!mutable_info())
    {
        return false;
    }
    flecs::world current(world_.native_world());
    std::vector<std::pair<std::uint64_t, Node>> ordered;
    ordered.reserve(local_node_index_.size());
    for (const auto& [local_id, id] : local_node_index_)
    {
        (void)local_id;
        const auto node = find_node(id);
        if (!node)
        {
            return false;
        }
        const auto* state = current.entity(id).try_get<ecs_detail::NodeRuntime>();
        ordered.emplace_back(state->order, node);
    }
    std::sort(ordered.begin(), ordered.end(),
              [](const auto& left, const auto& right) { return left.first < right.first; });
    output.reserve(ordered.size());
    for (const auto& entry : ordered)
    {
        output.push_back(entry.second);
    }
    return true;
}
bool Scene::collect_created(std::uint64_t first_order, std::vector<Node>& output) const
{
    if (!collect_nodes(output))
    {
        return false;
    }
    flecs::world current(world_.native_world());
    std::erase_if(output,
                  [&](Node node)
                  {
                      return current.entity(node.id()).try_get<ecs_detail::NodeRuntime>()->order <
                             first_order;
                  });
    return true;
}
void Scene::rollback_created(std::uint64_t first_order) noexcept
{
    std::vector<Node> created;
    if (!collect_created(first_order, created))
    {
        return;
    }
    for (const auto node : created)
    {
        if (node)
        {
            (void)destroy_node(node.id());
        }
    }
}
bool Scene::publish()
{
    if (!world_.can_modify())
    {
        return false;
    }
    std::vector<Node> nodes;
    if (!collect_nodes(nodes))
    {
        return false;
    }
    flecs::world current(world_.native_world());
    for (const auto node : nodes)
    {
        current.entity(node.id()).add<ecs_detail::Published>();
    }
    published_ = true;
    world_.invalidate_scene_state();
    return true;
}
void Scene::retain_persistent()
{
    std::vector<Node> nodes;
    if (!collect_nodes(nodes))
    {
        return;
    }
    auto* info = mutable_info();
    if (info)
    {
        info->active_camera_node = 0;
    }
    // Only persistent roots survive; ChildOf cascade owns all descendant removal.
    for (const auto node : nodes)
    {
        if (node && !node.parent() && !node.persistent())
        {
            (void)destroy_node(node.id());
        }
    }
    if (auto* loader = world_.resource_loader())
    {
        (void)loader->release(resource_);
    }
}
Camera Scene::active_camera() const noexcept
{
    const auto* info = mutable_info();
    return info ? find_local_node(info->active_camera_node).get_component<Camera>() : Camera{};
}
bool Scene::set_active_camera(Camera camera) noexcept
{
    auto* info = mutable_info();
    if (!world_.can_modify() || !info ||
        (camera.instance_id() != 0 && (!camera || camera.node().scene() != this)))
    {
        return false;
    }
    info->active_camera_node = camera ? camera.node().info().properties->id : 0;
    return true;
}
bool Scene::build_data(schema::SceneDataT& output) const
{
    output = {};
    const auto* info = mutable_info();
    if (!info)
    {
        return false;
    }
    if (const auto camera = active_camera();
        info->active_camera_node != 0 &&
        (!camera || camera.transient() || camera.node().transient()))
    {
        return false;
    }
    std::vector<Node> nodes;
    if (!collect_nodes(nodes))
    {
        return false;
    }
    schema::SceneDataT snapshot;
    snapshot.info = std::make_unique<schema::SceneInfoT>(*info);
    for (const auto node : nodes)
    {
        if (node.transient())
        {
            continue;
        }
        auto data = std::make_unique<schema::NodeDataT>();
        data->info = std::make_unique<schema::NodeInfoT>();
        if (!node.build_info(*data->info))
        {
            return false;
        }
        std::vector<Component> components;
        if (!node.collect_components(components))
        {
            return false;
        }
        for (const auto& component : components)
        {
            if (component.transient())
            {
                continue;
            }
            auto value = std::make_unique<schema::ComponentDataT>();
            if (!component.build_data(*value))
            {
                return false;
            }
            data->components.push_back(std::move(value));
        }
        snapshot.nodes.push_back(std::move(data));
    }
    std::vector<schema::PrefabInstanceDataT> instances;
    if (!collect_prefab_instances(instances))
    {
        return false;
    }
    for (const auto& instance : instances)
    {
        const auto root = find_local_node(instance.root_id);
        if (root && root.transient())
        {
            continue;
        }
        if (!valid_prefab_instance(instance))
        {
            return false;
        }
        for (const auto& mapping : instance.node_mappings)
        {
            const auto node = find_local_node(mapping->target_id);
            if (node && node.transient())
            {
                return false;
            }
        }
        snapshot.prefab_instances.push_back(
            std::make_unique<schema::PrefabInstanceDataT>(instance));
    }
    output = std::move(snapshot);
    return true;
}
bool Scene::restore_data(const schema::SceneDataT& data)
{
    if (!empty() || data.schema_version != 1 || !data.info)
    {
        return false;
    }
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
    if (!source.info || !source.info->properties || source.info->properties->id == 0 ||
        find_local_node(source.info->properties->id))
    {
        return false;
    }
    const auto& properties = *source.info->properties;
    auto node = create_node(properties.name, properties.id);
    if (!node || !node.set_enabled(properties.enabled) || !node.set_visible(properties.visible))
    {
        return false;
    }
    std::set<ComponentType> types;
    for (const auto& component : source.components)
    {
        if (!component || !component->flags || component->payload.value == nullptr ||
            !types.insert(component->payload.type).second)
        {
            return false;
        }
        auto restored = node.get_component(component->payload.type);
        if (!restored)
        {
            restored = node.add_component(component->payload.type);
        }
        bool success = static_cast<bool>(restored);
        if (success && !mapping.empty())
        {
            auto candidate = *component;
            success = restored.remap_node_references(candidate, mapping) &&
                      restored.restore_data(candidate);
        }
        else if (success)
        {
            success = restored.restore_data(*component);
        }
        if (!success)
        {
            LogSystem::write(LogLevel::error, "Cannot restore component " +
                                                  std::string(schema::EnumNameComponentPayload(
                                                      component->payload.type)) +
                                                  " on scene node: " + properties.name);
            return false;
        }
    }
    return types.contains(ComponentType::TransformData);
}
bool Scene::restore_parent(const schema::NodeDataT& source)
{
    if (!source.info || !source.info->properties)
    {
        return false;
    }
    auto node = find_local_node(source.info->properties->id);
    const auto parent = find_local_node(source.info->parent_id);
    return node && (source.info->parent_id == 0 || parent) && node.set_parent(parent);
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
    Camera camera;
    if (data.info->active_camera_node != 0)
    {
        camera = find_local_node(data.info->active_camera_node).get_component<Camera>();
        if (!camera)
        {
            return false;
        }
    }
    auto* info = mutable_info();
    if (!info)
    {
        return false;
    }
    info->name = data.info->name;
    return set_active_camera(camera);
}
} // namespace engine
