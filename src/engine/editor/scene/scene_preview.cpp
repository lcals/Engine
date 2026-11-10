#include "editor/scene/scene_preview.hpp"
#include "editor/resource_access.hpp"
#include "runtime/resource/virtual_file_system.hpp"

#include "editor/asset_pipeline/project_lock.hpp"
#include "editor/commands/editor_commands.hpp"
#include "editor/commands/editor_edit.hpp"
#include "editor/editor_environment.hpp"
#include "editor/scene/scene_saved_resource.hpp"
#include "runtime/engine_config.hpp"
#include "runtime/gameplay/gameplay_session.hpp"
#include "runtime/input/input_system.hpp"
#include "runtime/render/render_system.hpp"
#include "runtime/resource/resource_system.hpp"
#include "runtime/scene/camera.hpp"
#include "runtime/scene/node.hpp"
#include "runtime/scene/scene.hpp"
#include "runtime/scene/transform.hpp"
#include "runtime/utils/path_utils.hpp"
#include "runtime/world/world_system.hpp"

#include <SDL3/SDL.h>
#if ENGINE_HAS_ASSET_PIPELINE
#include "editor/asset_pipeline/asset_io.hpp"

#include <efsw/efsw.hpp>
#endif
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <map>
#include <numbers>
#include <utility>

namespace engine::editor
{
namespace
{
namespace fs = std::filesystem;
using Json = nlohmann::json;

std::int64_t milliseconds()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

#if ENGINE_HAS_ASSET_PIPELINE
class SourceWatcher final : public efsw::FileWatchListener
{
public:
    SourceWatcher(std::atomic<std::uint64_t>& serial, std::atomic<std::int64_t>& changed_at)
        : serial_(serial), changed_at_(changed_at)
    {
    }

    bool initialize(const fs::path& root)
    {
        if (watcher_.addWatch(PathUtils::to_utf8(root / "assets"), this, true) < 0 ||
            watcher_.addWatch(PathUtils::to_utf8(root / "settings"), this, false) < 0)
        {
            return false;
        }
        watcher_.watch();
        return true;
    }

private:
    void handleFileAction(efsw::WatchID, const std::string&, const std::string&, efsw::Action,
                          const std::string&) override
    {
        changed_at_.store(milliseconds());
        serial_.fetch_add(1);
    }

    std::atomic<std::uint64_t>& serial_;
    std::atomic<std::int64_t>& changed_at_;
    efsw::FileWatcher watcher_;
};
#endif

bool fields(const Json& value, std::initializer_list<std::string_view> names)
{
    if (!value.is_object())
    {
        return false;
    }
    for (const auto& item : value.items())
    {
        if (std::find(names.begin(), names.end(), item.key()) == names.end())
        {
            return false;
        }
    }
    return true;
}

bool vector(const Json& value, Vec3& output)
{
    if (!value.is_array() || value.size() != 3)
    {
        return false;
    }
    std::array<double, 3> coordinates{};
    for (std::size_t index = 0; index != coordinates.size(); ++index)
    {
        if (!value[index].is_number())
        {
            return false;
        }
        coordinates[index] = value[index].get<double>();
        if (!std::isfinite(coordinates[index]) || std::abs(coordinates[index]) > 10000000.0)
        {
            return false;
        }
    }
    output = {coordinates[0], coordinates[1], coordinates[2]};
    return true;
}

Json vector(const Vec3& value)
{
    return Json::array({value.x(), value.y(), value.z()});
}

bool camera_arguments(const Json& arguments, TransformSettings& pose, double& speed)
{
    if (!fields(arguments, {"position", "rotation", "speed"}) ||
        (arguments.contains("position") && !vector(arguments["position"], pose.position)) ||
        (arguments.contains("rotation") && !vector(arguments["rotation"], pose.rotation)))
    {
        return false;
    }
    if (arguments.contains("speed"))
    {
        if (!arguments["speed"].is_number())
        {
            return false;
        }
        speed = arguments["speed"].get<double>();
    }
    return std::isfinite(speed) && speed >= 0.01 && speed <= 1000.0;
}

bool copy_camera(const Camera& source, TransformSettings& pose)
{
    const auto transform = source.node().get_component<Transform>();
    float matrix[16]{};
    if (!transform || !transform.world_matrix(matrix))
    {
        return false;
    }
    for (std::size_t column = 0; column < 3; ++column)
    {
        const auto offset = column * 4;
        const double length = std::hypot(matrix[offset], matrix[offset + 1], matrix[offset + 2]);
        if (!std::isfinite(length) || length < 1.0e-8)
        {
            return false;
        }
        for (std::size_t row = 0; row < 3; ++row)
        {
            matrix[offset + row] = static_cast<float>(matrix[offset + row] / length);
        }
    }
    constexpr double degrees = 180.0 / std::numbers::pi;
    const double yaw = std::asin(std::clamp(-static_cast<double>(matrix[2]), -1.0, 1.0));
    const bool singular = std::abs(std::cos(yaw)) < 1.0e-6;
    const double pitch =
        singular ? std::atan2(-matrix[9], matrix[5]) : std::atan2(matrix[6], matrix[10]);
    const double roll = singular ? 0.0 : std::atan2(matrix[1], matrix[0]);
    pose.position = {matrix[12], matrix[13], matrix[14]};
    pose.rotation = {pitch * degrees, yaw * degrees, roll * degrees};
    pose.scale = {1, 1, 1};
    return true;
}
} // namespace

struct ScenePreview::State
{
    EditorEnvironment& environment;
    GameplaySession& gameplay;
    WorldSystem& world;
    RenderSystem& renderer;
    InputSystem& input;
    schema::InputChannelHandle input_channel;
    std::uint64_t device_revision = 0;
    fs::path project_root;
    ImportCallback import;
    WorldSystem camera_world;
    EditorCommands* commands = nullptr;
    Camera camera{};
#if ENGINE_HAS_ASSET_PIPELINE
    std::unique_ptr<SourceWatcher> watcher;
#endif
    std::atomic<std::uint64_t> source_serial{0};
    std::atomic<std::int64_t> source_changed_at{0};
    std::uint64_t imported_serial = 0;
    std::uint64_t saved_revision = 0;
    std::int64_t catalog_checked_at = 0;
    fs::file_time_type catalog_stamp{};
    std::string resource_revision;
    ResourceCatalog preview_catalog;
    std::string path;
    std::string error;
    std::map<std::string, SceneSavedResource, std::less<>> saved_resources;
    double speed = 5.0;
    bool reload_pending = false;
    bool initialized = false;
    bool saved_import_pending = false;
    bool saved_import_complete = false;

    State(EditorEnvironment& editor_environment, GameplaySession& session, WorldSystem& scene_world,
          RenderSystem& render_system, InputSystem& input_system, fs::path root,
          ImportCallback callback)
        : environment(editor_environment), gameplay(session), world(scene_world),
          renderer(render_system), input(input_system), project_root(std::move(root)),
          import(std::move(callback))
    {
    }

    ~State()
    {
#if ENGINE_HAS_ASSET_PIPELINE
        watcher.reset();
#endif
        // 命令服务按契约已释放；显式关闭负责清历史，析构不访问其借用。
        commands = nullptr;
        close();
        if (input_channel.generation() != 0)
        {
            (void)input.destroy_channel(input_channel);
        }
        camera_world.shutdown();
    }

    bool active() const noexcept
    {
        return camera.valid() && !path.empty();
    }

    bool modified() const noexcept
    {
        return active() && commands != nullptr && commands->mutation_revision() != saved_revision;
    }

    bool fail(std::string message)
    {
        error = std::move(message);
        return false;
    }

    void close() noexcept
    {
        if (input_channel.generation() != 0)
        {
            (void)input.set_channel_enabled(input_channel, false,
                                            schema::InputActivation::wait_for_neutral);
        }
        if (active())
        {
            gameplay.stop();
            if (commands)
            {
                commands->clear_history();
            }
        }
        camera = {};
        camera_world.destroy_scene();
        path.clear();
        resource_revision.clear();
        saved_resources.clear();
        saved_import_pending = false;
        saved_import_complete = false;
        reload_pending = false;
        error.clear();
    }

    Json status() const
    {
        Json view = nullptr;
        TransformSettings pose;
        if (camera && camera.node().get_component<Transform>().get_settings(pose))
        {
            view = {{"position", vector(pose.position)},
                    {"rotation", vector(pose.rotation)},
                    {"speed", speed}};
        }
        return {{"active", active()},
                {"path", path},
                {"sceneId", active() && world.scene() ? std::to_string(world.scene()->id()) : "0"},
                {"camera", std::move(view)},
                {"modified", modified()},
                {"reloadPending", reload_pending},
                {"lastError", error}};
    }

    bool import_sources()
    {
        const auto serial = source_serial.load();
        imported_serial = serial;
        if (import && !import(error))
        {
            if (error.empty())
            {
                error = "Cannot import changed project assets";
            }
            reload_pending = true;
            return false;
        }
        return true;
    }

    bool checkpoint()
    {
        if (commands)
        {
            commands->clear_history();
            saved_revision = commands->mutation_revision();
        }
        if (!ResourceSystem::read_catalog(project_root / "cache", preview_catalog))
        {
            return fail("Cannot checkpoint preview resource directory");
        }
        resource_revision = preview_catalog.revision();
        std::error_code ignored;
        catalog_stamp = fs::last_write_time(project_root / "cache/catalog.bin", ignored);
        saved_resources.clear();
        saved_import_pending = false;
        saved_import_complete = false;
        reload_pending = false;
        error.clear();
        return true;
    }

    bool open(std::string_view requested, const Json& arguments)
    {
        if (!initialized || !GuidMap::valid_path(requested) ||
            requested.find(':') != requested.npos || !requested.ends_with(".scene"))
        {
            return fail(
                "Preview requires an initialized editor and an assets-relative .scene path");
        }
        TransformSettings pose;
        pose.position = {0, 2, 5};
        double requested_speed = 5.0;
        if (!camera_arguments(arguments, pose, requested_speed))
        {
            return fail(
                "Camera requires finite position/rotation vectors and speed in [0.01,1000]");
        }
        if (!import_sources())
        {
            return false;
        }
        asset_pipeline::ProjectLock lock;
        if (!lock.acquire(project_root / "cache/.asset-build.lock", error))
        {
            return false;
        }
        close();
        gameplay.stop();
        if (commands)
        {
            commands->clear_history();
        }
        if (!environment.refresh_resources(gameplay))
        {
            return fail("Cannot refresh the editor resource directory");
        }
        ResourceReference root;
        if (!resource_access::load(world.resource_loader(), requested, root))
        {
            return fail("Cannot prepare preview scene: " + std::string(requested));
        }
        auto* scene = world.load_scene(requested);
        if (!scene)
        {
            return fail("Cannot load preview scene: " + std::string(requested));
        }
        CameraSettings projection;
        if (const auto source = scene->active_camera())
        {
            if (!source.get_settings(projection) || !copy_camera(source, pose))
            {
                gameplay.stop();
                return fail("Cannot read the scene camera world transform");
            }
        }
        if (!camera_arguments(arguments, pose, requested_speed))
        {
            gameplay.stop();
            return fail("Invalid preview camera settings");
        }
        auto* view_scene = camera_world.create_scene("Editor Preview Camera");
        auto node = view_scene ? view_scene->create_node("Preview Camera") : Node{};
        auto view = node ? node.add_component<Camera>() : Camera{};
        if (!view || !node.get_component<Transform>().set_settings(pose) ||
            !view.set_settings(projection))
        {
            gameplay.stop();
            camera_world.destroy_scene();
            return fail("Cannot initialize the editor preview camera");
        }
        camera = view;
        path = requested;
        speed = requested_speed;
        if (!configure_input())
        {
            close();
            return fail("Cannot prepare preview rendering resources");
        }
        return checkpoint();
    }

    bool refresh(bool discard_changes, bool rebuild)
    {
        if (!active())
        {
            return fail("No scene preview is active");
        }
        if (modified() && !discard_changes)
        {
            reload_pending = true;
            return fail(
                "Preview has unsaved edits; save the scene or refresh with discardChanges=true");
        }
        if (rebuild && !import_sources())
        {
            return false;
        }
        asset_pipeline::ProjectLock lock;
        if (!lock.acquire(project_root / "cache/.asset-build.lock", error) ||
            !environment.refresh_resources(gameplay))
        {
            if (error.empty())
            {
                error = "Cannot refresh the editor resource directory";
            }
            reload_pending = true;
            return false;
        }
        ResourceReference root;
        if (!resource_access::load(world.resource_loader(), path, root) || !world.load_scene(path))
        {
            reload_pending = true;
            return fail("Cannot reload preview scene; the current scene was retained");
        }
        renderer.release_scene();
        return checkpoint();
    }

    bool configure_input()
    {
        schema::InputChannelDataT data;
        data.enabled = active();
        data.activation = schema::InputActivation::wait_for_neutral;
        const auto action = [&](std::string name, schema::InputActionKind kind,
                                schema::InputValueMode mode = schema::InputValueMode::state)
        {
            auto value = std::make_unique<schema::InputActionDataT>();
            value->name = std::move(name);
            value->kind = kind;
            value->value_mode = mode;
            auto* pointer = value.get();
            data.actions.push_back(std::move(value));
            return pointer;
        };
        auto* move = action("move", schema::InputActionKind::axis2d);
        auto* vertical = action("vertical", schema::InputActionKind::axis1d);
        auto* fast = action("fast", schema::InputActionKind::button);
        auto* navigate = action("navigate", schema::InputActionKind::button);
        auto* look = action("look", schema::InputActionKind::axis2d, schema::InputValueMode::delta);
        const auto binding = [](schema::InputActionDataT& target)
        {
            auto value = std::make_unique<schema::InputBindingDataT>();
            auto* pointer = value.get();
            target.bindings.push_back(std::move(value));
            return pointer;
        };
        const auto part = [](schema::InputBindingDataT& target, std::uint64_t device,
                             schema::InputControlKind kind, std::uint32_t code, double x,
                             double y = 0.0)
        {
            auto value = std::make_unique<schema::InputBindingPartDataT>();
            value->source = {device, kind, code};
            value->weight_x = x;
            value->weight_y = y;
            target.parts.push_back(std::move(value));
        };
        for (const auto& device : input.devices())
        {
            const auto id = device.connection_id;
            if (device.kind == schema::InputDeviceKind::keyboard)
            {
                auto* movement = binding(*move);
                part(*movement, id, schema::InputControlKind::key, SDL_SCANCODE_D, 1);
                part(*movement, id, schema::InputControlKind::key, SDL_SCANCODE_A, -1);
                part(*movement, id, schema::InputControlKind::key, SDL_SCANCODE_W, 0, 1);
                part(*movement, id, schema::InputControlKind::key, SDL_SCANCODE_S, 0, -1);
                auto* elevation = binding(*vertical);
                part(*elevation, id, schema::InputControlKind::key, SDL_SCANCODE_E, 1);
                part(*elevation, id, schema::InputControlKind::key, SDL_SCANCODE_Q, -1);
                part(*binding(*fast), id, schema::InputControlKind::key, SDL_SCANCODE_LSHIFT, 1);
                part(*binding(*fast), id, schema::InputControlKind::key, SDL_SCANCODE_RSHIFT, 1);
            }
            else if (device.kind == schema::InputDeviceKind::mouse)
            {
                part(*binding(*navigate), id, schema::InputControlKind::mouse_button,
                     SDL_BUTTON_RIGHT, 1);
                auto* delta = binding(*look);
                delta->normalize = false;
                part(*delta, id, schema::InputControlKind::mouse_delta, 0, 1);
                part(*delta, id, schema::InputControlKind::mouse_delta, 1, 0, 1);
            }
        }
        if (!input.configure_channel(input_channel, data))
        {
            return fail("Cannot configure preview input: " + input.last_error());
        }
        device_revision = input.frame_state().device_revision();
        return true;
    }

    bool move_camera(double delta_time)
    {
        if (device_revision != input.frame_state().device_revision() && !configure_input())
        {
            return false;
        }
        schema::InputActionState navigate;
        if (!input.read_action(input_channel, "navigate", navigate) || !navigate.held())
        {
            return true;
        }
        auto transform = camera.node().get_component<Transform>();
        TransformSettings pose;
        if (!transform.get_settings(pose))
        {
            return fail("Cannot read preview camera transform");
        }
        schema::InputActionState delta;
        (void)input.read_action(input_channel, "look", delta);
        pose.rotation = {std::clamp(pose.rotation.x() - delta.value_y() * 0.12, -89.5, 89.5),
                         std::remainder(pose.rotation.y() - delta.value_x() * 0.12, 360.0),
                         pose.rotation.z()};
        if (!transform.set_settings(pose))
        {
            return fail("Cannot update preview camera orientation");
        }
        schema::InputActionState movement;
        schema::InputActionState elevation;
        schema::InputActionState fast;
        (void)input.read_action(input_channel, "move", movement);
        (void)input.read_action(input_channel, "vertical", elevation);
        (void)input.read_action(input_channel, "fast", fast);
        double right = movement.value_x();
        double forward = movement.value_y();
        double up = elevation.value_x();
        const double length = std::max(1.0, std::hypot(right, forward, up));
        const double distance =
            std::min(delta_time, 0.1) * speed * (fast.held() ? 4.0 : 1.0) / length;
        right *= distance;
        forward *= distance;
        up *= distance;
        float matrix[16]{};
        if (!transform.world_matrix(matrix))
        {
            return fail("Cannot read preview camera basis");
        }
        pose.position = {pose.position.x() + matrix[0] * right - matrix[8] * forward,
                         pose.position.y() + matrix[1] * right - matrix[9] * forward + up,
                         pose.position.z() + matrix[2] * right - matrix[10] * forward};
        return transform.set_settings(pose) || fail("Cannot move preview camera");
    }

    bool matches_saved_catalog(const ResourceCatalog& catalog) const
    {
#if ENGINE_HAS_ASSET_PIPELINE
        if (!saved_import_pending || !saved_import_complete)
        {
            return false;
        }
        const auto& previous = preview_catalog;
        if (previous.data().schema_version != catalog.data().schema_version ||
            previous.data().target_platform != catalog.data().target_platform)
        {
            return false;
        }
        for (const auto& entry : previous.data().entries)
        {
            const auto* current = catalog.find(entry->guid);
            if (current == nullptr ||
                (*entry != *current && !saved_resources.contains(entry->guid)))
            {
                return false;
            }
        }
        for (const auto& entry : catalog.data().entries)
        {
            if (previous.find(entry->guid) == nullptr && !saved_resources.contains(entry->guid))
            {
                return false;
            }
        }
        for (const auto& [guid, saved] : saved_resources)
        {
            ByteBuffer source;
            ByteBuffer meta;
            auto meta_path = saved.source;
            meta_path += ".meta";
            if (catalog.find(guid) == nullptr ||
                !VirtualFileSystem::read_file(saved.source, source) ||
                !VirtualFileSystem::read_file(meta_path, meta) ||
                asset_pipeline::hash_bytes(source) != saved.source_hash ||
                asset_pipeline::hash_bytes(meta) != saved.meta_hash)
            {
                return false;
            }
        }
        return true;
#else
        (void)catalog;
        return false;
#endif
    }

    void poll_resources()
    {
        const auto now = milliseconds();
        bool imported = false;
        if (source_serial.load() != imported_serial && now - source_changed_at.load() >= 500)
        {
            if (!import_sources())
            {
                return;
            }
            imported = true;
            saved_import_complete = saved_import_pending;
        }
        // 等本次保存后的源文件完成导入，不能把较早发布的目录误认作最新保存结果。
        if (saved_import_pending && !saved_import_complete &&
            source_serial.load() != imported_serial)
        {
            return;
        }
        if (!imported && now - catalog_checked_at < 250)
        {
            return;
        }
        catalog_checked_at = now;
        std::error_code file_error;
        const auto stamp = fs::last_write_time(project_root / "cache/catalog.bin", file_error);
        if (file_error || (!imported && stamp == catalog_stamp))
        {
            return;
        }
        {
            asset_pipeline::ProjectLock lock;
            ResourceCatalog catalog;
            if (!lock.acquire(project_root / "cache/.asset-build.lock", error) ||
                !ResourceSystem::read_catalog(project_root / "cache", catalog))
            {
                reload_pending = true;
                if (error.empty())
                {
                    error = "Cannot read the updated preview resource directory";
                }
                return;
            }
            if (catalog.revision() == resource_revision)
            {
                catalog_stamp = stamp;
                saved_resources.clear();
                saved_import_pending = false;
                saved_import_complete = false;
                reload_pending = false;
                error.clear();
                return;
            }
            if (matches_saved_catalog(catalog))
            {
                if (!environment.apply_resources(catalog.data(), gameplay))
                {
                    reload_pending = true;
                    error = "Cannot submit the saved preview resource directory";
                    return;
                }
                resource_revision = catalog.revision();
                preview_catalog = catalog;
                catalog_stamp = stamp;
                saved_resources.clear();
                saved_import_pending = false;
                saved_import_complete = false;
                reload_pending = false;
                error.clear();
                return;
            }
        }
        saved_resources.clear();
        saved_import_pending = false;
        saved_import_complete = false;
        reload_pending = true;
        if (modified())
        {
            error = "External assets changed; unsaved preview edits were preserved";
            return;
        }
        // A failed version is retried after the next source/catalog change or explicit refresh.
        catalog_stamp = stamp;
        (void)refresh(false, false);
    }
};

ScenePreview::ScenePreview(EditorEnvironment& environment, GameplaySession& gameplay,
                           WorldSystem& world, RenderSystem& renderer, InputSystem& input,
                           fs::path project_root, ImportCallback import)
    : state_(std::make_unique<State>(environment, gameplay, world, renderer, input,
                                     std::move(project_root), std::move(import)))
{
}

ScenePreview::~ScenePreview() noexcept = default;

bool ScenePreview::initialize(const EngineConfig& config)
{
    auto& state = *state_;
    if (state.initialized || !state.environment.is_initialized() || !state.world.is_initialized() ||
        !state.input.is_initialized() || !state.camera_world.initialize(config))
    {
        return state.fail("Cannot initialize the editor preview world");
    }
    if (!state.input.create_channel(state.input_channel) || !state.configure_input())
    {
        return state.fail("Cannot initialize preview input: " + state.input.last_error());
    }
#if ENGINE_HAS_ASSET_PIPELINE
    if (state.import)
    {
        state.watcher =
            std::make_unique<SourceWatcher>(state.source_serial, state.source_changed_at);
        if (!state.watcher->initialize(state.project_root))
        {
            state.watcher.reset();
            state.camera_world.shutdown();
            return state.fail("Cannot watch the project assets and settings directories");
        }
    }
#endif
    state.initialized = true;
    return true;
}

bool ScenePreview::register_commands(EditorCommands& commands)
{
    auto& state = *state_;
    if (!state.initialized || state.commands != nullptr)
    {
        return false;
    }
    state.commands = &commands;
    return commands.register_command(
               "open_scene_preview", false,
               [&state](const Json& arguments, Json& result, std::unique_ptr<EditorEdit>&,
                        std::string& error)
               {
                   const bool discarded = state.modified();
                   if (!fields(arguments, {"path", "camera"}) || !arguments.contains("path") ||
                       !arguments["path"].is_string())
                   {
                       error = "open_scene_preview requires path and optional camera";
                       return false;
                   }
                   const Json view = arguments.value("camera", Json::object());
                   if (!state.open(arguments["path"].get_ref<const std::string&>(), view))
                   {
                       error = state.error;
                       return false;
                   }
                   result = state.status();
                   result["discardedChanges"] = discarded;
                   return true;
               }) &&
           commands.register_command("close_scene_preview", false,
                                     [&state](const Json& arguments, Json& result,
                                              std::unique_ptr<EditorEdit>&, std::string& error)
                                     {
                                         if (!fields(arguments, {}) || !arguments.empty())
                                         {
                                             error = "close_scene_preview takes no arguments";
                                             return false;
                                         }
                                         const bool discarded = state.modified();
                                         state.close();
                                         result = state.status();
                                         result["discardedChanges"] = discarded;
                                         return true;
                                     }) &&
           commands.register_command("get_scene_preview", false,
                                     [&state](const Json& arguments, Json& result,
                                              std::unique_ptr<EditorEdit>&, std::string& error)
                                     {
                                         if (!fields(arguments, {}) || !arguments.empty())
                                         {
                                             error = "get_scene_preview takes no arguments";
                                             return false;
                                         }
                                         result = state.status();
                                         return true;
                                     }) &&
           commands.register_command(
               "set_preview_camera", false,
               [&state](const Json& arguments, Json& result, std::unique_ptr<EditorEdit>&,
                        std::string& error)
               {
                   if (!state.active())
                   {
                       error = "No scene preview is active";
                       return false;
                   }
                   auto transform = state.camera.node().get_component<Transform>();
                   TransformSettings pose;
                   if (!transform.get_settings(pose))
                   {
                       error = "Cannot read preview camera transform";
                       return false;
                   }
                   double speed = state.speed;
                   if (arguments.empty() || !camera_arguments(arguments, pose, speed) ||
                       !transform.set_settings(pose))
                   {
                       error = "Camera requires position, rotation or speed in [0.01,1000]";
                       return false;
                   }
                   state.speed = speed;
                   result = state.status();
                   return true;
               }) &&
           commands.register_command(
               "refresh_scene_preview", false,
               [&state](const Json& arguments, Json& result, std::unique_ptr<EditorEdit>&,
                        std::string& error)
               {
                   if (!fields(arguments, {"discardChanges"}) ||
                       (arguments.contains("discardChanges") &&
                        !arguments["discardChanges"].is_boolean()))
                   {
                       error = "refresh_scene_preview accepts optional discardChanges";
                       return false;
                   }
                   const bool discarded = state.modified();
                   if (!state.refresh(arguments.value("discardChanges", false), true))
                   {
                       error = state.error;
                       return false;
                   }
                   result = state.status();
                   result["discardedChanges"] = discarded;
                   return true;
               });
}

bool ScenePreview::tick(double delta_time)
{
    if (!state_->initialized || !std::isfinite(delta_time) || delta_time < 0.0)
    {
        return state_->fail("Invalid scene preview frame");
    }
    if (!active())
    {
        return true;
    }
    state_->poll_resources();
    if (!active())
    {
        return true;
    }
    if (state_->renderer.prepare_scope({}, {}, state_->camera) == SceneWorkState::failed)
    {
        ScenePreparationSnapshot snapshot;
        (void)state_->renderer.scope_status({}, snapshot);
        const auto error = snapshot.stages.empty() ? "Scene preview preparation failed"
                                                   : snapshot.stages.front().error;
        state_->close();
        return state_->fail(error);
    }
    return state_->move_camera(delta_time);
}

void ScenePreview::close() noexcept
{
    state_->close();
}

bool ScenePreview::active() const noexcept
{
    return state_->active();
}

const Camera ScenePreview::camera() const noexcept
{
    return state_->camera;
}

void ScenePreview::mark_saved(std::string_view path, std::span<const SceneSavedResource> resources)
{
    if (active() && state_->path == path && state_->commands)
    {
        state_->saved_revision = state_->commands->mutation_revision();
        for (const auto& resource : resources)
        {
            state_->saved_resources.insert_or_assign(resource.guid, resource);
        }
        state_->saved_import_pending = true;
        state_->saved_import_complete = false;
        state_->source_changed_at.store(milliseconds());
        state_->source_serial.fetch_add(1);
    }
}

const std::string& ScenePreview::last_error() const noexcept
{
    return state_->error;
}
} // namespace engine::editor
