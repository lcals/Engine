#include "editor/commands/prefab_commands.hpp"

#include "editor/asset_pipeline/components/scene_component_codec.hpp"
#include "editor/asset_pipeline/prefab_importer.hpp"
#include "editor/asset_pipeline/scene_importer.hpp"
#include "editor/commands/components/component_edit.hpp"
#include "editor/commands/scene_commands.hpp"
#include "runtime/resource/resource_system.hpp"
#include "runtime/scene/mesh_renderer.hpp"

#include <toml++/toml.hpp>

#include <algorithm>
#include <cctype>
#include <optional>
#include <set>

namespace engine::editor
{
namespace
{
using Json = nlohmann::json;
using Instance = schema::PrefabInstanceDataT;
using Fields = std::vector<std::string>;
using NodeIdentities = std::map<std::uint64_t, std::shared_ptr<std::uint64_t>>;

NodeIdentities retain_identities(SceneCommands& commands, const Scene& scene)
{
    NodeIdentities result;
    std::vector<Node> nodes;
    if (!scene.collect_nodes(nodes))
    {
        return {};
    }
    for (const auto& node : nodes)
    {
        result.emplace(node.info().properties->id, commands.node_identity(node.id()));
    }
    return result;
}

std::vector<schema::NodeIdMappingT> current_ids(const Scene& scene, const NodeIdentities& ids)
{
    std::vector<schema::NodeIdMappingT> result;
    for (const auto& [local, runtime] : ids)
    {
        if (const auto node = scene.find_node(*runtime))
        {
            schema::NodeIdMappingT mapping;
            mapping.source_id = local;
            mapping.target_id = node.info().properties->id;
            result.push_back(mapping);
        }
    }
    return result;
}

bool rebase_instance(std::optional<Instance>& instance, std::span<const schema::NodeIdMappingT> ids,
                     std::string& error)
{
    if (!instance)
    {
        return true;
    }
    const auto remap = [ids](std::uint64_t& id)
    {
        for (const auto& mapping : ids)
        {
            if (mapping.source_id == id)
            {
                id = mapping.target_id;
                return;
            }
        }
    };
    remap(instance->root_id);
    for (auto& mapping : instance->node_mappings)
    {
        remap(mapping->target_id);
    }
    for (auto& node : instance->overrides)
    {
        if (node->info)
        {
            remap(node->info->properties->id);
            remap(node->info->parent_id);
        }
        for (auto& component : node->components)
        {
            if (component->removed)
            {
                continue;
            }
            const auto* codec =
                asset_pipeline::find_scene_component_codec(component->component->payload.type);
            if (!codec || !codec->remap_node_references(*component->component, ids, error))
            {
                if (error.empty())
                {
                    error = "Prefab override references a stale node";
                }
                return false;
            }
        }
    }
    return true;
}

Node local_node(Scene& scene, std::uint64_t id)
{
    std::vector<Node> nodes;
    if (!scene.collect_nodes(nodes))
    {
        return {};
    }
    for (const auto& node : nodes)
    {
        if (node.info().properties->id == id)
        {
            return node;
        }
    }
    return {};
}
std::optional<Instance> instance_for(const Scene& scene, std::uint64_t local, std::uint64_t& source)
{
    source = 0;
    std::vector<Instance> instances;
    if (!scene.collect_prefab_instances(instances))
    {
        return {};
    }
    for (const auto& instance : instances)
    {
        for (const auto& mapping : instance.node_mappings)
        {
            if (mapping->target_id == local)
            {
                source = mapping->source_id;
                return instance;
            }
        }
    }
    return {};
}
schema::PrefabNodeOverrideT& node_override(Instance& instance, std::uint64_t source)
{
    for (auto& value : instance.overrides)
    {
        if (value->node_id == source)
        {
            return *value;
        }
    }
    auto value = std::make_unique<schema::PrefabNodeOverrideT>();
    value->node_id = source;
    instance.overrides.push_back(std::move(value));
    return *instance.overrides.back();
}
void merge_fields(Fields& target, const Fields& source)
{
    for (const auto& field : source)
    {
        if (std::find(target.begin(), target.end(), field) == target.end())
        {
            target.push_back(field);
        }
    }
}
bool read_fields(const Json& arguments, Fields& fields, std::string& error)
{
    if (!arguments.contains("fields") || !arguments["fields"].is_array() ||
        arguments["fields"].empty())
    {
        error = "fields must contain at least one codec property name";
        return false;
    }
    for (const auto& field : arguments["fields"])
    {
        if (!field.is_string() || field.get_ref<const std::string&>().empty() ||
            field.get_ref<const std::string&>().size() > 128 || field == "type")
        {
            error = "Invalid prefab property field";
            return false;
        }
        fields.push_back(field.get<std::string>());
    }
    return true;
}
Component component_argument(Node node, const Json& arguments, std::string& error)
{
    if (!arguments["componentType"].is_string())
    {
        error = "componentType must use the get_scene componentType name";
        return {};
    }
    std::vector<Component> components;
    if (!node.collect_components(components))
    {
        return {};
    }
    for (const auto& component : components)
    {
        if (arguments["componentType"] == schema::EnumNameComponentPayload(component.type()))
        {
            return component;
        }
    }
    error = "Prefab component does not exist";
    return {};
}
std::string property_name(std::string_view name)
{
    std::string output;
    for (unsigned char ch : name)
    {
        if (ch >= 'A' && ch <= 'Z')
        {
            output.push_back('_');
            output.push_back(static_cast<char>(ch - 'A' + 'a'));
        }
        else
        {
            output.push_back(static_cast<char>(ch));
        }
    }
    return output;
}
bool capture(Instance& instance, std::uint64_t source, Node node, ComponentType type,
             const Fields& fields, std::string& error)
{
    auto& entry = node_override(instance, source);
    if (type == ComponentType::NONE)
    {
        if (!entry.info)
        {
            entry.info = std::make_unique<schema::NodeInfoT>(node.info());
        }
        for (const auto& field : fields)
        {
            if (field == "enabled")
            {
                entry.info->properties->enabled = node.enabled();
            }
            else if (field == "visible")
            {
                entry.info->properties->visible = node.visible();
            }
            else
            {
                error = "Node prefab overrides currently support enabled and visible";
                return false;
            }
        }
        merge_fields(entry.fields, fields);
        return true;
    }
    const auto component = node.get_component(type);
    if (!component)
    {
        error = "Edited prefab component is missing";
        return false;
    }
    schema::PrefabComponentOverrideT current;
    current.component = std::make_unique<schema::ComponentDataT>(component.data());
    // Dynamic meshes are exported by ScenePersistence; ordinary renderers retain current bindings.
    if (!component.build_data(*current.component))
    {
        if (type != ComponentType::MeshRendererData)
        {
            error = "Cannot snapshot prefab component";
            return false;
        }
        *current.component = component.data();
        auto* mesh = current.component->payload.AsMeshRendererData();
        for (const auto& previous : entry.components)
        {
            if (const auto* saved = previous->component->payload.AsMeshRendererData())
            {
                mesh->mesh_guid = saved->mesh_guid;
            }
        }
        const auto renderer = node.get_component<MeshRenderer>();
        if (!ResourceSystem::path_to_guid(renderer.material().type_path(), mesh->material_guid))
        {
            error = "Cannot resolve prefab material identity";
            return false;
        }
        mesh->additional_material_guids.clear();
        for (std::size_t slot = 1; slot < renderer.material_count(); ++slot)
        {
            Guid material;
            if (!ResourceSystem::path_to_guid(renderer.material(slot).type_path(), material))
            {
                error = "Cannot resolve prefab material identity";
                return false;
            }
            mesh->additional_material_guids.push_back(std::move(material));
        }
    }
    current.fields = fields;
    auto validation = *current.component;
    if (!PrefabImporter::apply_component_override(validation, current, error))
    {
        return false;
    }
    for (auto& previous : entry.components)
    {
        if (previous->component->payload.type != type)
        {
            continue;
        }
        if (!PrefabImporter::apply_component_override(*previous->component, current, error))
        {
            return false;
        }
        merge_fields(previous->fields, fields);
        previous->removed = false;
        return true;
    }
    entry.components.push_back(
        std::make_unique<schema::PrefabComponentOverrideT>(std::move(current)));
    return true;
}
void remove_fields(Instance& instance, std::uint64_t source, ComponentType type,
                   const Fields& fields)
{
    for (auto& node : instance.overrides)
    {
        if (node->node_id != source)
        {
            continue;
        }
        auto erase = [&](Fields& value)
        {
            std::erase_if(
                value, [&](const auto& field)
                { return std::find(fields.begin(), fields.end(), field) != fields.end(); });
        };
        if (type == ComponentType::NONE)
        {
            erase(node->fields);
            if (node->fields.empty())
            {
                node->info.reset();
            }
        }
        else
        {
            for (auto& component : node->components)
            {
                if (component->component->payload.type == type)
                {
                    erase(component->fields);
                }
            }
            std::erase_if(node->components, [](const auto& value)
                          { return value->fields.empty() && !value->removed; });
        }
    }
    std::erase_if(instance.overrides, [](const auto& node)
                  { return !node->removed && node->fields.empty() && node->components.empty(); });
}

// Existing edits remain responsible for native state and rollback. This record adds only source
// metadata.
bool capture_link(Instance& instance, Scene& scene, const schema::PrefabDataT& prefab,
                  std::string& error)
{
    std::vector<schema::NodeIdMappingT> ids;
    for (const auto& mapping : instance.node_mappings)
    {
        ids.push_back(*mapping);
    }
    for (const auto& source : prefab.nodes)
    {
        const auto mapping =
            std::find_if(ids.begin(), ids.end(), [&source](const auto& value)
                         { return value.source_id == source->info->properties->id; });
        auto node = mapping != ids.end() ? local_node(scene, mapping->target_id) : Node{};
        if (!node || node.transient())
        {
            error = "Prefab association has a transient or missing node";
            return false;
        }
        auto& entry = node_override(instance, source->info->properties->id);
        entry.info = std::make_unique<schema::NodeInfoT>(node.info());
        if (node.name() != source->info->properties->name)
        {
            entry.fields.emplace_back("name");
        }
        if (node.enabled() != source->info->properties->enabled)
        {
            entry.fields.emplace_back("enabled");
        }
        if (node.visible() != source->info->properties->visible)
        {
            entry.fields.emplace_back("visible");
        }
        std::uint64_t parent = 0;
        for (const auto& id : ids)
        {
            if (id.source_id == source->info->parent_id)
            {
                parent = id.target_id;
            }
        }
        if (node.info().parent_id != parent)
        {
            entry.fields.emplace_back("parent");
        }
        for (const auto& original : source->components)
        {
            if (node.get_component(original->payload.type))
            {
                continue;
            }
            auto removed = std::make_unique<schema::PrefabComponentOverrideT>();
            removed->component = std::make_unique<schema::ComponentDataT>(*original);
            removed->removed = true;
            entry.components.push_back(std::move(removed));
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
                error = "Prefab association requires persistent components";
                return false;
            }
            const auto original = std::find_if(source->components.begin(), source->components.end(),
                                               [&component](const auto& value)
                                               { return value->payload.type == component.type(); });
            schema::ComponentDataT current;
            current.flags = std::make_unique<schema::ComponentFlagsT>();
            Fields forced;
            if (!component.build_data(current))
            {
                if (component.type() != ComponentType::MeshRendererData ||
                    original == source->components.end())
                {
                    error = "Cannot snapshot component for prefab association";
                    return false;
                }
                current = component.data();
                current.payload.AsMeshRendererData()->mesh_guid =
                    (*original)->payload.AsMeshRendererData()->mesh_guid;
                forced.emplace_back("mesh");
            }
            if (component.type() == ComponentType::MeshRendererData)
            {
                const auto renderer = node.get_component<MeshRenderer>();
                auto* mesh = current.payload.AsMeshRendererData();
                mesh->additional_material_guids.clear();
                for (std::size_t slot = 0; slot < renderer.material_count(); ++slot)
                {
                    Guid material;
                    if (!ResourceSystem::path_to_guid(renderer.material(slot).type_path(),
                                                      material))
                    {
                        error = "Cannot resolve associated material";
                        return false;
                    }
                    if (slot == 0)
                    {
                        mesh->material_guid = material;
                    }
                    else
                    {
                        mesh->additional_material_guids.push_back(material);
                    }
                    if (renderer.material(slot).has_overrides())
                    {
                        merge_fields(forced, slot == 0 ? Fields{"material"}
                                                       : Fields{"additional_materials"});
                    }
                }
            }
            toml::table values, baseline;
            if (!SceneImporter::write_component(current, values, error))
            {
                return false;
            }
            if (original != source->components.end())
            {
                auto data = **original;
                if (!component.remap_node_references(data, ids) ||
                    !SceneImporter::write_component(data, baseline, error))
                {
                    return false;
                }
            }
            std::set<std::string> keys;
            for (const auto& [key, value] : values)
            {
                keys.emplace(key.str());
            }
            for (const auto& [key, value] : baseline)
            {
                keys.emplace(key.str());
            }
            for (const auto& key : keys)
            {
                if (key == "type")
                {
                    continue;
                }
                toml::table left, right;
                if (const auto* value = values.get(key))
                {
                    left.insert("value", *value);
                }
                if (const auto* value = baseline.get(key))
                {
                    right.insert("value", *value);
                }
                if (left != right)
                {
                    merge_fields(forced, {key});
                }
            }
            if (!forced.empty())
            {
                auto value = std::make_unique<schema::PrefabComponentOverrideT>();
                value->component = std::make_unique<schema::ComponentDataT>(std::move(current));
                value->fields = std::move(forced);
                entry.components.push_back(std::move(value));
            }
        }
    }
    std::erase_if(instance.overrides, [](const auto& value)
                  { return value->fields.empty() && value->components.empty(); });
    return true;
}

class PrefabTrackedEdit final : public EditorEdit
{
public:
    PrefabTrackedEdit(WorldSystem& world, SceneCommands& scenes, Node node, ComponentType type,
                      Fields fields, std::unique_ptr<EditorEdit> edit)
        : world_(world), node_(scenes.node_identity(node.id())), scene_(node.scene()->id()),
          type_(type), fields_(std::move(fields)), edit_(std::move(edit))
    {
    }
    bool apply(std::string& error) override
    {
        auto node = resolve(error);
        std::uint64_t source = 0;
        const auto current =
            node ? instance_for(*node.scene(), node.info().properties->id, source) : std::nullopt;
        if (!current)
        {
            error = "Prefab association changed before editing";
            return false;
        }
        Instance before(*current);
        if (!edit_->apply(error))
        {
            return false;
        }
        Instance after(before);
        if (!capture(after, source, node, type_, fields_, error) ||
            !node.scene()->set_prefab_instance(after))
        {
            std::string rollback;
            if (!edit_->revert(rollback))
            {
                error += "; rollback failed: " + rollback;
            }
            return false;
        }
        before_ = std::move(before);
        after_ = std::move(after);
        return true;
    }
    bool revert(std::string& error) override
    {
        auto node = resolve(error);
        std::uint64_t source = 0;
        const auto current =
            node ? instance_for(*node.scene(), node.info().properties->id, source) : std::nullopt;
        if (!current || *current != after_)
        {
            error = "Prefab overrides changed outside this edit";
            return false;
        }
        if (!edit_->revert(error))
        {
            return false;
        }
        if (!node.scene()->set_prefab_instance(before_))
        {
            std::string rollback;
            if (!edit_->apply(rollback))
            {
                error += "; rollback failed: " + rollback;
            }
            error = "Cannot restore prefab override metadata; " + error;
            return false;
        }
        return true;
    }

private:
    Node resolve(std::string& error) const
    {
        auto* scene = world_.scene();
        auto node = scene && scene->id() == scene_ ? scene->find_node(*node_) : Node{};
        if (!node)
        {
            error = "Prefab edit targets a stale scene or node";
        }
        return node;
    }
    WorldSystem& world_;
    std::shared_ptr<std::uint64_t> node_;
    std::uint64_t scene_;
    ComponentType type_;
    Fields fields_;
    std::unique_ptr<EditorEdit> edit_;
    Instance before_, after_;
};

class PrefabMetadataEdit final : public EditorEdit
{
public:
    PrefabMetadataEdit(WorldSystem& world, SceneCommands& scenes, Node node,
                       std::optional<Instance> before, std::optional<Instance> after,
                       std::unique_ptr<EditorEdit> edit = {})
        : world_(world), node_(scenes.node_identity(node.id())), scene_(node.scene()->id()),
          before_(std::move(before)), after_(std::move(after)), edit_(std::move(edit)),
          identities_(retain_identities(scenes, *node.scene()))
    {
    }
    bool apply(std::string& error) override
    {
        return change(before_, after_, true, error);
    }
    bool revert(std::string& error) override
    {
        return change(after_, before_, false, error);
    }

private:
    bool change(const std::optional<Instance>& expected, const std::optional<Instance>& replacement,
                bool apply, std::string& error)
    {
        auto* scene = world_.scene();
        auto node = scene && scene->id() == scene_ ? scene->find_node(*node_) : Node{};
        if (!node)
        {
            error = "Prefab source metadata targets a stale node";
            return false;
        }
        auto current_expected = expected;
        auto current_replacement = replacement;
        const auto ids = current_ids(*scene, identities_);
        if (!rebase_instance(current_expected, ids, error) ||
            !rebase_instance(current_replacement, ids, error))
        {
            return false;
        }
        std::uint64_t source = 0;
        const auto current = instance_for(*scene, node.info().properties->id, source);
        if (bool(current) != bool(current_expected) || (current && *current != *current_expected))
        {
            error = "Prefab source metadata changed outside this edit";
            return false;
        }
        if (edit_ && !(apply ? edit_->apply(error) : edit_->revert(error)))
        {
            return false;
        }
        const bool changed = current_replacement
                                 ? scene->set_prefab_instance(*current_replacement)
                                 : scene->remove_prefab_instance(current_expected->root_id);
        if (!changed)
        {
            std::string rollback;
            if (edit_ && !(apply ? edit_->revert(rollback) : edit_->apply(rollback)))
            {
                error += "; " + rollback;
            }
            error = "Cannot change prefab association; " + error;
            return false;
        }
        return true;
    }
    WorldSystem& world_;
    std::shared_ptr<std::uint64_t> node_;
    std::uint64_t scene_;
    std::optional<Instance> before_, after_;
    std::unique_ptr<EditorEdit> edit_;
    NodeIdentities identities_;
};

class PrefabComponentRestoreEdit final : public EditorEdit
{
public:
    PrefabComponentRestoreEdit(WorldSystem& world, SceneCommands& scenes, Node node,
                               schema::ComponentDataT before, schema::ComponentDataT after,
                               Fields fields)
        : world_(world), node_(scenes.node_identity(node.id())), scene_(node.scene()->id()),
          before_(std::move(before)), after_(std::move(after)),
          identities_(retain_identities(scenes, *node.scene())), fields_(std::move(fields))
    {
        if (before_.payload.type == ComponentType::MeshRendererData)
        {
            before_materials_ = materials(node.get_component<MeshRenderer>());
        }
    }
    bool apply(std::string& error) override
    {
        return change(before_, after_, true, error);
    }
    bool revert(std::string& error) override
    {
        return change(after_, before_, false, error);
    }

private:
    static std::vector<Json> materials(const MeshRenderer& renderer)
    {
        std::vector<Json> result;
        for (std::size_t slot = 0; slot < renderer.material_count(); ++slot)
        {
            result.push_back(components::material_snapshot(renderer.material(slot)));
        }
        return result;
    }
    static bool restore_materials(MeshRenderer renderer, const std::vector<Json>& snapshots)
    {
        if (renderer.material_count() != snapshots.size())
        {
            return false;
        }
        for (std::size_t slot = 0; slot < snapshots.size(); ++slot)
        {
            if (!components::restore_material_snapshot(renderer.material(slot), snapshots[slot]))
            {
                return false;
            }
        }
        return true;
    }
    bool change(schema::ComponentDataT expected, schema::ComponentDataT replacement, bool forward,
                std::string& error)
    {
        auto* scene = world_.scene();
        if (!scene || scene->id() != scene_)
        {
            error = "Prefab restore targets a stale scene";
            return false;
        }
        const auto* codec = asset_pipeline::find_scene_component_codec(expected.payload.type);
        const auto ids = current_ids(*scene, identities_);
        if (!codec || !codec->remap_node_references(expected, ids, error) ||
            !codec->remap_node_references(replacement, ids, error))
        {
            return false;
        }
        auto node = scene->find_node(*node_);
        auto renderer = node && expected.payload.type == ComponentType::MeshRendererData
                            ? node.get_component<MeshRenderer>()
                            : MeshRenderer{};
        const auto& expected_materials = forward ? before_materials_ : after_materials_;
        if (renderer && materials(renderer) != expected_materials)
        {
            error = "Prefab material values changed outside this restore";
            return false;
        }
        const auto original = expected;
        components::ComponentEdit edit(world_, scene_, node_, std::move(expected),
                                       std::move(replacement));
        if (!edit.apply(error))
        {
            return false;
        }
        if (!renderer)
        {
            return true;
        }
        auto wanted = forward ? after_materials_ : before_materials_;
        if (forward && wanted.empty())
        {
            wanted = materials(renderer);
            for (std::size_t slot = 0; slot < wanted.size(); ++slot)
            {
                const auto field = slot == 0 ? "material" : "additional_materials";
                if (slot < before_materials_.size() &&
                    std::find(fields_.begin(), fields_.end(), field) == fields_.end())
                {
                    wanted[slot] = before_materials_[slot];
                }
            }
        }
        if (!restore_materials(renderer, wanted))
        {
            std::string rollback;
            const bool restored =
                renderer.restore_data(original) && restore_materials(renderer, expected_materials);
            error = restored ? "Cannot restore prefab material values"
                             : "Cannot roll back prefab material restore: " + rollback;
            return false;
        }
        if (forward)
        {
            after_materials_ = std::move(wanted);
        }
        return true;
    }
    WorldSystem& world_;
    std::shared_ptr<std::uint64_t> node_;
    std::uint64_t scene_;
    schema::ComponentDataT before_, after_;
    NodeIdentities identities_;
    Fields fields_;
    std::vector<Json> before_materials_, after_materials_;
};
class PrefabNodeFlagsEdit final : public EditorEdit
{
public:
    PrefabNodeFlagsEdit(WorldSystem& world, std::shared_ptr<std::uint64_t> node,
                        std::uint64_t scene, schema::NodeInfoT before, schema::NodeInfoT after)
        : world_(world), node_(std::move(node)), scene_(scene), before_(std::move(before)),
          after_(std::move(after))
    {
    }
    bool apply(std::string& error) override
    {
        return change(before_, after_, error);
    }
    bool revert(std::string& error) override
    {
        return change(after_, before_, error);
    }

private:
    bool change(const schema::NodeInfoT& expected, const schema::NodeInfoT& value,
                std::string& error)
    {
        auto* scene = world_.scene();
        auto node = scene && scene->id() == scene_ ? scene->find_node(*node_) : Node{};
        if (!node || node.enabled() != expected.properties->enabled ||
            node.visible() != expected.properties->visible)
        {
            error = "Prefab node flags changed outside this edit";
            return false;
        }
        return node.set_enabled(value.properties->enabled) &&
               node.set_visible(value.properties->visible);
    }
    WorldSystem& world_;
    std::shared_ptr<std::uint64_t> node_;
    std::uint64_t scene_;
    schema::NodeInfoT before_, after_;
};

bool override_command(WorldSystem& world, SceneCommands& scenes, const Json& arguments,
                      Json& result, std::unique_ptr<EditorEdit>& edit, std::string& error,
                      bool revert)
{
    if (!components::fields(arguments, {"componentType", "fields"}, error))
    {
        return false;
    }
    auto node = components::target(world, arguments, error);
    Fields fields;
    if (!node || !read_fields(arguments, fields, error))
    {
        return false;
    }
    std::uint64_t source = 0;
    const auto current = instance_for(*node.scene(), node.info().properties->id, source);
    if (!current)
    {
        error = "Node is not a prefab source member";
        return false;
    }
    Component component = {};
    if (arguments.contains("componentType"))
    {
        component = component_argument(node, arguments, error);
        if (!component)
        {
            return false;
        }
    }
    const auto type = component ? component.type() : ComponentType::NONE;
    Instance after(*current);
    std::unique_ptr<EditorEdit> property;
    if (revert)
    {
        ResourcePtr resource;
        auto* resources = ResourceSystem::instance();
        if (!resources || !resources->loader().load(current->prefab_guid, resource) || !resource ||
            !resource->prefab())
        {
            error = "Cannot load the prefab baseline";
            return false;
        }
        const schema::NodeDataT* baseline = nullptr;
        for (const auto& candidate : resource->prefab()->nodes)
        {
            if (candidate->info->properties->id == source)
            {
                baseline = candidate.get();
            }
        }
        if (!baseline)
        {
            error = "Prefab baseline node no longer exists";
            return false;
        }
        if (component)
        {
            const schema::ComponentDataT* original = nullptr;
            for (const auto& candidate : baseline->components)
            {
                if (candidate->payload.type == type)
                {
                    original = candidate.get();
                }
            }
            if (!original)
            {
                error = "Prefab has no original component for these fields";
                return false;
            }
            schema::PrefabComponentOverrideT restore;
            restore.component = std::make_unique<schema::ComponentDataT>(*original);
            restore.fields = fields;
            std::vector<schema::NodeIdMappingT> mapping;
            for (const auto& value : current->node_mappings)
            {
                mapping.push_back(*value);
            }
            if (!component.remap_node_references(*restore.component, mapping))
            {
                error = "Prefab baseline references a missing node";
                return false;
            }
            schema::ComponentDataT before;
            before.flags = std::make_unique<schema::ComponentFlagsT>();
            if (!component.build_data(before))
            {
                error = "Cannot snapshot component before reverting";
                return false;
            }
            auto replacement = before;
            if (!PrefabImporter::apply_component_override(replacement, restore, error))
            {
                return false;
            }
            property = std::make_unique<PrefabComponentRestoreEdit>(
                world, scenes, node, std::move(before), std::move(replacement), fields);
        }
        else
        {
            auto replacement = node.info();
            for (const auto& field : fields)
            {
                if (field == "enabled")
                {
                    replacement.properties->enabled = baseline->info->properties->enabled;
                }
                else if (field == "visible")
                {
                    replacement.properties->visible = baseline->info->properties->visible;
                }
                else
                {
                    error = "Node restore supports enabled and visible";
                    return false;
                }
            }
            property =
                std::make_unique<PrefabNodeFlagsEdit>(world, scenes.node_identity(node.id()),
                                                      node.scene()->id(), node.info(), replacement);
        }
        remove_fields(after, source, type, fields);
    }
    else if (!capture(after, source, node, type, fields, error))
    {
        return false;
    }
    edit = std::make_unique<PrefabMetadataEdit>(world, scenes, node, *current, std::move(after),
                                                std::move(property));
    result = {{"nodeId", arguments["nodeId"]},
              {"sourceNodeId", std::to_string(source)},
              {"fields", fields},
              {"restored", revert}};
    return true;
}

bool wrap_edit(WorldSystem& world, SceneCommands& scenes, std::string_view command,
               const Json& arguments, std::unique_ptr<EditorEdit>& edit, std::string& error)
{
    auto* scene = world.scene();
    std::vector<Instance> instances;
    if (!scene || !scene->collect_prefab_instances(instances) || instances.empty() ||
        command == "instantiate_prefab" || command == "set_prefab_override" ||
        command == "revert_prefab_override" || command == "link_prefab" ||
        command == "unlink_prefab" || command == "create_node" || command == "instantiate_model" ||
        command == "set_active_camera")
    {
        return true;
    }
    if (!arguments.contains("nodeId"))
    {
        if (!edit->affects_scene())
        {
            return true;
        }
        error = "This edit cannot target a prefab implicitly; use an explicit node command";
        return false;
    }
    auto node = components::target(world, arguments, error);
    if (!node)
    {
        return false;
    }
    std::uint64_t source = 0;
    if (!instance_for(*scene, node.info().properties->id, source))
    {
        return true;
    }
    Fields fields;
    ComponentType type = ComponentType::NONE;
    if (command == "set_node_state")
    {
        for (const auto* field : {"enabled", "visible"})
        {
            if (arguments.contains(field))
            {
                fields.emplace_back(field);
            }
        }
    }
    else if (command == "set_component_state")
    {
        auto component = component_argument(node, arguments, error);
        if (!component)
        {
            return false;
        }
        type = component.type();
        for (const auto* field : {"enabled", "visible"})
        {
            if (arguments.contains(field))
            {
                fields.emplace_back(field);
            }
        }
    }
    else if (command.starts_with("set_material_"))
    {
        type = ComponentType::MeshRendererData;
        fields =
            arguments.value("slot", 0) == 0 ? Fields{"material"} : Fields{"additional_materials"};
    }
    else if (command.starts_with("set_"))
    {
        const auto name = command == "set_ui_document"     ? "ui"
                          : command == "set_mesh_lighting" ? "mesh_renderer"
                                                           : command.substr(4);
        const auto* codec = asset_pipeline::find_scene_component_codec(name);
        if (!codec)
        {
            error = "This prefab edit has no registered component codec";
            return false;
        }
        type = codec->type();
        for (const auto& argument : arguments.items())
        {
            if (argument.key() == "sceneId" || argument.key() == "nodeId")
            {
                continue;
            }
            if (name == "light_probe_volume" && argument.key() == "resource")
            {
                fields.emplace_back("probes");
            }
            else
            {
                fields.push_back(property_name(argument.key()));
            }
        }
        if (name == "mesh_renderer" && arguments.contains("lightmap"))
        {
            merge_fields(fields, {"lightmap_mesh_hash", "lightmap_transform"});
        }
    }
    else
    {
        error = "This structural edit is not supported on prefab members; unlink the prefab first";
        return false;
    }
    if (fields.empty())
    {
        error = "Prefab edit did not identify explicit fields";
        return false;
    }
    edit = std::make_unique<PrefabTrackedEdit>(world, scenes, node, type, std::move(fields),
                                               std::move(edit));
    return true;
}
} // namespace

bool register_prefab_commands(WorldSystem& world, SceneCommands& scenes,
                              std::map<std::uint64_t, std::shared_ptr<std::uint64_t>>& aliases,
                              EditorCommands& commands)
{
    if (!commands.register_command(
            "instantiate_prefab", true,
            [&world, &scenes, &aliases](const Json& arguments, Json& result,
                                        std::unique_ptr<EditorEdit>& edit, std::string& error)
            {
                (void)scenes.node_identity(0);
                return components::prepare_prefab(world, aliases, arguments, result, edit, error);
            }))
    {
        return false;
    }
    for (bool revert : {false, true})
    {
        if (!commands.register_command(
                revert ? "revert_prefab_override" : "set_prefab_override", true,
                [&world, &scenes, revert](const Json& arguments, Json& result,
                                          std::unique_ptr<EditorEdit>& edit, std::string& error)
                {
                    return override_command(world, scenes, arguments, result, edit, error, revert);
                }))
        {
            return false;
        }
    }
    if (!commands.register_command(
            "get_prefab_instance", false,
            [&world](const Json& arguments, Json& result, std::unique_ptr<EditorEdit>&,
                     std::string& error)
            {
                if (!components::fields(arguments, {}, error))
                {
                    return false;
                }
                const auto node = components::target(world, arguments, error);
                if (!node)
                {
                    return false;
                }
                std::uint64_t source = 0;
                const auto instance =
                    instance_for(*node.scene(), node.info().properties->id, source);
                if (!instance)
                {
                    result = {{"prefab", nullptr}};
                    return true;
                }
                std::string path;
                (void)ResourceSystem::guid_to_path(instance->prefab_guid, path);
                Json overrides = Json::array();
                for (const auto& value : instance->overrides)
                {
                    Json components = Json::array();
                    for (const auto& component : value->components)
                    {
                        components.push_back(
                            {{"componentType",
                              schema::EnumNameComponentPayload(component->component->payload.type)},
                             {"fields", component->fields},
                             {"removed", component->removed}});
                    }
                    overrides.push_back({{"sourceNodeId", std::to_string(value->node_id)},
                                         {"fields", value->fields},
                                         {"removed", value->removed},
                                         {"components", std::move(components)}});
                }
                result = {{"prefab", path},
                          {"prefabGuid", instance->prefab_guid},
                          {"rootLocalId", std::to_string(instance->root_id)},
                          {"sourceNodeId", std::to_string(source)},
                          {"overrides", std::move(overrides)}};
                return true;
            }))
    {
        return false;
    }
    if (!commands.register_command(
            "unlink_prefab", true,
            [&world, &scenes](const Json& arguments, Json& result,
                              std::unique_ptr<EditorEdit>& edit, std::string& error)
            {
                if (!components::fields(arguments, {}, error))
                {
                    return false;
                }
                auto node = components::target(world, arguments, error);
                if (!node)
                {
                    return false;
                }
                std::uint64_t source = 0;
                const auto instance =
                    instance_for(*node.scene(), node.info().properties->id, source);
                if (!instance || instance->root_id != node.info().properties->id)
                {
                    error = "unlink_prefab requires the instance root";
                    return false;
                }
                edit = std::make_unique<PrefabMetadataEdit>(world, scenes, node, *instance,
                                                            std::nullopt);
                result = {{"nodeId", arguments["nodeId"]}, {"linked", false}};
                return true;
            }))
    {
        return false;
    }
    if (!commands.register_command(
            "link_prefab", true,
            [&world, &scenes](const Json& arguments, Json& result,
                              std::unique_ptr<EditorEdit>& edit, std::string& error)
            {
                if (!components::fields(arguments, {"path"}, error) ||
                    !arguments.contains("path") || !arguments["path"].is_string())
                {
                    return false;
                }
                auto node = components::target(world, arguments, error);
                if (!node)
                {
                    return false;
                }
                auto* resources = ResourceSystem::instance();
                Guid guid;
                ResourcePtr resource;
                if (!resources ||
                    !ResourceSystem::path_to_guid(arguments["path"].get<std::string>(), guid) ||
                    !resources->loader().load(guid, resource) || !resource || !resource->prefab())
                {
                    error = "Cannot load prefab source for association";
                    return false;
                }
                std::uint64_t source = 0;
                if (instance_for(*node.scene(), node.info().properties->id, source))
                {
                    error = "Node already belongs to a prefab";
                    return false;
                }
                Instance instance;
                instance.prefab_guid = guid;
                instance.root_id = node.info().properties->id;
                for (const auto& member : resource->prefab()->nodes)
                {
                    auto target = member->info->properties->id == resource->prefab()->root_id
                                      ? node
                                      : local_node(*node.scene(), member->info->properties->id);
                    bool descendant = false;
                    for (auto parent = target; parent; parent = parent.parent())
                    {
                        if (parent == node)
                        {
                            descendant = true;
                        }
                    }
                    if (!target || !descendant)
                    {
                        error = "link_prefab requires the source subtree's stable local node IDs";
                        return false;
                    }
                    auto mapping = std::make_unique<schema::NodeIdMappingT>();
                    mapping->source_id = member->info->properties->id;
                    mapping->target_id = target.info().properties->id;
                    instance.node_mappings.push_back(std::move(mapping));
                }
                if (!capture_link(instance, *node.scene(), *resource->prefab(), error))
                {
                    return false;
                }
                edit = std::make_unique<PrefabMetadataEdit>(world, scenes, node, std::nullopt,
                                                            std::move(instance));
                result = {{"nodeId", arguments["nodeId"]}, {"linked", true}, {"prefabGuid", guid}};
                return true;
            }))
    {
        return false;
    }
    return commands.set_edit_wrapper(
        [&world, &scenes](std::string_view command, const Json& arguments,
                          std::unique_ptr<EditorEdit>& edit, std::string& error)
        { return wrap_edit(world, scenes, command, arguments, edit, error); });
}
} // namespace engine::editor
