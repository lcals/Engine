#include "runtime/scene/scene.hpp"

#include "runtime/resource/resource_system.hpp"

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
    for (const auto* ancestor = &node; ancestor; ancestor = ancestor->parent())
    {
        if (ancestor == &root)
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

Node* Scene::instantiate_prefab(std::string_view path, std::string_view name)
{
    auto* resources = ResourceSystem::instance();
    Guid guid;
    ResourcePtr resource;
    if (!resources || !ResourceSystem::path_to_guid(path, guid) ||
        !resources->loader().load(guid, resource) || !resource->prefab())
    {
        return nullptr;
    }
    const auto& prefab = *resource->prefab();
    if (prefab.schema_version != 1 || prefab.root_id == 0 || prefab.nodes.empty() ||
        prefab.nodes.size() > std::numeric_limits<std::uint64_t>::max() - node_sequence_)
    {
        return nullptr;
    }
    std::unordered_map<std::uint64_t, const schema::NodeDataT*> sources;
    std::unordered_map<std::uint64_t, std::uint64_t> ids;
    std::vector<schema::NodeIdMappingT> mapping;
    mapping.reserve(prefab.nodes.size());
    schema::PrefabInstanceDataT instance;
    instance.prefab_guid = guid;
    auto sequence = node_sequence_;
    for (const auto& source : prefab.nodes)
    {
        if (!source || !source->info || source->info->id == 0 ||
            !sources.emplace(source->info->id, source.get()).second)
        {
            return nullptr;
        }
        schema::NodeIdMappingT entry;
        entry.source_id = source->info->id;
        entry.target_id = ++sequence;
        mapping.push_back(entry);
        ids.emplace(entry.source_id, entry.target_id);
        instance.node_mappings.push_back(std::make_unique<schema::NodeIdMappingT>(entry));
    }
    const auto root_source = sources.find(prefab.root_id);
    if (root_source == sources.end() || root_source->second->info->parent_id != 0)
    {
        return nullptr;
    }
    for (const auto& source : prefab.nodes)
    {
        std::unordered_set<std::uint64_t> ancestors;
        auto id = source->info->id;
        while (id != prefab.root_id)
        {
            const auto found = sources.find(id);
            if (found == sources.end() || !ancestors.insert(id).second)
            {
                return nullptr;
            }
            id = found->second->info->parent_id;
        }
    }
    instance.root_id = ids.find(prefab.root_id)->second;
    const auto candidate_start = nodes_.size();
    // 组件恢复也可能生成临时节点；先保留全部模板身份，防止它们占用后续候选 ID。
    node_sequence_ = sequence;
    const auto rollback = [this, candidate_start]() -> Node*
    {
        // 每个候选都在末尾，包含尚未连接到根的节点及组件生成的子节点。
        nodes_.resize(candidate_start);
        return nullptr;
    };
    std::vector<schema::NodeDataT> candidates;
    candidates.reserve(prefab.nodes.size());
    for (const auto& source : prefab.nodes)
    {
        auto& candidate = candidates.emplace_back(*source);
        candidate.info->id = ids.find(source->info->id)->second;
        candidate.info->parent_id =
            source->info->parent_id == 0 ? 0 : ids.find(source->info->parent_id)->second;
        if (source->info->id == prefab.root_id && !name.empty())
        {
            candidate.info->name = name;
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
    auto* root = find_local_node(instance.root_id);
    if (!root)
    {
        return rollback();
    }
    if (!name.empty())
    {
        auto override = std::make_unique<schema::PrefabNodeOverrideT>();
        override->node_id = prefab.root_id;
        override->info = std::make_unique<schema::NodeInfoT>(root->info());
        override->fields.push_back("name");
        instance.overrides.push_back(std::move(override));
    }
    if (!set_prefab_instance(instance))
    {
        return rollback();
    }
    return root;
}

const std::vector<std::unique_ptr<schema::PrefabInstanceDataT>>&
Scene::prefab_instances() const noexcept
{
    return prefab_instances_;
}

bool Scene::valid_prefab_instance(const schema::PrefabInstanceDataT& instance) const
{
    const auto* root = find_local_node(instance.root_id);
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
        const auto* node = find_local_node(mapping->target_id);
        if (mapping->retired || removed.contains(mapping->source_id))
        {
            if (node || mapping->target_id == instance.root_id)
            {
                return false;
            }
        }
        else if (node)
        {
            if (!descendant_of(*node, *root))
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
    for (const auto& other : prefab_instances_)
    {
        if (other->root_id == instance.root_id)
        {
            continue;
        }
        const auto* other_root = find_local_node(other->root_id);
        if (!other_root || descendant_of(*root, *other_root) || descendant_of(*other_root, *root))
        {
            return false;
        }
        for (const auto& mapping : other->node_mappings)
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
    auto copy = std::make_unique<schema::PrefabInstanceDataT>(instance);
    for (const auto& mapping : instance.node_mappings)
    {
        node_sequence_ = std::max(node_sequence_, mapping->target_id);
    }
    const auto found =
        std::find_if(prefab_instances_.begin(), prefab_instances_.end(),
                     [&instance](const auto& item) { return item->root_id == instance.root_id; });
    if (found == prefab_instances_.end())
    {
        prefab_instances_.push_back(std::move(copy));
    }
    else
    {
        *found = std::move(copy);
    }
    return true;
}

bool Scene::remove_prefab_instance(std::uint64_t root_local_id) noexcept
{
    return std::erase_if(prefab_instances_, [root_local_id](const auto& instance)
                         { return instance->root_id == root_local_id; }) != 0;
}

} // namespace engine
