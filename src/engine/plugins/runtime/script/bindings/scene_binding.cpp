#include "plugins/runtime/script/bindings/scene_binding.hpp"

#include "plugins/runtime/script/bindings/binding_context.hpp"
#include "runtime/gameplay/gameplay_session.hpp"
#include "runtime/scene/camera.hpp"
#include "runtime/scene/scene.hpp"
#include "runtime/world/world_system.hpp"
#include <nlohmann/json.hpp>

namespace engine::plugins::script::bindings
{
namespace
{
constexpr std::string_view javascript = R"js(
export class Scene {
    constructor(token, scene) { attach(this, token, scene); }
    static Create(name) {
        const id = native.SceneCreate(name);
        return id === null ? null : new Scene(key, id);
    }
    static Load(path) {
        const id = native.SceneLoad(path);
        return id === null ? null : new Scene(key, id);
    }
    static BeginLoad(path, preloadRoots = []) {
        const request = native.SceneBeginLoad(path, JSON.stringify(preloadRoots));
        return request === null ? null : new SceneLoadRequest(key, request);
    }
    static BeginBatch(destination, preloadRoots = []) {
        if (!(destination instanceof Scene)) return null;
        const request = native.SceneBeginBatch(handles.get(destination).scene, JSON.stringify(preloadRoots));
        return request === null ? null : new SceneLoadRequest(key, request);
    }
    FindNode(name) {
        const scene = handles.get(this).scene;
        const node = native.SceneFindNode(scene, name);
        return node === null ? null : new Node(key, scene, node);
    }
    IsValid() { return native.SceneValid(handles.get(this).scene); }
    SetPaused(paused) { return native.SceneSetPaused(handles.get(this).scene, paused); }
    GetPaused() { return native.SceneGetPaused(handles.get(this).scene); }
    Destroy() { return native.SceneDestroy(handles.get(this).scene); }
    CreateNode(name) {
        const scene = handles.get(this).scene;
        const node = native.NodeCreate(scene, name);
        return node === null ? null : new Node(key, scene, node);
    }
    InstantiateModel(path, name) {
        const scene = handles.get(this).scene;
        const node = native.SceneInstantiateModel(scene, path, name);
        return node === null ? null : new Node(key, scene, node);
    }
    InstantiatePrefab(path, name) {
        const scene = handles.get(this).scene;
        const node = native.SceneInstantiatePrefab(scene, path, name);
        return node === null ? null : new Node(key, scene, node);
    }
    SetActiveCamera(camera) {
        const scene = handles.get(this).scene;
        if (camera === null) return native.ActiveCamera(scene, 0n);
        const other = handles.get(camera);
        return camera instanceof Camera && other?.scene === scene &&
            native.ActiveCamera(scene, other.node);
    }
}
const sceneRequests = new WeakMap();
export class SceneLoadRequest {
    constructor(token, request) {
        if (token !== key) throw new TypeError("Scene requests are created by Scene");
        sceneRequests.set(this, request);
    }
    GetStatus() { return native.SceneRequestStatus(sceneRequests.get(this)); }
    GetScene() {
        const id = native.SceneRequestScene(sceneRequests.get(this));
        return id === null ? null : new Scene(key, id);
    }
    Prepare() { return native.SceneRequestAction(sceneRequests.get(this), 0); }
    Activate() { return native.SceneRequestAction(sceneRequests.get(this), 1); }
    Cancel() { return native.SceneRequestAction(sceneRequests.get(this), 2); }
    Release() { return native.SceneRequestAction(sceneRequests.get(this), 3); }
}
Object.freeze(SceneLoadRequest.prototype);
Object.freeze(SceneLoadRequest);
Object.freeze(Scene.prototype);
Object.freeze(Scene);
)js";

void create(const Arguments& arguments)
{
    arguments.set_return({});
    auto* binding = BindingContext::current(arguments);
    std::string name;
    if (binding == nullptr || binding->world() == nullptr || !binding->world()->is_initialized() ||
        arguments.count() != 1 || !BindingContext::text(arguments, 0, name))
    {
        return;
    }
    if (auto* scene = binding->world()->create_scene(name); scene != nullptr)
    {
        binding->set_scene_id(scene->id());
        arguments.set_return(scene->id());
    }
}

void load(const Arguments& arguments)
{
    arguments.set_return({});
    auto* binding = BindingContext::current(arguments);
    std::string path;
    if (binding == nullptr || binding->world() == nullptr || !binding->world()->is_initialized() ||
        arguments.count() != 1 || !BindingContext::text(arguments, 0, path))
    {
        return;
    }
    const auto* current_scene = binding->world()->scene();
    if (current_scene != nullptr && current_scene->id() != binding->scene_id())
    {
        return; // 玩法不能替换由另一个宿主建立的场景。
    }
    if (auto* scene = binding->world()->load_scene(path); scene != nullptr)
    {
        binding->set_scene_id(scene->id());
        arguments.set_return(scene->id());
    }
}

void begin_load(const Arguments& arguments)
{
    arguments.set_return({});
    auto* binding = BindingContext::current(arguments);
    SceneLoadSpec spec;
    std::string roots;
    if (!binding || arguments.count() != 2 || !BindingContext::text(arguments, 1, roots))
    {
        return;
    }
    if (arguments.data() == 0)
    {
        if (!BindingContext::text(arguments, 0, spec.scene_path))
        {
            return;
        }
    }
    else
    {
        spec.kind = SceneLoadKind::append_batch;
        if (!BindingContext::identifier(arguments, 0, spec.destination_scene_id) ||
            !binding->owned_scene(spec.destination_scene_id))
        {
            return;
        }
    }
    const auto values = nlohmann::json::parse(roots, nullptr, false);
    if (!values.is_array() || values.size() > 128)
    {
        return;
    }
    for (const auto& value : values)
    {
        if (!value.is_string())
        {
            return;
        }
        spec.preload_roots.push_back(value.get<std::string>());
    }
    const auto request = binding->begin_scene_load(spec);
    if (request)
    {
        arguments.set_return(request);
    }
}

void request_scene(const Arguments& arguments)
{
    arguments.set_return({});
    auto* binding = BindingContext::current(arguments);
    std::uint64_t token = 0;
    if (!binding || arguments.count() != 1 || !BindingContext::identifier(arguments, 0, token))
    {
        return;
    }
    const auto request = binding->scene_request(token);
    auto* gameplay = binding->gameplay();
    auto* scene = gameplay ? gameplay->scene_load_scene(request) : nullptr;
    if (!scene)
    {
        return;
    }
    SceneLoadSnapshot snapshot;
    if (gameplay->scene_load_status(request, snapshot) && snapshot.state == SceneLoadState::active)
    {
        binding->set_scene_id(scene->id());
    }
    arguments.set_return(scene->id());
}

void request_status(const Arguments& arguments)
{
    arguments.set_return({});
    auto* binding = BindingContext::current(arguments);
    std::uint64_t token = 0;
    if (!binding || arguments.count() != 1 || !BindingContext::identifier(arguments, 0, token) ||
        !binding->gameplay())
    {
        return;
    }
    SceneLoadSnapshot snapshot;
    if (!binding->gameplay()->scene_load_status(binding->scene_request(token), snapshot))
    {
        return;
    }
    using Value = vm::NativeValue;
    constexpr const char* states[] = {"loading",    "staged", "preparing", "ready",
                                      "activating", "active", "failed",    "cancelled"};
    constexpr const char* work_states[] = {"queued",       "working", "ready",
                                           "not_required", "failed",  "cancelled"};
    Value::Array stages;
    for (const auto& stage : snapshot.stages)
    {
        stages.emplace_back(
            Value::Object{{"domain", stage.domain},
                          {"stage", stage.stage},
                          {"state", work_states[static_cast<unsigned>(stage.state)]},
                          {"completed", std::to_string(stage.completed)},
                          {"total", std::to_string(stage.total)},
                          {"totalKnown", stage.total_known},
                          {"inFlight", std::to_string(stage.in_flight)},
                          {"progressRevision", std::to_string(stage.progress_revision)},
                          {"lastProgressNs", std::to_string(stage.last_progress_ns)},
                          {"errorCode", stage.error_code},
                          {"error", stage.error}});
    }
    arguments.set_return(Value::Object{
        {"id", Value::Object{{"owner", std::to_string(snapshot.id.owner)},
                             {"request", std::to_string(snapshot.id.request)}}},
        {"scope", Value::Object{{"worldEpoch", std::to_string(snapshot.scope.world_epoch)},
                                {"value", std::to_string(snapshot.scope.value)}}},
        {"state", states[static_cast<unsigned>(snapshot.state)]},
        {"sceneId", std::to_string(snapshot.scene_id)},
        {"targetAlive", snapshot.target_alive},
        {"progressRevision", std::to_string(snapshot.progress_revision)},
        {"lastProgressNs", std::to_string(snapshot.last_progress_ns)},
        {"firstSceneSubmissionId", std::to_string(snapshot.first_scene_submission_id)},
        {"stages", std::move(stages)},
        {"errorCode", snapshot.error_code},
        {"error", snapshot.error}});
}

void request_action(const Arguments& arguments)
{
    arguments.set_return(false);
    auto* binding = BindingContext::current(arguments);
    std::uint64_t token = 0;
    if (!binding || arguments.count() != 2 || !BindingContext::identifier(arguments, 0, token) ||
        !arguments.is_uint32(1) || !binding->gameplay())
    {
        return;
    }
    const auto request = binding->scene_request(token);
    bool result = false;
    switch (arguments.unsigned_integer(1))
    {
    case 0:
        result = binding->gameplay()->prepare_scene_load(request);
        break;
    case 1:
        result = binding->gameplay()->activate_scene_load(request);
        break;
    case 2:
        result = binding->gameplay()->cancel_scene_load(request);
        break;
    case 3:
        result = binding->release_scene_request(token);
        break;
    default:
        break;
    }
    arguments.set_return(result);
}

void find_node(const Arguments& arguments)
{
    arguments.set_return({});
    auto* scene = BindingContext::scene(arguments);
    std::string name;
    if (scene == nullptr || arguments.count() != 2 || !BindingContext::text(arguments, 1, name))
    {
        return;
    }
    if (const auto node = scene->find_node(name))
    {
        arguments.set_return(node.id());
    }
}

void valid(const Arguments& arguments)
{
    arguments.set_return(arguments.count() == 1 && BindingContext::scene(arguments) != nullptr);
}

void set_paused(const Arguments& arguments)
{
    arguments.set_return(false);
    auto* binding = BindingContext::current(arguments);
    if (arguments.count() != 2 || !arguments.is_boolean(1) || binding == nullptr ||
        BindingContext::scene(arguments) == nullptr ||
        BindingContext::scene(arguments) != binding->world()->scene())
    {
        return;
    }
    arguments.set_return(binding->world()->set_paused(arguments.boolean(1)));
}

void get_paused(const Arguments& arguments)
{
    arguments.set_return({});
    auto* binding = BindingContext::current(arguments);
    if (arguments.count() == 1 && binding != nullptr &&
        BindingContext::scene(arguments) != nullptr &&
        BindingContext::scene(arguments) == binding->world()->scene())
    {
        arguments.set_return(binding->world()->paused());
    }
}

void instantiate_model(const Arguments& arguments)
{
    arguments.set_return({});
    auto* scene = BindingContext::scene(arguments);
    std::string path;
    std::string name;
    if (!scene || arguments.count() != 3 || !BindingContext::text(arguments, 1, path) ||
        !BindingContext::text(arguments, 2, name))
    {
        return;
    }
    if (const auto root = scene->instantiate_model(path, name))
    {
        arguments.set_return(root.id());
    }
}

void instantiate_prefab(const Arguments& arguments)
{
    arguments.set_return({});
    auto* scene = BindingContext::scene(arguments);
    std::string path;
    std::string name;
    if (scene == nullptr || arguments.count() != 3 || !BindingContext::text(arguments, 1, path) ||
        !BindingContext::text(arguments, 2, name))
    {
        return;
    }
    if (const auto root = scene->instantiate_prefab(path, name); root.valid())
    {
        arguments.set_return(root.id());
    }
}

void destroy(const Arguments& arguments)
{
    arguments.set_return(false);
    auto* binding = BindingContext::current(arguments);
    if (arguments.count() == 1 && binding && BindingContext::scene(arguments) != nullptr &&
        BindingContext::scene(arguments) == binding->world()->scene())
    {
        binding->world()->unload_scene();
        binding->set_scene_id(0);
        arguments.set_return(true);
    }
}

void active_camera(const Arguments& arguments)
{
    arguments.set_return(false);
    auto* scene = BindingContext::scene(arguments);
    std::uint64_t id = 0;
    if (scene == nullptr || arguments.count() != 2 || !BindingContext::identifier(arguments, 1, id))
    {
        return;
    }
    if (id == 0)
    {
        arguments.set_return(scene->set_active_camera({}));
        return;
    }
    auto node = scene->find_node(id);
    auto camera = !node ? Camera{} : node.get_component<Camera>();
    arguments.set_return(camera.valid() && scene->set_active_camera(camera));
}
} // namespace

std::string_view SceneBinding::name() const noexcept
{
    return "Scene";
}

std::string_view SceneBinding::source() const noexcept
{
    return javascript;
}

bool SceneBinding::register_functions(vm::ScriptVm& vm) const
{
    return vm.function("SceneCreate", create) && vm.function("SceneLoad", load) &&
           vm.function("SceneBeginLoad", begin_load, 0) &&
           vm.function("SceneBeginBatch", begin_load, 1) &&
           vm.function("SceneRequestScene", request_scene) &&
           vm.function("SceneRequestStatus", request_status) &&
           vm.function("SceneRequestAction", request_action) &&
           vm.function("SceneFindNode", find_node) && vm.function("SceneSetPaused", set_paused) &&
           vm.function("SceneGetPaused", get_paused) &&
           vm.function("SceneInstantiateModel", instantiate_model) &&
           vm.function("SceneInstantiatePrefab", instantiate_prefab) &&
           vm.function("SceneValid", valid) && vm.function("SceneDestroy", destroy) &&
           vm.function("ActiveCamera", active_camera);
}

} // namespace engine::plugins::script::bindings
