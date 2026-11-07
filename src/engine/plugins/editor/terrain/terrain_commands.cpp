#include "plugins/editor/terrain/terrain_commands.hpp"
#include "editor/resource_access.hpp"
#include "plugins/runtime/terrain/terrain_resource.hpp"
#include "runtime/resource/virtual_file_system.hpp"

#include "editor/commands/components/component_edit.hpp"
#include "editor/commands/scene_commands.hpp"
#include "plugins/editor/terrain/terrain_files.hpp"
#include "plugins/editor/terrain/terrain_settings_edit.hpp"
#include "plugins/editor/terrain/terrain_state_edit.hpp"
#include "plugins/runtime/terrain/terrain_component.hpp"
#include "runtime/resource/resource_system.hpp"
#include "runtime/schema/terrain_state_generated.h"

#include <flatbuffers/flatbuffers.h>
#include <flatbuffers/verifier.h>

#include <array>
#include <span>
#include <system_error>

namespace engine::plugins::terrain
{
namespace
{
using Json = nlohmann::json;
constexpr std::uintmax_t maximum_state_bytes = 32U * 1024U * 1024U;

bool resource(ResourceLoader* loader, const Json& input, schema::AssetKind kind, Guid& output,
              std::string& error)
{
    if (!input.is_string())
    {
        error = "Terrain resource and material must be resource path strings";
        return false;
    }
    const auto& path = input.get_ref<const std::string&>();
    if (path.empty())
    {
        output.clear();
        return true;
    }
    engine::ResourceReference resource;
    if (!editor::resource_access::load(loader, path, resource) || resource->kind() != kind)
    {
        error = "Terrain resource path is missing or has the wrong resource type";
        return false;
    }
    output = resource->info().guid;
    return true;
}

bool color(const Json& input, schema::Color& output)
{
    if (!input.is_array() || input.size() != 4)
    {
        return false;
    }
    std::array<float, 4> values{};
    for (std::size_t index = 0; index < values.size(); ++index)
    {
        if (!editor::components::number(input[index], values[index]) || values[index] < 0 ||
            values[index] > 1)
        {
            return false;
        }
    }
    output = schema::Color(values[0], values[1], values[2], values[3]);
    return true;
}

bool read_settings(ResourceLoader* loader, const Json& arguments, schema::TerrainDataT& value,
                   std::string& error)
{
    if (!editor::components::fields(
            arguments,
            {"enabled", "terrain", "material", "collision", "surfaceColor", "wallColor", "uvScale"},
            error) ||
        (arguments.contains("terrain") &&
         !resource(loader, arguments["terrain"], schema::AssetKind::terrain, value.terrain_guid,
                   error)) ||
        (arguments.contains("material") &&
         !resource(loader, arguments["material"], schema::AssetKind::material, value.material_guid,
                   error)))
    {
        return false;
    }
    if (arguments.contains("collision"))
    {
        if (!arguments["collision"].is_boolean())
        {
            error = "Terrain collision must be boolean";
            return false;
        }
        value.collision = arguments["collision"].get<bool>();
    }
    if ((arguments.contains("surfaceColor") &&
         !color(arguments["surfaceColor"], value.surface_color)) ||
        (arguments.contains("wallColor") && !color(arguments["wallColor"], value.wall_color)) ||
        (arguments.contains("uvScale") &&
         !editor::components::number(arguments["uvScale"], value.uv_scale)) ||
        !TerrainComponent::valid_settings(value))
    {
        error =
            "Terrain requires valid resource GUIDs, RGBA colors in [0,1] and a positive UV scale";
        return false;
    }
    return true;
}

Json settings_json(ResourceLoader* loader, const schema::TerrainDataT& value)
{
    std::string terrain;
    std::string material;
    if (!value.terrain_guid.empty())
    {
        static_cast<void>(editor::resource_access::path(loader, value.terrain_guid, terrain));
    }
    if (!value.material_guid.empty())
    {
        static_cast<void>(editor::resource_access::path(loader, value.material_guid, material));
    }
    return {{"terrain", terrain},
            {"terrainGuid", value.terrain_guid},
            {"material", material},
            {"materialGuid", value.material_guid},
            {"collision", value.collision},
            {"surfaceColor", Json::array({value.surface_color.r(), value.surface_color.g(),
                                          value.surface_color.b(), value.surface_color.a()})},
            {"wallColor", Json::array({value.wall_color.r(), value.wall_color.g(),
                                       value.wall_color.b(), value.wall_color.a()})},
            {"uvScale", value.uv_scale}};
}

Json component_json(const TerrainComponent& component)
{
    schema::TerrainDataT settings;
    if (!component.get_settings(settings))
    {
        return nullptr;
    }
    auto result = settings_json(component.node().scene()->world().resource_loader(), settings);
    result["enabled"] = component.enabled();
    result["ready"] = component.ready();
    result["revision"] = component.revision();
    result["chunks"] = component.chunk_count();
    result["rebuiltChunks"] = component.rebuilt_chunks();
    result["triangles"] = component.triangle_count();
    result["fragmentCount"] = component.fragment_count();
    result["sceneId"] = std::to_string(component.node().scene()->id());
    result["nodeId"] = std::to_string(component.node().id());
    result["lastError"] = component.last_error();
    return result;
}

TerrainComponent target(WorldSystem& world, const Json& arguments, std::string& error)
{
    auto node = editor::components::target(world, arguments, error);
    auto component = node.valid() ? node.get_component<TerrainComponent>() : TerrainComponent{};
    if (!component && error.empty())
    {
        error = "Node has no terrain component";
    }
    return component;
}

bool bounded_integer(const Json& input, std::uint32_t maximum, std::uint32_t& value)
{
    if (!input.is_number_integer())
    {
        return false;
    }
    if (input.is_number_unsigned())
    {
        const auto number = input.get<std::uint64_t>();
        if (number == 0 || number > maximum)
        {
            return false;
        }
        value = static_cast<std::uint32_t>(number);
        return true;
    }
    const auto number = input.get<std::int64_t>();
    if (number <= 0 || number > maximum)
    {
        return false;
    }
    value = static_cast<std::uint32_t>(number);
    return true;
}

bool unpack_state(ByteView bytes, std::unique_ptr<schema::TerrainStateDataT>& output,
                  std::string& error)
{
    output.reset();
    if (bytes.empty() || bytes.size() > maximum_state_bytes)
    {
        error = "Terrain state is empty or exceeds 32 MiB";
        return false;
    }
    flatbuffers::Verifier verifier(reinterpret_cast<const std::uint8_t*>(bytes.data()),
                                   bytes.size());
    if (!schema::VerifyTerrainStateDataBuffer(verifier))
    {
        error = "Terrain state has invalid FlatBuffers data";
        return false;
    }
    auto state = std::unique_ptr<schema::TerrainStateDataT>(
        schema::GetTerrainStateData(bytes.data())->UnPack());
    if (state->schema_version != 1 || !GuidMap::valid_guid(state->base_guid) ||
        state->base_hash.empty() || !state->volume ||
        !TerrainVolume::validate(*state->volume, error))
    {
        if (error.empty())
        {
            error = "Terrain state has invalid version, base identity or volume";
        }
        return false;
    }
    output = std::move(state);
    return true;
}

ByteBuffer pack_state(const schema::TerrainStateDataT& state)
{
    flatbuffers::FlatBufferBuilder builder;
    schema::FinishTerrainStateDataBuffer(builder, schema::TerrainStateData::Pack(builder, &state));
    const auto bytes = std::as_bytes(std::span(builder.GetBufferPointer(), builder.GetSize()));
    return ByteBuffer(bytes.begin(), bytes.end());
}

bool reset_state(ResourceLoader* loader, schema::TerrainStateDataT& state, std::string& error)
{
    engine::ResourceReference resource;
    if (!editor::resource_access::load(loader, state.base_guid, resource) ||
        !resource.data<TerrainResource>() || resource->info().content_hash != state.base_hash)
    {
        error = "Cannot load the original terrain resource version";
        return false;
    }
    state.volume = std::make_unique<schema::TerrainAssetDataT>(*resource.data<TerrainResource>());
    return true;
}

bool settings_state(ResourceLoader* loader, const schema::TerrainDataT& settings,
                    const ByteBuffer& previous, ByteBuffer& output, std::string& error)
{
    output.clear();
    if (settings.terrain_guid.empty())
    {
        return true;
    }
    engine::ResourceReference base;
    if (!editor::resource_access::load(loader, settings.terrain_guid, base) || !base ||
        base->info().kind != schema::AssetKind::terrain)
    {
        error = "Cannot prepare the original terrain resource";
        return false;
    }
    if (!previous.empty())
    {
        std::unique_ptr<schema::TerrainStateDataT> state;
        if (!unpack_state(previous, state, error))
        {
            return false;
        }
        if (state->base_guid == settings.terrain_guid &&
            state->base_hash == base->info().content_hash)
        {
            output = previous;
            return true;
        }
    }
    schema::TerrainStateDataT state;
    state.base_guid = settings.terrain_guid;
    state.base_hash = base->info().content_hash;
    if (!reset_state(loader, state, error))
    {
        return false;
    }
    output = pack_state(state);
    return true;
}

bool state_edit(WorldSystem& world, editor::SceneCommands& scene_commands,
                const std::filesystem::path& project, std::string_view operation,
                const Json& arguments, Json& result, std::unique_ptr<editor::EditorEdit>& edit,
                std::string& error)
{
    const bool brush = operation == "terrain_brush";
    const bool load = operation == "terrain_load_state";
    if (!(brush  ? editor::components::fields(arguments, {"center", "radius", "operation"}, error)
          : load ? editor::components::fields(arguments, {"path"}, error)
                 : editor::components::fields(arguments, {}, error)))
    {
        return false;
    }
    auto component = target(world, arguments, error);
    if (component && component.fragment_count() != 0)
    {
        error = "Dynamic terrain fragments cannot be captured in volume-only undo/save state; "
                "use terrain_reset_runtime before this edit";
        return false;
    }
    ByteBuffer before;
    schema::ComponentDataT settings;
    settings.flags = std::make_unique<schema::ComponentFlagsT>();
    std::unique_ptr<schema::TerrainStateDataT> state;
    if (!component || !component.ready() || !component.snapshot(before) ||
        !component.build_data(settings) || !unpack_state(before, state, error))
    {
        if (error.empty())
        {
            error = "Terrain must be ready before editing its volume";
        }
        return false;
    }
    if (brush)
    {
        Vec3 center;
        float radius = 0;
        if (!arguments.contains("center") ||
            !editor::components::vector(arguments["center"], center) ||
            !arguments.contains("radius") ||
            !editor::components::number(arguments["radius"], radius) || radius <= 0 ||
            (arguments.contains("operation") &&
             (!arguments["operation"].is_string() ||
              (arguments["operation"] != "dig" && arguments["operation"] != "fill"))))
        {
            error = "Terrain brush requires finite local center, positive radius and dig/fill "
                    "operation";
            return false;
        }
        TerrainVolume volume;
        if (!volume.initialize(*state->volume, error) ||
            !volume.apply_sphere(schema::Vec3(center.x(), center.y(), center.z()), radius,
                                 arguments.value("operation", "dig") == "fill", error))
        {
            return false;
        }
        *state->volume = volume.data();
    }
    else if (load)
    {
        std::filesystem::path path;
        if (!arguments.contains("path") || !arguments["path"].is_string() ||
            !terrain_file_path(project, "cache/terrain-states",
                               arguments["path"].get_ref<const std::string&>(), ".terrainstate",
                               path, error))
        {
            if (error.empty())
            {
                error = "Terrain load requires a relative .terrainstate path";
            }
            return false;
        }
        std::error_code filesystem_error;
        const auto size = std::filesystem::file_size(path, filesystem_error);
        ByteBuffer loaded;
        std::unique_ptr<schema::TerrainStateDataT> replacement;
        if (filesystem_error || size > maximum_state_bytes ||
            !VirtualFileSystem::read_file(path, loaded) ||
            !unpack_state(loaded, replacement, error))
        {
            if (error.empty())
            {
                error = "Cannot read terrain state, or state exceeds 32 MiB";
            }
            return false;
        }
        if (replacement->base_guid != state->base_guid ||
            replacement->base_hash != state->base_hash)
        {
            error = "Saved terrain state belongs to a different base resource or version";
            return false;
        }
        state = std::move(replacement);
    }
    else if (!reset_state(world.resource_loader(), *state, error))
    {
        return false;
    }
    auto identity = scene_commands.node_identity(component.node().id());
    if (!identity)
    {
        error = "Cannot resolve stable terrain node identity";
        return false;
    }
    auto after = pack_state(*state);
    result = {{"sceneId", std::to_string(component.node().scene()->id())},
              {"nodeId", std::to_string(component.node().id())},
              {"operation", operation},
              {"changed", before != after},
              {"undoable", true}};
    edit = std::make_unique<TerrainStateEdit>(world, component.node().scene()->id(),
                                              std::move(identity), std::move(settings),
                                              std::move(before), std::move(after));
    return true;
}

} // namespace

bool register_terrain_commands(WorldSystem& world, editor::SceneCommands& scene_commands,
                               editor::EditorCommands& commands,
                               const std::filesystem::path& project_root)
{
    if (!register_terrain_asset_commands(commands, project_root) ||
        !commands.register_command(
            "set_terrain", true,
            [&world, &scene_commands](const Json& arguments, Json& result,
                                      std::unique_ptr<editor::EditorEdit>& edit, std::string& error)
            {
                auto node = editor::components::target(world, arguments, error);
                if (!node)
                {
                    return false;
                }
                const auto component = node.get_component<TerrainComponent>();
                std::optional<schema::ComponentDataT> before;
                ByteBuffer before_state;
                if (component.valid())
                {
                    before.emplace();
                    if (!component.build_data(*before))
                    {
                        error = "Cannot snapshot terrain settings";
                        return false;
                    }
                    if (component.ready() && !component.snapshot(before_state))
                    {
                        error = "Cannot snapshot current terrain volume";
                        return false;
                    }
                }
                schema::TerrainDataT value;
                if (component && !component.get_settings(value))
                {
                    error = "Cannot read component settings";
                    return false;
                }
                if (!read_settings(world.resource_loader(), arguments, value, error))
                {
                    return false;
                }
                schema::ComponentDataT after;
                after.flags = std::make_unique<schema::ComponentFlagsT>();
                after.flags->enabled = !component || component.enabled();
                after.flags->visible = !component || component.visible();
                if (arguments.contains("enabled"))
                {
                    if (!arguments["enabled"].is_boolean())
                    {
                        error = "Terrain enabled must be boolean";
                        return false;
                    }
                    after.flags->enabled = arguments["enabled"].get<bool>();
                }
                auto identity = scene_commands.node_identity(node.id());
                if (!identity)
                {
                    error = "Cannot resolve stable terrain node identity";
                    return false;
                }
                result = settings_json(world.resource_loader(), value);
                result["enabled"] = after.flags->enabled;
                result["sceneId"] = std::to_string(node.scene()->id());
                result["nodeId"] = std::to_string(node.id());
                ByteBuffer after_state;
                if (!settings_state(world.resource_loader(), value, before_state, after_state,
                                    error))
                {
                    return false;
                }
                after.payload.Set(std::move(value));
                edit = std::make_unique<TerrainSettingsEdit>(
                    world, node.scene()->id(), std::move(identity), std::move(before),
                    std::move(after), std::move(before_state), std::move(after_state));
                return true;
            }) ||
        !commands.register_command("get_terrain", false,
                                   [&world](const Json& arguments, Json& result,
                                            std::unique_ptr<editor::EditorEdit>&,
                                            std::string& error)
                                   {
                                       if (!editor::components::fields(arguments, {}, error))
                                       {
                                           return false;
                                       }
                                       const auto component = target(world, arguments, error);
                                       if (!component)
                                       {
                                           return false;
                                       }
                                       result = component_json(component);
                                       return true;
                                   }) ||
        !commands.register_command(
            "terrain_collapse_detached", false,
            [&world](const Json& arguments, Json& result, std::unique_ptr<editor::EditorEdit>&,
                     std::string& error)
            {
                std::uint32_t minimum = 0;
                std::uint32_t maximum = 0;
                std::uint32_t cells = 4;
                float density = 1600;
                if (!editor::components::fields(
                        arguments, {"minSamples", "maxFragments", "fragmentCells", "density"},
                        error) ||
                    !arguments.contains("minSamples") ||
                    !bounded_integer(arguments["minSamples"], 4U * 1024U * 1024U, minimum) ||
                    !arguments.contains("maxFragments") ||
                    !bounded_integer(arguments["maxFragments"], 128, maximum) ||
                    (arguments.contains("fragmentCells") &&
                     !bounded_integer(arguments["fragmentCells"], 8, cells)) ||
                    (arguments.contains("density") &&
                     !editor::components::number(arguments["density"], density)))
                {
                    if (error.empty())
                    {
                        error = "Terrain collapse requires positive minSamples, maxFragments "
                                "within 1..128, fragmentCells within 1..8 and a finite density";
                    }
                    return false;
                }
                auto component = target(world, arguments, error);
                if (!component || !component.collapse_detached(minimum, maximum, cells, density))
                {
                    if (component)
                    {
                        error = component.last_error();
                    }
                    return false;
                }
                result = component_json(component);
                result["undoable"] = false;
                return true;
            }) ||
        !commands.register_command("terrain_reset_runtime", false,
                                   [&world](const Json& arguments, Json& result,
                                            std::unique_ptr<editor::EditorEdit>&,
                                            std::string& error)
                                   {
                                       if (!editor::components::fields(arguments, {}, error))
                                       {
                                           return false;
                                       }
                                       auto component = target(world, arguments, error);
                                       if (!component || !component.reset())
                                       {
                                           if (component)
                                           {
                                               error = component.last_error();
                                           }
                                           return false;
                                       }
                                       result = component_json(component);
                                       result["undoable"] = false;
                                       return true;
                                   }))
    {
        return false;
    }
    for (const auto* operation : {"terrain_brush", "terrain_reset", "terrain_load_state"})
    {
        if (!commands.register_command(
                operation, true,
                [&world, &scene_commands, project_root,
                 operation](const Json& arguments, Json& result,
                            std::unique_ptr<editor::EditorEdit>& edit, std::string& error)
                {
                    return state_edit(world, scene_commands, project_root, operation, arguments,
                                      result, edit, error);
                }))
        {
            return false;
        }
    }
    return commands.register_command(
        "terrain_save_state", false,
        [&world, project_root](const Json& arguments, Json& result,
                               std::unique_ptr<editor::EditorEdit>&, std::string& error)
        {
            if (!editor::components::fields(arguments, {"path"}, error) ||
                !arguments.contains("path") || !arguments["path"].is_string())
            {
                if (error.empty())
                {
                    error = "Terrain save requires a relative .terrainstate path";
                }
                return false;
            }
            auto component = target(world, arguments, error);
            if (component && component.fragment_count() != 0)
            {
                error = "Volume-only terrain state cannot save dynamic fragments; "
                        "use terrain_reset_runtime first";
                return false;
            }
            std::filesystem::path path;
            if (!component || !component.ready() ||
                !terrain_file_path(project_root, "cache/terrain-states",
                                   arguments["path"].get_ref<const std::string&>(), ".terrainstate",
                                   path, error))
            {
                if (error.empty())
                {
                    error = "Terrain must be ready before saving its state";
                }
                return false;
            }
            std::error_code filesystem_error;
            std::filesystem::create_directories(path.parent_path(), filesystem_error);
            if (filesystem_error || !component.save_state(path))
            {
                error = component.last_error().empty() ? "Cannot save terrain state"
                                                       : component.last_error();
                return false;
            }
            result = component_json(component);
            result["path"] = arguments["path"];
            result["directory"] = "cache/terrain-states";
            return true;
        });
}

} // namespace engine::plugins::terrain
