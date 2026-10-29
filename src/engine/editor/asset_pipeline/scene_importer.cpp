#include "editor/asset_pipeline/scene_importer.hpp"

#include "editor/asset_pipeline/asset_io.hpp"
#include "editor/asset_pipeline/components/scene_component_codec.hpp"
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

namespace engine
{
namespace
{

bool validate(const schema::SceneDataT& scene, std::string& error)
{
    if (scene.schema_version != 1 || scene.info == nullptr || scene.info->name.empty())
    {
        error = "scene requires version 1 and a name";
        return false;
    }
    std::map<std::uint64_t, const schema::NodeDataT*> nodes;
    for (const auto& node : scene.nodes)
    {
        if (node == nullptr || node->info == nullptr || node->info->id == 0 ||
            !nodes.emplace(node->info->id, node.get()).second)
        {
            error = "scene node IDs must be unique and nonzero";
            return false;
        }
        std::set<schema::ComponentPayload> components;
        for (const auto& component : node->components)
        {
            if (component == nullptr || !components.insert(component->payload.type).second)
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
        }
        if (!components.contains(schema::ComponentPayload::TransformData))
        {
            error = "each scene node requires a Transform component";
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
    if (scene.info->active_camera_node != 0)
    {
        const auto camera = nodes.find(scene.info->active_camera_node);
        if (camera == nodes.end() ||
            std::none_of(camera->second->components.begin(), camera->second->components.end(),
                         [](const auto& component)
                         {
                             return component->enabled &&
                                    component->payload.type == schema::ComponentPayload::CameraData;
                         }))
        {
            error = "active camera must refer to a node with an enabled Camera component";
            return false;
        }
    }
    return true;
}

bool read_component(const toml::table& table, schema::ComponentDataT& component, std::string& error)
{
    using namespace asset_pipeline;
    component.enabled = table["enabled"].value_or(true);
    if (table.contains("enabled") && !table["enabled"].is_boolean())
    {
        error = "component enabled must be boolean";
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

bool SceneImporter::read(const std::filesystem::path& source, schema::SceneDataT& output,
                         std::string& error)
{
    output = {};
    error.clear();
    std::string text;
    if (!ResourceSystem::read_text(source, text))
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
    for (const auto& item : *nodes)
    {
        const auto* node_table = item.as_table();
        auto node = std::make_unique<schema::NodeDataT>();
        node->info = std::make_unique<schema::NodeInfoT>();
        if (node_table == nullptr ||
            !asset_pipeline::identifier(node_table->get("id"), node->info->id) ||
            (node_table->contains("parent") &&
             !asset_pipeline::identifier(node_table->get("parent"), node->info->parent_id)))
        {
            error = "node id/parent must be unsigned decimal IDs";
            return false;
        }
        node->info->name = (*node_table)["name"].value_or(std::string{});
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
        data.nodes.push_back(std::move(node));
    }
    if (!validate(data, error))
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
    for (const auto& node : data.nodes)
    {
        toml::table table{{"id", std::to_string(node->info->id)},
                          {"name", node->info->name},
                          {"parent", std::to_string(node->info->parent_id)}};
        toml::array components;
        for (const auto& component : node->components)
        {
            toml::table value{{"enabled", component->enabled}};
            const auto* codec = asset_pipeline::find_scene_component_codec(component->payload.type);
            if (codec == nullptr)
            {
                error = "unknown scene component type";
                return false;
            }
            codec->write(*component, value);
            components.push_back(std::move(value));
        }
        table.insert("components", std::move(components));
        nodes.push_back(std::move(table));
    }
    document.insert("nodes", std::move(nodes));
    std::ostringstream stream;
    stream << document;
    auto temporary = destination;
    temporary += ".saving-" + asset_pipeline::new_guid();
    if (!asset_pipeline::write_text(temporary, stream.str(), error))
    {
        return false;
    }
    std::error_code filesystem_error;
    // POSIX rename 替换已有目标；Windows 以同目录备份保护旧内容。
    auto backup = temporary;
    backup += ".previous";
    const bool existed = std::filesystem::exists(destination, filesystem_error);
    if (!filesystem_error && existed)
    {
        std::filesystem::rename(destination, backup, filesystem_error);
    }
    if (!filesystem_error)
    {
        std::filesystem::rename(temporary, destination, filesystem_error);
    }
    if (filesystem_error)
    {
        error = "cannot publish scene: " + filesystem_error.message();
        std::error_code cleanup_error;
        if (std::filesystem::exists(backup, cleanup_error))
        {
            std::filesystem::rename(backup, destination, cleanup_error);
        }
        std::filesystem::remove(temporary, cleanup_error);
        return false;
    }
    std::filesystem::remove(backup, filesystem_error);
    return true;
}

} // namespace engine
