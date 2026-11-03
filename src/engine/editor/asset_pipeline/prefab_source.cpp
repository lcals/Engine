#include "editor/asset_pipeline/prefab_source.hpp"

#include "editor/asset_pipeline/asset_io.hpp"
#include "editor/asset_pipeline/scene_importer.hpp"
#include "editor/asset_pipeline/toml_helpers.hpp"

#include <set>
#include <sstream>
#include <system_error>

namespace engine::editor::prefab_detail
{
namespace
{
bool fields(const toml::table& input, std::vector<std::string>& output, std::string& error)
{
    output.clear();
    const auto* values = input["fields"].as_array();
    if (!values)
    {
        if (!input.contains("fields"))
        {
            return true;
        }
        error = "Override fields must be an array";
        return false;
    }
    std::set<std::string> seen;
    for (const auto& value : *values)
    {
        const auto name = value.value<std::string>();
        if (!name || name->empty() || !seen.insert(*name).second)
        {
            error = "Override fields must be unique nonempty names";
            return false;
        }
        output.push_back(*name);
    }
    return true;
}

toml::array field_array(const std::vector<std::string>& input)
{
    toml::array result;
    for (const auto& field : input)
    {
        result.push_back(field);
    }
    return result;
}

bool read_info(const toml::table& table, schema::NodeInfoT& output, std::string& error)
{
    output.properties = std::make_unique<schema::NodePropertiesT>();
    if ((table.contains("parent") &&
         !asset_pipeline::identifier(table.get("parent"), output.parent_id)) ||
        (table.contains("enabled") && !table["enabled"].is_boolean()) ||
        (table.contains("visible") && !table["visible"].is_boolean()) ||
        (table.contains("name") && !table["name"].is_string()))
    {
        error = "Invalid prefab node override fields";
        return false;
    }
    output.properties->name = table["name"].value_or(std::string{});
    output.properties->enabled = table["enabled"].value_or(true);
    output.properties->visible = table["visible"].value_or(true);
    return true;
}
} // namespace

bool read_overrides(const toml::array& input,
                    std::vector<std::unique_ptr<schema::PrefabNodeOverrideT>>& output,
                    std::string& error)
{
    output.clear();
    std::set<std::uint64_t> nodes;
    for (const auto& item : input)
    {
        const auto* table = item.as_table();
        auto value = std::make_unique<schema::PrefabNodeOverrideT>();
        if (!table || !asset_pipeline::identifier(table->get("node"), value->node_id) ||
            value->node_id == 0 || !nodes.insert(value->node_id).second ||
            !fields(*table, value->fields, error) ||
            (table->contains("removed") && !(*table)["removed"].is_boolean()))
        {
            if (error.empty())
            {
                error = "Invalid or duplicate prefab node override";
            }
            return false;
        }
        value->removed = (*table)["removed"].value_or(false);
        if (const auto* info = (*table)["info"].as_table())
        {
            value->info = std::make_unique<schema::NodeInfoT>();
            value->info->properties = std::make_unique<schema::NodePropertiesT>();
            if (!read_info(*info, *value->info, error))
            {
                return false;
            }
        }
        else if (table->contains("info") || !value->fields.empty())
        {
            error = "Node override fields require an info table";
            return false;
        }
        std::set<schema::ComponentPayload> components;
        if (const auto* list = (*table)["components"].as_array())
        {
            for (const auto& entry : *list)
            {
                const auto* override_table = entry.as_table();
                auto component = std::make_unique<schema::PrefabComponentOverrideT>();
                component->component = std::make_unique<schema::ComponentDataT>();
                component->component->flags = std::make_unique<schema::ComponentFlagsT>();
                const auto* data =
                    override_table ? (*override_table)["component"].as_table() : nullptr;
                if (!data || !SceneImporter::read_component(*data, *component->component, error) ||
                    !components.insert(component->component->payload.type).second ||
                    !fields(*override_table, component->fields, error) ||
                    (override_table->contains("removed") &&
                     !(*override_table)["removed"].is_boolean()))
                {
                    if (error.empty())
                    {
                        error = "Invalid or duplicate component override";
                    }
                    return false;
                }
                component->removed = (*override_table)["removed"].value_or(false);
                value->components.push_back(std::move(component));
            }
        }
        else if (table->contains("components"))
        {
            error = "Component overrides must be an array";
            return false;
        }
        output.push_back(std::move(value));
    }
    return true;
}

bool write_overrides(const std::vector<std::unique_ptr<schema::PrefabNodeOverrideT>>& input,
                     toml::array& output, std::string& error)
{
    output.clear();
    for (const auto& value : input)
    {
        if (!value || value->node_id == 0)
        {
            error = "Invalid prefab node override";
            return false;
        }
        toml::table table{{"node", std::to_string(value->node_id)},
                          {"fields", field_array(value->fields)},
                          {"removed", value->removed}};
        if (value->info)
        {
            table.insert("info", toml::table{{"name", value->info->properties->name},
                                             {"parent", std::to_string(value->info->parent_id)},
                                             {"enabled", value->info->properties->enabled},
                                             {"visible", value->info->properties->visible}});
        }
        toml::array components;
        for (const auto& component : value->components)
        {
            toml::table data;
            if (!component || !component->component ||
                !SceneImporter::write_component(*component->component, data, error))
            {
                return false;
            }
            components.push_back(toml::table{{"fields", field_array(component->fields)},
                                             {"removed", component->removed},
                                             {"component", std::move(data)}});
        }
        table.insert("components", std::move(components));
        output.push_back(std::move(table));
    }
    return true;
}

bool read_instances(const toml::table& input,
                    std::vector<std::unique_ptr<schema::PrefabInstanceDataT>>& output,
                    std::string& error)
{
    output.clear();
    const auto* instances = input["prefab_instances"].as_array();
    if (!instances)
    {
        if (!input.contains("prefab_instances"))
        {
            return true;
        }
        error = "prefab_instances must be an array";
        return false;
    }
    for (const auto& item : *instances)
    {
        const auto* table = item.as_table();
        auto instance = std::make_unique<schema::PrefabInstanceDataT>();
        if (!table || !asset_pipeline::identifier(table->get("root"), instance->root_id) ||
            instance->root_id == 0 || !(*table)["prefab"].is_string())
        {
            error = "Prefab instance requires a source GUID and root local ID";
            return false;
        }
        instance->prefab_guid = *(*table)["prefab"].value<std::string>();
        const auto* mappings = (*table)["node_mappings"].as_array();
        if (!mappings)
        {
            error = "Prefab instance requires node_mappings";
            return false;
        }
        std::set<std::uint64_t> sources, targets;
        for (const auto& mapping : *mappings)
        {
            const auto* entry = mapping.as_table();
            auto pair = std::make_unique<schema::NodeIdMappingT>();
            if (!entry || !asset_pipeline::identifier(entry->get("source"), pair->source_id) ||
                !asset_pipeline::identifier(entry->get("target"), pair->target_id) ||
                pair->source_id == 0 || pair->target_id == 0 ||
                !sources.insert(pair->source_id).second || !targets.insert(pair->target_id).second)
            {
                error = "Prefab mappings must have unique nonzero source and target IDs";
                return false;
            }
            if (entry->contains("retired") && !(*entry)["retired"].is_boolean())
            {
                error = "Prefab mapping retired must be a boolean";
                return false;
            }
            pair->retired = (*entry)["retired"].value_or(false);
            instance->node_mappings.push_back(std::move(pair));
        }
        if (const auto* overrides = (*table)["overrides"].as_array())
        {
            if (!read_overrides(*overrides, instance->overrides, error))
            {
                return false;
            }
        }
        else if (table->contains("overrides"))
        {
            error = "Prefab overrides must be an array";
            return false;
        }
        output.push_back(std::move(instance));
    }
    return true;
}

bool write_instances(const std::vector<std::unique_ptr<schema::PrefabInstanceDataT>>& input,
                     toml::table& output, std::string& error)
{
    if (input.empty())
    {
        return true;
    }
    toml::array instances;
    for (const auto& instance : input)
    {
        if (!instance)
        {
            error = "Invalid prefab instance";
            return false;
        }
        toml::table table{{"prefab", instance->prefab_guid},
                          {"root", std::to_string(instance->root_id)}};
        toml::array mappings, overrides;
        for (const auto& mapping : instance->node_mappings)
        {
            if (!mapping)
            {
                return false;
            }
            mappings.push_back(toml::table{{"source", std::to_string(mapping->source_id)},
                                           {"target", std::to_string(mapping->target_id)},
                                           {"retired", mapping->retired}});
        }
        if (!write_overrides(instance->overrides, overrides, error))
        {
            return false;
        }
        table.insert("node_mappings", std::move(mappings));
        table.insert("overrides", std::move(overrides));
        instances.push_back(std::move(table));
    }
    output.insert("prefab_instances", std::move(instances));
    return true;
}

bool write_document(const std::filesystem::path& destination, const toml::table& document,
                    std::string& error)
{
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
} // namespace engine::editor::prefab_detail
