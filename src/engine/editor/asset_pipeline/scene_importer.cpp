#include "editor/asset_pipeline/scene_importer.hpp"
#include "editor/file_text.hpp"

#include "editor/asset_pipeline/asset_io.hpp"
#include "editor/asset_pipeline/components/scene_component_codec.hpp"
#include "editor/asset_pipeline/prefab_source.hpp"
#include "editor/asset_pipeline/toml_helpers.hpp"
#include "runtime/resource/resource_system.hpp"
#include "runtime/utils/path_utils.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace engine::editor
{
namespace
{

bool validate_scene(const schema::SceneDataT& scene, std::string& error)
{
    if (scene.schema_version != 2 || scene.info == nullptr || scene.info->name.empty())
    {
        error = "scene requires version 1 and a name";
        return false;
    }
    std::map<std::uint64_t, const schema::NodeDataT*> nodes;
    bool has_environment = false;
    bool has_shadow_light = false;
    for (const auto& node : scene.nodes)
    {
        if (node == nullptr || node->info == nullptr || !node->info->properties ||
            node->info->properties->id == 0 ||
            !nodes.emplace(node->info->properties->id, node.get()).second)
        {
            error = "scene node IDs must be unique and nonzero";
            return false;
        }
        std::set<schema::ComponentPayload> components;
        for (const auto& component : node->components)
        {
            if (component == nullptr || !component->flags ||
                !components.insert(component->payload.type).second)
            {
                error = "scene node components must be unique";
                return false;
            }
            const auto* codec = asset_pipeline::find_scene_component_codec(component->payload.type);
            if (codec == nullptr)
            {
                error = "unknown scene component";
                return false;
            }
            if (!codec->validate(*component, error))
            {
                return false;
            }
            if (component->flags->enabled &&
                component->payload.type == schema::ComponentPayload::EnvironmentData)
            {
                if (has_environment)
                {
                    error = "scene supports at most one enabled Environment component";
                    return false;
                }
                has_environment = true;
            }
            if (const auto* light = component->payload.AsLightData();
                component->flags->enabled && light != nullptr &&
                light->kind == schema::LightKind::directional && light->cast_shadows)
            {
                if (has_shadow_light)
                {
                    error = "scene supports at most one enabled shadow-casting directional light";
                    return false;
                }
                has_shadow_light = true;
            }
        }
        if (!components.contains(schema::ComponentPayload::TransformData))
        {
            error = "each scene node requires a Transform component";
            return false;
        }
        if (components.contains(schema::ComponentPayload::PostProcessData) &&
            !components.contains(schema::ComponentPayload::CameraData))
        {
            error = "PostProcess component requires a Camera on the same node";
            return false;
        }
    }
    for (const auto& [id, node] : nodes)
    {
        std::set<std::uint64_t> ancestors{id};
        auto parent = node->info->parent_id;
        while (parent != 0)
        {
            const auto iterator = nodes.find(parent);
            if (iterator == nodes.end() || !ancestors.insert(parent).second)
            {
                error = "scene parent is missing or hierarchy has a cycle";
                return false;
            }
            parent = iterator->second->info->parent_id;
        }
    }
    std::vector<schema::NodeIdMappingT> references;
    for (const auto& [id, node] : nodes)
    {
        schema::NodeIdMappingT mapping;
        mapping.source_id = id;
        mapping.target_id = id;
        references.push_back(mapping);
    }
    for (const auto& [id, node] : nodes)
    {
        for (const auto& component : node->components)
        {
            const auto* codec = asset_pipeline::find_scene_component_codec(component->payload.type);
            schema::ComponentDataT candidate(*component);
            if (!codec->remap_node_references(candidate, references, error))
            {
                return false;
            }
        }
    }
    if (scene.info->active_camera_node != 0)
    {
        const auto camera = nodes.find(scene.info->active_camera_node);
        if (camera == nodes.end() ||
            std::none_of(camera->second->components.begin(), camera->second->components.end(),
                         [](const auto& component)
                         {
                             return component->flags->enabled &&
                                    component->payload.type == schema::ComponentPayload::CameraData;
                         }))
        {
            error = "active camera must refer to a node with an enabled Camera component";
            return false;
        }
    }
    return true;
}

bool parse_component(const toml::table& table, schema::ComponentDataT& component,
                     std::string& error)
{
    using namespace asset_pipeline;
    component.flags = std::make_unique<schema::ComponentFlagsT>();
    component.flags->enabled = table["enabled"].value_or(true);
    component.flags->visible = table["visible"].value_or(true);
    if ((table.contains("enabled") && !table["enabled"].is_boolean()) ||
        (table.contains("visible") && !table["visible"].is_boolean()))
    {
        error = "component enabled/visible must be boolean";
        return false;
    }
    const auto type = table["type"].value<std::string_view>();
    const auto* codec = find_scene_component_codec(type.value_or(std::string_view{}));
    if (codec == nullptr)
    {
        error = "unknown scene component type";
        return false;
    }
    return codec->read(table, component, error);
}

} // namespace

bool SceneImporter::validate(const schema::SceneDataT& data, std::string& error)
{
    return validate_scene(data, error);
}

bool SceneImporter::read_component(const toml::table& table, schema::ComponentDataT& output,
                                   std::string& error)
{
    return parse_component(table, output, error);
}

bool SceneImporter::write_component(const schema::ComponentDataT& component, toml::table& output,
                                    std::string& error)
{
    const auto* codec = asset_pipeline::find_scene_component_codec(component.payload.type);
    if (!component.flags || !codec || !codec->validate(component, error))
    {
        if (error.empty())
        {
            error = "Unknown or invalid component";
        }
        return false;
    }
    output =
        toml::table{{"enabled", component.flags->enabled}, {"visible", component.flags->visible}};
    codec->write(component, output);
    return true;
}

bool SceneImporter::read_nodes(const toml::array& nodes,
                               std::vector<std::unique_ptr<schema::NodeDataT>>& output,
                               std::string& error)
{
    output.clear();
    std::vector<std::unique_ptr<schema::NodeDataT>> candidate;
    for (const auto& item : nodes)
    {
        const auto* node_table = item.as_table();
        auto node = std::make_unique<schema::NodeDataT>();
        node->info = std::make_unique<schema::NodeInfoT>();
        node->info->properties = std::make_unique<schema::NodePropertiesT>();
        if (node_table == nullptr ||
            !asset_pipeline::identifier(node_table->get("id"), node->info->properties->id) ||
            (node_table->contains("parent") &&
             !asset_pipeline::identifier(node_table->get("parent"), node->info->parent_id)))
        {
            error = "node id/parent must be unsigned decimal IDs";
            return false;
        }
        node->info->properties->name = (*node_table)["name"].value_or(std::string{});
        if ((node_table->contains("enabled") && !(*node_table)["enabled"].is_boolean()) ||
            (node_table->contains("visible") && !(*node_table)["visible"].is_boolean()))
        {
            error = "node enabled/visible must be boolean";
            return false;
        }
        node->info->properties->enabled = (*node_table)["enabled"].value_or(true);
        node->info->properties->visible = (*node_table)["visible"].value_or(true);
        const auto* components = (*node_table)["components"].as_array();
        if (components == nullptr)
        {
            error = "node requires components array";
            return false;
        }
        for (const auto& item_component : *components)
        {
            const auto* component_table = item_component.as_table();
            auto component = std::make_unique<schema::ComponentDataT>();
            component->flags = std::make_unique<schema::ComponentFlagsT>();
            if (component_table == nullptr || !read_component(*component_table, *component, error))
            {
                if (error.empty())
                {
                    error = "component must be a table";
                }
                return false;
            }
            node->components.push_back(std::move(component));
        }
        candidate.push_back(std::move(node));
    }
    output = std::move(candidate);
    return true;
}

bool SceneImporter::write_nodes(const std::vector<std::unique_ptr<schema::NodeDataT>>& input,
                                toml::array& output, std::string& error)
{
    output.clear();
    for (const auto& node : input)
    {
        toml::table table{{"id", std::to_string(node->info->properties->id)},
                          {"name", node->info->properties->name},
                          {"parent", std::to_string(node->info->parent_id)}};
        if (!node->info->properties->enabled)
        {
            table.insert("enabled", false);
        }
        if (!node->info->properties->visible)
        {
            table.insert("visible", false);
        }
        toml::array components;
        for (const auto& component : node->components)
        {
            toml::table value;
            if (!component || !write_component(*component, value, error))
            {
                return false;
            }
            components.push_back(std::move(value));
        }
        table.insert("components", std::move(components));
        output.push_back(std::move(table));
    }
    return true;
}

bool SceneImporter::read(const std::filesystem::path& source, schema::SceneDataT& output,
                         std::string& error)
{
    output = {};
    error.clear();
    std::string text;
    if (!engine::editor::file_detail::read_text(source, text))
    {
        error = "cannot read scene: " + PathUtils::to_utf8(source);
        return false;
    }
    auto parsed = toml::parse(text);
    if (!parsed)
    {
        error = "invalid scene TOML: " + std::string(parsed.error().description());
        return false;
    }
    const auto& table = parsed.table();
    schema::SceneDataT data;
    data.schema_version = 2;
    data.info = std::make_unique<schema::SceneInfoT>();
    data.info->name = table["name"].value_or(std::string{});
    if (table.contains("active_camera") &&
        !asset_pipeline::identifier(table.get("active_camera"), data.info->active_camera_node))
    {
        error = "active_camera must be an unsigned decimal ID";
        return false;
    }
    const auto* nodes = table["nodes"].as_array();
    if (nodes == nullptr)
    {
        error = "scene requires nodes array";
        return false;
    }
    if (!read_nodes(*nodes, data.nodes, error) ||
        !prefab_detail::read_instances(table, data.prefab_instances, error))
    {
        return false;
    }
    if (data.prefab_instances.empty() && !validate(data, error))
    {
        return false;
    }
    output = std::move(data);
    return true;
}

bool SceneImporter::write(const std::filesystem::path& destination, const schema::SceneDataT& data,
                          std::string& error)
{
    error.clear();
    if (!validate(data, error))
    {
        return false;
    }
    toml::table document{{"name", data.info->name},
                         {"active_camera", std::to_string(data.info->active_camera_node)}};
    toml::array nodes;
    std::vector<std::unique_ptr<schema::NodeDataT>> ordinary;
    std::set<std::uint64_t> mapped;
    for (const auto& instance : data.prefab_instances)
    {
        for (const auto& mapping : instance->node_mappings)
        {
            if (mapping)
            {
                mapped.insert(mapping->target_id);
            }
        }
    }
    for (const auto& node : data.nodes)
    {
        if (node && node->info && !mapped.contains(node->info->properties->id))
        {
            ordinary.push_back(std::make_unique<schema::NodeDataT>(*node));
        }
    }
    if (!write_nodes(ordinary, nodes, error) ||
        !prefab_detail::write_instances(data.prefab_instances, document, error))
    {
        return false;
    }
    document.insert("nodes", std::move(nodes));
    return prefab_detail::write_document(destination, document, error);
}

} // namespace engine::editor
