#include "editor/asset_pipeline/prefab_importer.hpp"

#include "editor/asset_pipeline/components/scene_component_codec.hpp"
#include "editor/asset_pipeline/importer_utils.hpp"
#include "editor/asset_pipeline/prefab_source.hpp"
#include "editor/asset_pipeline/scene_importer.hpp"
#include "editor/asset_pipeline/toml_helpers.hpp"
#include "runtime/resource/codecs/model_resource_codec.hpp"
#include "runtime/resource/codecs/prefab_resource_codec.hpp"

#include <algorithm>
#include <limits>
#include <map>
#include <set>

namespace engine::editor
{
namespace
{
using Nodes = std::vector<std::unique_ptr<schema::NodeDataT>>;

std::uint64_t stable_id(std::string_view key)
{
    std::uint64_t hash = 14695981039346656037ULL;
    for (const unsigned char byte : key)
    {
        hash ^= byte;
        hash *= 1099511628211ULL;
    }
    // 保留正常递增 ID 空间；碰撞由调用方明确拒绝。
    return (hash & 0x3fffffffffffffffULL) | 0x4000000000000000ULL;
}

bool apply_nodes(Nodes& nodes,
                 const std::vector<std::unique_ptr<schema::PrefabNodeOverrideT>>& overrides,
                 const std::map<std::uint64_t, std::uint64_t>& mapping, std::string& error)
{
    std::set<std::uint64_t> removed;
    for (const auto& value : overrides)
    {
        if (!value || !mapping.contains(value->node_id))
        {
            error = "Prefab override has no source node mapping";
            return false;
        }
        const auto id = mapping.at(value->node_id);
        const auto found = std::find_if(nodes.begin(), nodes.end(),
                                        [id](const auto& node) { return node->info->id == id; });
        if (found == nodes.end())
        {
            if (value->removed)
            {
                continue;
            }
            error = "Template removed a node that still has an instance override";
            return false;
        }
        auto& node = **found;
        if (value->removed)
        {
            removed.insert(id);
            continue;
        }
        for (const auto& field : value->fields)
        {
            if (!value->info)
            {
                error = "Prefab node override has no info";
                return false;
            }
            if (field == "name")
            {
                node.info->name = value->info->name;
            }
            else if (field == "parent")
            {
                node.info->parent_id = value->info->parent_id;
            }
            else if (field == "enabled")
            {
                node.info->enabled = value->info->enabled;
            }
            else if (field == "visible")
            {
                node.info->visible = value->info->visible;
            }
            else
            {
                error = "Unknown prefab node override field: " + field;
                return false;
            }
        }
        std::set<schema::ComponentPayload> changed;
        for (const auto& component : value->components)
        {
            if (!component || !component->component ||
                !changed.insert(component->component->payload.type).second)
            {
                error = "Invalid or duplicate prefab component override";
                return false;
            }
            const auto type = component->component->payload.type;
            const auto current =
                std::find_if(node.components.begin(), node.components.end(),
                             [type](const auto& item) { return item->payload.type == type; });
            if (component->removed)
            {
                if (current != node.components.end())
                {
                    node.components.erase(current);
                }
            }
            else if (current == node.components.end())
            {
                const auto* codec = asset_pipeline::find_scene_component_codec(type);
                if (!codec || !codec->validate(*component->component, error))
                {
                    return false;
                }
                node.components.push_back(
                    std::make_unique<schema::ComponentDataT>(*component->component));
            }
            else if (!PrefabImporter::apply_component_override(**current, *component, error))
            {
                return false;
            }
        }
    }
    bool changed = true;
    while (changed)
    {
        changed = false;
        for (const auto& node : nodes)
        {
            if (removed.contains(node->info->parent_id) && removed.insert(node->info->id).second)
            {
                changed = true;
            }
        }
    }
    std::erase_if(nodes, [&removed](const auto& node) { return removed.contains(node->info->id); });
    return true;
}
} // namespace

bool PrefabImporter::read(const std::filesystem::path& source, schema::PrefabDataT& output,
                          std::string& error)
{
    output = {};
    toml::table table;
    if (!asset_pipeline::read_table(source, table, error))
    {
        return false;
    }
    schema::PrefabDataT data;
    if (!asset_pipeline::identifier(table.get("root"), data.root_id) || data.root_id == 0)
    {
        error = "Prefab requires a nonzero root ID";
        return false;
    }
    data.model_guid = table["model"].value_or(std::string{});
    if (table.contains("model") && !table["model"].is_string())
    {
        error = "Prefab model must be a resource GUID";
        return false;
    }
    if (const auto* nodes = table["nodes"].as_array())
    {
        if (!SceneImporter::read_nodes(*nodes, data.nodes, error))
        {
            return false;
        }
    }
    else if (data.model_guid.empty() || table.contains("nodes"))
    {
        error = "Prefab requires a nodes array or a Model source";
        return false;
    }
    if (const auto* mappings = table["model_nodes"].as_array())
    {
        std::set<std::string> keys;
        std::set<std::uint64_t> ids{data.root_id};
        for (const auto& item : *mappings)
        {
            const auto* entry = item.as_table();
            auto mapping = std::make_unique<schema::PrefabModelNodeMappingT>();
            if (!entry || !(*entry)["source_key"].is_string() ||
                !asset_pipeline::identifier(entry->get("id"), mapping->id))
            {
                error = "Invalid Model node mapping";
                return false;
            }
            mapping->source_key = *(*entry)["source_key"].value<std::string>();
            if (mapping->source_key.empty() || mapping->id == 0 ||
                !keys.insert(mapping->source_key).second || !ids.insert(mapping->id).second)
            {
                error = "Model node mappings require unique keys and nonzero IDs";
                return false;
            }
            data.model_nodes.push_back(std::move(mapping));
        }
    }
    else if (table.contains("model_nodes"))
    {
        error = "Model node mappings must be an array";
        return false;
    }
    if (const auto* overrides = table["model_overrides"].as_array())
    {
        if (!prefab_detail::read_overrides(*overrides, data.model_overrides, error))
        {
            return false;
        }
    }
    else if (table.contains("model_overrides"))
    {
        error = "Model overrides must be an array";
        return false;
    }
    if (!data.model_guid.empty() && !data.nodes.empty())
    {
        error = "Model-backed prefab source uses model_overrides; authored nodes must be empty";
        return false;
    }
    if (data.model_guid.empty() && !validate(data, error))
    {
        return false;
    }
    output = std::move(data);
    return true;
}

bool PrefabImporter::write(const std::filesystem::path& destination,
                           const schema::PrefabDataT& data, std::string& error)
{
    if (!validate(data, error))
    {
        return false;
    }
    toml::table table{{"root", std::to_string(data.root_id)}};
    toml::array nodes;
    if (data.model_guid.empty())
    {
        if (!SceneImporter::write_nodes(data.nodes, nodes, error))
        {
            return false;
        }
    }
    else
    {
        table.insert("model", data.model_guid);
        toml::array mappings, overrides;
        for (const auto& mapping : data.model_nodes)
        {
            if (!mapping)
            {
                return false;
            }
            mappings.push_back(toml::table{{"source_key", mapping->source_key},
                                           {"id", std::to_string(mapping->id)}});
        }
        if (!prefab_detail::write_overrides(data.model_overrides, overrides, error))
        {
            return false;
        }
        table.insert("model_nodes", std::move(mappings));
        table.insert("model_overrides", std::move(overrides));
    }
    table.insert("nodes", std::move(nodes));
    return prefab_detail::write_document(destination, table, error);
}

bool PrefabImporter::validate(const schema::PrefabDataT& data, std::string& error)
{
    if (!PrefabResourceCodec::validate(data))
    {
        error = "Prefab must contain one rooted tree with unique nodes and components";
        return false;
    }
    schema::SceneDataT scene;
    scene.info = std::make_unique<schema::SceneInfoT>();
    scene.info->name = "Prefab";
    for (const auto& node : data.nodes)
    {
        scene.nodes.push_back(std::make_unique<schema::NodeDataT>(*node));
    }
    return SceneImporter::validate(scene, error);
}

bool PrefabImporter::apply_component_override(schema::ComponentDataT& in_out,
                                              const schema::PrefabComponentOverrideT& override_data,
                                              std::string& error)
{
    if (!override_data.component || override_data.removed ||
        override_data.component->payload.type != in_out.payload.type)
    {
        error = "Component override type does not match its template";
        return false;
    }
    toml::table base, values;
    if (!SceneImporter::write_component(in_out, base, error) ||
        !SceneImporter::write_component(*override_data.component, values, error))
    {
        return false;
    }
    std::set<std::string> fields;
    for (const auto& field : override_data.fields)
    {
        if (field == "type" || !fields.insert(field).second || !values.contains(field) ||
            !base.contains(field))
        {
            error = "Unknown or duplicate component override field: " + field;
            return false;
        }
        base.insert_or_assign(field, *values.get(field));
    }
    schema::ComponentDataT result;
    if (!SceneImporter::read_component(base, result, error))
    {
        return false;
    }
    const auto* codec = asset_pipeline::find_scene_component_codec(result.payload.type);
    if (!codec || !codec->validate(result, error))
    {
        return false;
    }
    in_out = std::move(result);
    return true;
}

bool PrefabImporter::from_model(const schema::ModelDataT& model,
                                std::string_view default_material_guid, schema::PrefabDataT& in_out,
                                std::string& error)
{
    if (!ModelResourceCodec::validate(model))
    {
        error = "Invalid prefab Model source";
        return false;
    }
    schema::PrefabDataT result(in_out);
    result.nodes.clear();
    if (result.root_id == 0)
    {
        result.root_id = stable_id("prefab-root");
    }
    std::map<std::string, std::uint64_t> identities;
    std::set<std::uint64_t> reserved{result.root_id};
    for (const auto& mapping : result.model_nodes)
    {
        if (!mapping || mapping->source_key.empty() || mapping->id == 0 ||
            !identities.emplace(mapping->source_key, mapping->id).second ||
            !reserved.insert(mapping->id).second)
        {
            error = "Invalid prefab Model identity mapping";
            return false;
        }
    }
    auto root = std::make_unique<schema::NodeDataT>();
    root->info = std::make_unique<schema::NodeInfoT>();
    root->info->id = result.root_id;
    root->info->name = "Prefab";
    auto transform = std::make_unique<schema::ComponentDataT>();
    transform->payload.Set(schema::TransformDataT{});
    root->components.push_back(std::move(transform));
    result.nodes.push_back(std::move(root));
    std::vector<std::string> keys;
    std::vector<std::uint64_t> ids;
    std::set<std::string> encountered;
    for (const auto& source : model.nodes)
    {
        // Legacy Model producers may omit source_key; derive an unambiguous hierarchy path.
        const auto key =
            source->source_key.empty()
                ? (source->parent < 0 ? std::string{}
                                      : keys[static_cast<std::size_t>(source->parent)] + "/") +
                      source->name
                : source->source_key;
        if (key.empty() || !encountered.insert(key).second)
        {
            error = "Model has missing or ambiguous stable node paths";
            return false;
        }
        keys.push_back(key);
        auto found = identities.find(key);
        if (found == identities.end())
        {
            const auto id = stable_id(key);
            if (!reserved.insert(id).second)
            {
                error = "Stable Model node identity collision; assign an explicit model_nodes ID";
                return false;
            }
            auto mapping = std::make_unique<schema::PrefabModelNodeMappingT>();
            mapping->source_key = key;
            mapping->id = id;
            result.model_nodes.push_back(std::move(mapping));
            found = identities.emplace(key, id).first;
        }
        auto node = std::make_unique<schema::NodeDataT>();
        node->info = std::make_unique<schema::NodeInfoT>();
        node->info->id = found->second;
        node->info->name = source->name;
        node->info->parent_id = source->parent < 0 ? result.root_id : ids[source->parent];
        ids.push_back(found->second);
        schema::TransformDataT settings;
        settings.position = source->position;
        settings.rotation = source->rotation;
        settings.scale = source->scale;
        auto component = std::make_unique<schema::ComponentDataT>();
        component->payload.Set(settings);
        node->components.push_back(std::move(component));
        if (!source->mesh_guid.empty())
        {
            schema::MeshRendererDataT mesh;
            mesh.mesh_guid = source->mesh_guid;
            if (source->material_guids.empty())
            {
                if (!GuidMap::valid_guid(default_material_guid))
                {
                    error = "Default material identity is unavailable";
                    return false;
                }
                mesh.material_guid = default_material_guid;
            }
            else
            {
                mesh.material_guid = source->material_guids.front();
                mesh.additional_material_guids.assign(source->material_guids.begin() + 1,
                                                      source->material_guids.end());
            }
            auto renderer = std::make_unique<schema::ComponentDataT>();
            renderer->payload.Set(std::move(mesh));
            node->components.push_back(std::move(renderer));
        }
        result.nodes.push_back(std::move(node));
    }
    std::map<std::uint64_t, std::uint64_t> identity{{result.root_id, result.root_id}};
    for (const auto& mapping : result.model_nodes)
    {
        identity.emplace(mapping->id, mapping->id);
    }
    if (!apply_nodes(result.nodes, result.model_overrides, identity, error) ||
        !validate(result, error))
    {
        return false;
    }
    in_out = std::move(result);
    return true;
}

bool PrefabImporter::expand_instance(const schema::PrefabDataT& prefab,
                                     schema::PrefabInstanceDataT& instance,
                                     std::uint64_t first_free_id, Nodes& output, std::string& error)
{
    output.clear();
    if (!validate(prefab, error) || instance.root_id == 0)
    {
        return false;
    }
    schema::PrefabInstanceDataT candidate(instance);
    std::map<std::uint64_t, std::uint64_t> mapping;
    std::set<std::uint64_t> targets;
    for (const auto& pair : candidate.node_mappings)
    {
        if (!pair || pair->source_id == 0 || pair->target_id == 0 ||
            !mapping.emplace(pair->source_id, pair->target_id).second ||
            !targets.insert(pair->target_id).second)
        {
            error = "Prefab instance identity mappings are invalid";
            return false;
        }
    }
    std::set<std::uint64_t> source_ids;
    for (const auto& source : prefab.nodes)
    {
        source_ids.insert(source->info->id);
    }
    for (auto& pair : candidate.node_mappings)
    {
        pair->retired = !source_ids.contains(pair->source_id);
    }
    const auto root = mapping.find(prefab.root_id);
    if (root == mapping.end() || root->second != candidate.root_id)
    {
        error = "Prefab root identity mapping is missing or has changed";
        return false;
    }
    for (const auto& source : prefab.nodes)
    {
        if (mapping.contains(source->info->id))
        {
            continue;
        }
        while (first_free_id != 0 && targets.contains(first_free_id))
        {
            ++first_free_id;
        }
        if (first_free_id == 0 || first_free_id == std::numeric_limits<std::uint64_t>::max())
        {
            error = "Scene local node identities are exhausted";
            return false;
        }
        auto pair = std::make_unique<schema::NodeIdMappingT>();
        pair->source_id = source->info->id;
        pair->target_id = first_free_id++;
        targets.insert(pair->target_id);
        mapping.emplace(pair->source_id, pair->target_id);
        candidate.node_mappings.push_back(std::move(pair));
    }
    std::vector<schema::NodeIdMappingT> references;
    for (const auto& [source, target] : mapping)
    {
        schema::NodeIdMappingT value;
        value.source_id = source;
        value.target_id = target;
        references.push_back(value);
    }
    Nodes expanded;
    for (const auto& source : prefab.nodes)
    {
        auto node = std::make_unique<schema::NodeDataT>(*source);
        node->info->id = mapping.at(source->info->id);
        node->info->parent_id =
            source->info->parent_id == 0 ? 0 : mapping.at(source->info->parent_id);
        for (auto& component : node->components)
        {
            const auto* codec = asset_pipeline::find_scene_component_codec(component->payload.type);
            if (!codec || !codec->remap_node_references(*component, references, error))
            {
                return false;
            }
        }
        expanded.push_back(std::move(node));
    }
    if (!apply_nodes(expanded, candidate.overrides, mapping, error))
    {
        return false;
    }
    if (std::none_of(expanded.begin(), expanded.end(),
                     [&](const auto& node) { return node->info->id == candidate.root_id; }))
    {
        error = "Prefab instance root cannot be removed by an override";
        return false;
    }
    // Runtime validates provenance without loading the source template.
    // Explicitly retain every removed descendant, as mappings carry no parent chain.
    for (const auto& pair : candidate.node_mappings)
    {
        if (pair->retired || std::any_of(expanded.begin(), expanded.end(), [&](const auto& node)
                                         { return node->info->id == pair->target_id; }))
        {
            continue;
        }
        auto found =
            std::find_if(candidate.overrides.begin(), candidate.overrides.end(),
                         [&](const auto& item) { return item->node_id == pair->source_id; });
        if (found == candidate.overrides.end())
        {
            auto removed = std::make_unique<schema::PrefabNodeOverrideT>();
            removed->node_id = pair->source_id;
            removed->removed = true;
            candidate.overrides.push_back(std::move(removed));
        }
        else
        {
            (*found)->removed = true;
        }
    }
    instance = std::move(candidate);
    output = std::move(expanded);
    return true;
}
} // namespace engine::editor
