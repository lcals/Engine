#include "runtime/scene/scene.hpp"

#include "runtime/resource/types/prefab_resource.hpp"
#include "runtime/scene/ecs_storage.hpp"
#include "runtime/scene/resource_binding.hpp"

#include <algorithm>
#include <limits>
#include <unordered_map>
#include <unordered_set>

namespace engine
{
namespace
{

bool descendant_of(const Node& node, const Node& root)
{
    for (auto ancestor = node; ancestor; ancestor = ancestor.parent())
    {
        if (ancestor == root)
        {
            return true;
        }
    }
    return false;
}

bool valid_fields(const std::vector<std::string>& fields)
{
    std::unordered_set<std::string_view> names;
    for (const auto& field : fields)
    {
        if (field.empty() || !names.insert(field).second)
        {
            return false;
        }
    }
    return true;
}

} // namespace

Node Scene::instantiate_prefab(std::string_view path, std::string_view name,
                               std::vector<Node>* created)
{
    if (created)
    {
        created->clear();
    }
    auto* resources = world_.resource_loader();
    ResourceReference resource;
    const auto owner = ecs_detail::ResourceBindingScope::owner(world_);
    if (!resources ||
        !(owner.id ? ecs_detail::acquire_dependency(resources, owner, path, resource)
                   : resource.acquire(resources, path)) ||
        !resource.data<PrefabResource>())
    {
        return {};
    }
    ecs_detail::ResourceBindingScope binding(world_, resource.handle());
    const auto& prefab = *resource.data<PrefabResource>();
    if (prefab.schema_version != 2 || prefab.root_id == 0 || prefab.nodes.empty() ||
        prefab.nodes.size() > std::numeric_limits<std::uint64_t>::max() - node_sequence_)
    {
        return {};
    }
    std::unordered_map<std::uint64_t, const schema::NodeDataT*> sources;
    std::unordered_map<std::uint64_t, std::uint64_t> ids;
    std::vector<schema::NodeIdMappingT> mapping;
    mapping.reserve(prefab.nodes.size());
    schema::PrefabInstanceDataT instance;
    instance.prefab_guid = resource->info().guid;
    auto sequence = node_sequence_;
    for (const auto& source : prefab.nodes)
    {
        if (!source || !source->info || !source->info->properties ||
            source->info->properties->id == 0 ||
            !sources.emplace(source->info->properties->id, source.get()).second)
        {
            return {};
        }
        schema::NodeIdMappingT entry;
        entry.source_id = source->info->properties->id;
        entry.target_id = ++sequence;
        mapping.push_back(entry);
        ids.emplace(entry.source_id, entry.target_id);
        instance.node_mappings.push_back(std::make_unique<schema::NodeIdMappingT>(entry));
    }
    const auto root_source = sources.find(prefab.root_id);
    if (root_source == sources.end() || root_source->second->info->parent_id != 0)
    {
        return {};
    }
    for (const auto& source : prefab.nodes)
    {
        std::unordered_set<std::uint64_t> ancestors;
        auto id = source->info->properties->id;
        while (id != prefab.root_id)
        {
            const auto found = sources.find(id);
            if (found == sources.end() || !ancestors.insert(id).second)
            {
                return {};
            }
            id = found->second->info->parent_id;
        }
    }
    instance.root_id = ids.find(prefab.root_id)->second;
    const auto candidate_start = order_sequence_ + 1;
    // 组件恢复也可能生成临时节点；先保留全部模板身份，防止它们占用后续候选 ID。
    node_sequence_ = sequence;
    const auto rollback = [this, candidate_start]() -> Node
    {
        rollback_created(candidate_start);
        return {};
    };
    std::vector<schema::NodeDataT> candidates;
    candidates.reserve(prefab.nodes.size());
    for (const auto& source : prefab.nodes)
    {
        auto& candidate = candidates.emplace_back(*source);
        candidate.info->properties->id = ids.find(source->info->properties->id)->second;
        candidate.info->parent_id =
            source->info->parent_id == 0 ? 0 : ids.find(source->info->parent_id)->second;
        if (source->info->properties->id == prefab.root_id && !name.empty())
        {
            candidate.info->properties->name = name;
        }
        if (!restore_node(candidate, mapping))
        {
            return rollback();
        }
    }
    for (const auto& candidate : candidates)
    {
        if (!restore_parent(candidate))
        {
            return rollback();
        }
    }
    auto root = find_local_node(instance.root_id);
    if (!root)
    {
        return rollback();
    }
    if (!name.empty())
    {
        auto override = std::make_unique<schema::PrefabNodeOverrideT>();
        override->node_id = prefab.root_id;
        override->info = std::make_unique<schema::NodeInfoT>();
        if (!root.build_info(*override->info))
        {
            return rollback();
        }
        override->fields.push_back("name");
        instance.overrides.push_back(std::move(override));
    }
    if (!set_prefab_instance(instance))
    {
        return rollback();
    }
    if (created && !collect_created(candidate_start, *created))
    {
        return rollback();
    }
    return root;
}

bool Scene::collect_prefab_instances(std::vector<schema::PrefabInstanceDataT>& output) const
{
    output.clear();
    std::vector<Node> nodes;
    if (!collect_nodes(nodes))
    {
        return false;
    }
    flecs::world current(world_.native_world());
    std::sort(nodes.begin(), nodes.end(),
              [&](Node left, Node right)
              {
                  return current.entity(left.id()).get<ecs_detail::NodeRuntime>().prefab_order <
                         current.entity(right.id()).get<ecs_detail::NodeRuntime>().prefab_order;
              });
    for (const auto node : nodes)
    {
        if (const auto* value = current.entity(node.id()).try_get<schema::PrefabInstanceDataT>())
        {
            output.push_back(*value);
        }
    }
    return true;
}

bool Scene::valid_prefab_instance(const schema::PrefabInstanceDataT& instance) const
{
    const auto root = find_local_node(instance.root_id);
    if (!root || !GuidMap::valid_guid(instance.prefab_guid) || instance.node_mappings.empty())
    {
        return false;
    }
    std::unordered_set<std::uint64_t> removed;
    std::unordered_set<std::uint64_t> overridden;
    for (const auto& override : instance.overrides)
    {
        if (!override || override->node_id == 0 || !overridden.insert(override->node_id).second ||
            !valid_fields(override->fields) || (!override->fields.empty() && !override->info))
        {
            return false;
        }
        if (override->removed)
        {
            removed.insert(override->node_id);
        }
        std::unordered_set<ComponentType> types;
        for (const auto& component : override->components)
        {
            if (!component || !component->component ||
                component->component->payload.type == ComponentType::NONE ||
                (!component->removed && !component->component->payload.value) ||
                !types.insert(component->component->payload.type).second ||
                !valid_fields(component->fields))
            {
                return false;
            }
        }
    }
    std::unordered_set<std::uint64_t> source_ids;
    std::unordered_set<std::uint64_t> target_ids;
    for (const auto& mapping : instance.node_mappings)
    {
        if (!mapping || mapping->source_id == 0 || mapping->target_id == 0 ||
            !source_ids.insert(mapping->source_id).second ||
            !target_ids.insert(mapping->target_id).second)
        {
            return false;
        }
        const auto node = find_local_node(mapping->target_id);
        if (mapping->retired || removed.contains(mapping->source_id))
        {
            if (node || mapping->target_id == instance.root_id)
            {
                return false;
            }
        }
        else if (node)
        {
            if (!descendant_of(node, root))
            {
                return false;
            }
        }
        else
        {
            return false;
        }
    }
    if (!target_ids.contains(instance.root_id))
    {
        return false;
    }
    for (const auto id : overridden)
    {
        if (!source_ids.contains(id))
        {
            return false;
        }
    }
    std::vector<schema::PrefabInstanceDataT> others;
    if (!collect_prefab_instances(others))
    {
        return false;
    }
    for (const auto& other : others)
    {
        if (other.root_id == instance.root_id)
        {
            continue;
        }
        const auto other_root = find_local_node(other.root_id);
        if (!other_root || descendant_of(root, other_root) || descendant_of(other_root, root))
        {
            return false;
        }
        for (const auto& mapping : other.node_mappings)
        {
            if (target_ids.contains(mapping->target_id))
            {
                return false;
            }
        }
    }
    return true;
}

bool Scene::set_prefab_instance(const schema::PrefabInstanceDataT& instance)
{
    if (!valid_prefab_instance(instance))
    {
        return false;
    }
    if (!world_.can_modify())
    {
        return false;
    }
    for (const auto& mapping : instance.node_mappings)
    {
        node_sequence_ = std::max(node_sequence_, mapping->target_id);
    }
    flecs::world current(world_.native_world());
    auto entity = current.entity(find_local_node(instance.root_id).id());
    const bool replacing = entity.has<schema::PrefabInstanceDataT>();
    if (!replacing && prefab_sequence_ == std::numeric_limits<std::uint64_t>::max())
    {
        return false;
    }
    entity.set<schema::PrefabInstanceDataT>(instance);
    if (!replacing)
    {
        entity.get_mut<ecs_detail::NodeRuntime>().prefab_order = ++prefab_sequence_;
    }
    return true;
}

bool Scene::remove_prefab_instance(std::uint64_t root_local_id) noexcept
{
    auto root = find_local_node(root_local_id);
    if (!root || !world_.can_modify())
    {
        return false;
    }
    flecs::world current(world_.native_world());
    auto entity = current.entity(root.id());
    if (!entity.has<schema::PrefabInstanceDataT>())
    {
        return false;
    }
    entity.remove<schema::PrefabInstanceDataT>();
    return true;
}

} // namespace engine
