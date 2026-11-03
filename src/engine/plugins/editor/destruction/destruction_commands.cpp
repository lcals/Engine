#include "plugins/editor/destruction/destruction_commands.hpp"

#include "editor/commands/components/component_edit.hpp"
#include "editor/commands/scene_commands.hpp"
#include "plugins/editor/destruction/destruction_files.hpp"
#include "plugins/editor/destruction/destruction_settings_edit.hpp"
#include "plugins/editor/destruction/destruction_state_edit.hpp"
#include "plugins/runtime/destruction/destruction_component.hpp"
#include "runtime/resource/resource_system.hpp"
#include "runtime/scene/collider.hpp"

#include <iterator>
#include <map>
#include <memory>
#include <utility>

namespace engine::plugins::destruction
{
namespace
{
using Json = nlohmann::json;
using RevisionMap = std::map<std::pair<std::uint64_t, std::uint64_t>, std::weak_ptr<std::uint64_t>>;

std::shared_ptr<std::uint64_t> edit_revision(RevisionMap& revisions, const Node& node,
                                             const DestructionComponent* component)
{
    // A new external mutation starts a new history chain; older chains remain invalidated.
    for (auto entry = revisions.begin(); entry != revisions.end();)
    {
        entry = entry->second.expired() ? revisions.erase(entry) : std::next(entry);
    }
    auto& entry = revisions[{node.scene().id(), node.id()}];
    auto value = entry.lock();
    const auto current = component ? component->revision() : 0;
    if (!value || *value != current)
    {
        value = std::make_shared<std::uint64_t>(current);
        entry = value;
    }
    return value;
}

bool resource(const Json& input, schema::AssetKind kind, Guid& output, std::string& error)
{
    if (!input.is_string())
    {
        error = "Destruction resource and material require resource path strings";
        return false;
    }
    const auto& path = input.get_ref<const std::string&>();
    if (path.empty())
    {
        output.clear();
        return true;
    }
    const auto* resources = ResourceSystem::instance();
    Guid guid;
    const auto* entry = resources && ResourceSystem::path_to_guid(path, guid)
                            ? resources->catalog().find(guid)
                            : nullptr;
    if (!entry || entry->kind != kind)
    {
        error = "Destruction resource path is missing or has the wrong resource type";
        return false;
    }
    output = std::move(guid);
    return true;
}

bool read_settings(const Json& arguments, schema::DestructionDataT& value, std::string& error)
{
    if (!editor::components::fields(
            arguments, {"enabled", "destruction", "material", "density", "friction", "restitution"},
            error) ||
        (arguments.contains("destruction") &&
         !resource(arguments["destruction"], schema::AssetKind::destruction, value.destruction_guid,
                   error)) ||
        (arguments.contains("material") &&
         !resource(arguments["material"], schema::AssetKind::material, value.material_guid, error)))
    {
        return false;
    }
    for (const auto& [name, destination] :
         {std::pair{"density", &value.density}, std::pair{"friction", &value.friction},
          std::pair{"restitution", &value.restitution}})
    {
        if (arguments.contains(name) && !editor::components::number(arguments[name], *destination))
        {
            error = std::string("Destruction ") + name + " must be a finite float";
            return false;
        }
    }
    if (!DestructionComponent::valid_settings(value))
    {
        error = "Destruction requires valid GUIDs, density in (0,100000], friction and restitution "
                "in [0,1]";
        return false;
    }
    return true;
}

Json settings_json(const schema::DestructionDataT& value)
{
    std::string path;
    std::string material;
    if (!value.destruction_guid.empty())
    {
        static_cast<void>(ResourceSystem::guid_to_path(value.destruction_guid, path));
    }
    if (!value.material_guid.empty())
    {
        static_cast<void>(ResourceSystem::guid_to_path(value.material_guid, material));
    }
    return {{"destruction", path},
            {"destructionGuid", value.destruction_guid},
            {"material", material},
            {"materialGuid", value.material_guid},
            {"density", value.density},
            {"friction", value.friction},
            {"restitution", value.restitution}};
}

Json component_json(const DestructionComponent& component)
{
    auto result = settings_json(component.settings());
    result["enabled"] = component.enabled();
    result["ready"] = component.ready();
    result["revision"] = component.revision();
    result["chunks"] = component.chunk_count();
    result["groups"] = component.group_count();
    result["dynamicGroups"] = component.dynamic_groups();
    result["lastError"] = component.last_error();
    result["sceneId"] = std::to_string(component.node().scene().id());
    result["nodeId"] = std::to_string(component.node().id());
    return result;
}

DestructionComponent* target(WorldSystem& world, const Json& arguments, std::string& error)
{
    auto* node = editor::components::target(world, arguments, error);
    auto* component = node ? node->get_component<DestructionComponent>() : nullptr;
    if (!component && error.empty())
    {
        error = "Node has no destruction component";
    }
    return component;
}

} // namespace

bool register_destruction_commands(WorldSystem& world, editor::SceneCommands& scene_commands,
                                   editor::EditorCommands& commands,
                                   const std::filesystem::path& project_root)
{
    auto revisions = std::make_shared<RevisionMap>();
    if (!register_destruction_asset_commands(commands, project_root) ||
        !commands.register_command(
            "set_destruction", true,
            [&world, &scene_commands, revisions](const Json& arguments, Json& result,
                                                 std::unique_ptr<editor::EditorEdit>& edit,
                                                 std::string& error)
            {
                auto* node = editor::components::target(world, arguments, error);
                if (!node)
                {
                    return false;
                }
                const auto* component = node->get_component<DestructionComponent>();
                std::optional<schema::ComponentDataT> before;
                ByteBuffer before_state;
                if (component)
                {
                    before.emplace();
                    if (!component->build_data(*before) ||
                        (component->ready() && !component->snapshot(before_state)))
                    {
                        error = "Cannot snapshot destruction settings and state";
                        return false;
                    }
                }
                auto value = component ? component->settings() : schema::DestructionDataT{};
                if (!read_settings(arguments, value, error))
                {
                    return false;
                }
                schema::ComponentDataT after;
                after.enabled = !component || component->enabled();
                after.visible = component == nullptr || component->visible();
                if (arguments.contains("enabled"))
                {
                    if (!arguments["enabled"].is_boolean())
                    {
                        error = "Destruction enabled must be boolean";
                        return false;
                    }
                    after.enabled = arguments["enabled"].get<bool>();
                }
                auto identity = scene_commands.node_identity(node->id());
                if (!identity)
                {
                    error = "Cannot resolve stable destruction node identity";
                    return false;
                }
                result = settings_json(value);
                result["enabled"] = after.enabled;
                result["sceneId"] = std::to_string(node->scene().id());
                result["nodeId"] = std::to_string(node->id());
                after.payload.Set(std::move(value));
                edit = std::make_unique<DestructionSettingsEdit>(
                    world, node->scene().id(), std::move(identity),
                    edit_revision(*revisions, *node, component), std::move(before),
                    std::move(after), std::move(before_state));
                return true;
            }) ||
        !commands.register_command("get_destruction", false,
                                   [&world](const Json& arguments, Json& result,
                                            std::unique_ptr<editor::EditorEdit>&,
                                            std::string& error)
                                   {
                                       if (!editor::components::fields(arguments, {}, error))
                                       {
                                           return false;
                                       }
                                       const auto* component = target(world, arguments, error);
                                       if (!component)
                                       {
                                           return false;
                                       }
                                       result = component_json(*component);
                                       return true;
                                   }))
    {
        return false;
    }
    if (!commands.register_command(
            "destruction_refresh_support", true,
            [&world, &scene_commands, revisions](const Json& arguments, Json& result,
                                                 std::unique_ptr<editor::EditorEdit>& edit,
                                                 std::string& error)
            {
                if (!editor::components::fields(arguments,
                                                {"supportNodeId", "maxGap", "minCoverage"}, error))
                {
                    return false;
                }
                auto* component = target(world, arguments, error);
                float max_gap = 0;
                float min_coverage = 0;
                if (!component || !component->ready() || !arguments.contains("supportNodeId") ||
                    !arguments.contains("maxGap") || !arguments.contains("minCoverage") ||
                    !editor::components::number(arguments["maxGap"], max_gap) || max_gap <= 0 ||
                    max_gap > 1000 ||
                    !editor::components::number(arguments["minCoverage"], min_coverage) ||
                    min_coverage <= 0 || min_coverage > 1)
                {
                    if (error.empty())
                    {
                        error = "Support refresh requires a ready destruction object, "
                                "supportNodeId, maxGap in (0,1000], minCoverage in (0,1]";
                    }
                    return false;
                }
                Json support_target = {{"sceneId", arguments["sceneId"]},
                                       {"nodeId", arguments["supportNodeId"]}};
                auto* support = editor::components::target(world, support_target, error);
                if (!support || !support->get_component<Collider>())
                {
                    if (error.empty())
                    {
                        error = "Support node has no collider";
                    }
                    return false;
                }
                auto identity = scene_commands.node_identity(component->node().id());
                auto support_identity = scene_commands.node_identity(support->id());
                ByteBuffer before;
                schema::ComponentDataT settings;
                if (!identity || !support_identity || !component->snapshot(before) ||
                    !component->build_data(settings))
                {
                    error = "Cannot snapshot foundation support state";
                    return false;
                }
                result = {{"sceneId", std::to_string(component->node().scene().id())},
                          {"nodeId", std::to_string(component->node().id())},
                          {"supportNodeId", std::to_string(support->id())},
                          {"operation", "refresh_support"},
                          {"undoable", true}};
                edit = std::make_unique<DestructionStateEdit>(
                    world, component->node().scene().id(), std::move(identity),
                    edit_revision(*revisions, component->node(), component), std::move(settings),
                    std::move(before), std::move(support_identity), max_gap, min_coverage);
                return true;
            }))
    {
        return false;
    }
    if (!commands.register_command(
            "destruction_apply_stress", true,
            [&world, &scene_commands, revisions](const Json& arguments, Json& result,
                                                 std::unique_ptr<editor::EditorEdit>& edit,
                                                 std::string& error)
            {
                if (!editor::components::fields(
                        arguments, {"gravity", "elasticLimit", "fatalLimit", "iterations"}, error))
                {
                    return false;
                }
                auto* component = target(world, arguments, error);
                Vec3 gravity;
                float elastic_limit = 0;
                float fatal_limit = 0;
                if (!component || !component->ready() || !arguments.contains("gravity") ||
                    !editor::components::vector(arguments["gravity"], gravity) ||
                    !arguments.contains("elasticLimit") ||
                    !editor::components::number(arguments["elasticLimit"], elastic_limit) ||
                    elastic_limit <= 0 || !arguments.contains("fatalLimit") ||
                    !editor::components::number(arguments["fatalLimit"], fatal_limit) ||
                    fatal_limit < elastic_limit || !arguments.contains("iterations") ||
                    !arguments["iterations"].is_number_integer() ||
                    arguments["iterations"] < 1 || arguments["iterations"] > 1000)
                {
                    if (error.empty())
                    {
                        error = "Stress requires a ready destruction object, finite gravity, "
                                "0 < elasticLimit <= fatalLimit and integer iterations in [1,1000]";
                    }
                    return false;
                }
                auto identity = scene_commands.node_identity(component->node().id());
                ByteBuffer before;
                schema::ComponentDataT settings;
                if (!identity || !component->snapshot(before) || !component->build_data(settings))
                {
                    error = "Cannot snapshot destruction stress state";
                    return false;
                }
                result = {{"sceneId", std::to_string(component->node().scene().id())},
                          {"nodeId", std::to_string(component->node().id())},
                          {"operation", "apply_stress"},
                          {"undoable", true}};
                edit = std::make_unique<DestructionStateEdit>(
                    world, component->node().scene().id(), std::move(identity),
                    edit_revision(*revisions, component->node(), component), std::move(settings),
                    std::move(before), gravity, elastic_limit, fatal_limit,
                    arguments["iterations"].get<std::uint32_t>());
                return true;
            }))
    {
        return false;
    }
    for (const bool reset : {false, true})
    {
        if (!commands.register_command(
                reset ? "destruction_reset" : "destruction_damage", true,
                [&world, &scene_commands, revisions,
                 reset](const Json& arguments, Json& result,
                        std::unique_ptr<editor::EditorEdit>& edit, std::string& error)
                {
                    if (!(reset ? editor::components::fields(arguments, {}, error)
                                : editor::components::fields(
                                      arguments, {"center", "radius", "damage", "impulse"}, error)))
                    {
                        return false;
                    }
                    auto* component = target(world, arguments, error);
                    ByteBuffer before;
                    schema::ComponentDataT settings;
                    if (!component || !component->ready() || !component->snapshot(before) ||
                        !component->build_data(settings))
                    {
                        if (error.empty())
                        {
                            error = "Destruction must be ready before editing its state";
                        }
                        return false;
                    }
                    Vec3 center;
                    float radius = 0;
                    float damage = 0;
                    float impulse = 0;
                    if (!reset &&
                        (!arguments.contains("center") ||
                         !editor::components::vector(arguments["center"], center) ||
                         !arguments.contains("radius") ||
                         !editor::components::number(arguments["radius"], radius) || radius <= 0 ||
                         !arguments.contains("damage") ||
                         !editor::components::number(arguments["damage"], damage) || damage < 0 ||
                         (arguments.contains("impulse") &&
                          !editor::components::number(arguments["impulse"], impulse)) ||
                         impulse < 0))
                    {
                        error = "Destruction damage requires finite world center, positive radius, "
                                "nonnegative damage and impulse";
                        return false;
                    }
                    auto identity = scene_commands.node_identity(component->node().id());
                    if (!identity)
                    {
                        error = "Cannot resolve stable destruction node identity";
                        return false;
                    }
                    result = {{"sceneId", std::to_string(component->node().scene().id())},
                              {"nodeId", std::to_string(component->node().id())},
                              {"operation", reset ? "reset" : "damage"},
                              {"undoable", true}};
                    edit = std::make_unique<DestructionStateEdit>(
                        world, component->node().scene().id(), std::move(identity),
                        edit_revision(*revisions, component->node(), component),
                        std::move(settings), std::move(before), reset, center, radius, damage,
                        impulse);
                    return true;
                }))
        {
            return false;
        }
    }
    return true;
}

} // namespace engine::plugins::destruction
