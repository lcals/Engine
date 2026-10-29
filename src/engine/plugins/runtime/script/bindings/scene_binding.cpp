#include "plugins/runtime/script/bindings/scene_binding.hpp"

#include "plugins/runtime/script/bindings/binding_context.hpp"
#include "runtime/scene/camera.hpp"
#include "runtime/scene/scene.hpp"
#include "runtime/world/world_system.hpp"

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
    FindNode(name) {
        const scene = handles.get(this).scene;
        const node = native.SceneFindNode(scene, name);
        return node === null ? null : new Node(key, scene, node);
    }
    IsValid() { return native.SceneValid(handles.get(this).scene); }
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
    SetActiveCamera(camera) {
        const scene = handles.get(this).scene;
        if (camera === null) return native.ActiveCamera(scene, 0n);
        const other = handles.get(camera);
        return camera instanceof Camera && other?.scene === scene &&
            native.ActiveCamera(scene, other.node);
    }
}
Object.freeze(Scene.prototype);
Object.freeze(Scene);
)js";

void create(const Arguments& arguments)
{
    arguments.GetReturnValue().SetNull();
    auto* binding = BindingContext::current(arguments);
    std::string name;
    if (binding == nullptr || binding->world() == nullptr || !binding->world()->is_initialized() ||
        arguments.Length() != 1 || !BindingContext::text(arguments, 0, name))
    {
        return;
    }
    if (auto* scene = binding->world()->create_scene(name); scene != nullptr)
    {
        binding->set_scene_id(scene->id());
        arguments.GetReturnValue().Set(
            v8::BigInt::NewFromUnsigned(arguments.GetIsolate(), scene->id()));
    }
}

void load(const Arguments& arguments)
{
    arguments.GetReturnValue().SetNull();
    auto* binding = BindingContext::current(arguments);
    std::string path;
    if (binding == nullptr || binding->world() == nullptr || !binding->world()->is_initialized() ||
        arguments.Length() != 1 || !BindingContext::text(arguments, 0, path))
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
        arguments.GetReturnValue().Set(
            v8::BigInt::NewFromUnsigned(arguments.GetIsolate(), scene->id()));
    }
}

void find_node(const Arguments& arguments)
{
    arguments.GetReturnValue().SetNull();
    auto* scene = BindingContext::scene(arguments);
    std::string name;
    if (scene == nullptr || arguments.Length() != 2 || !BindingContext::text(arguments, 1, name))
    {
        return;
    }
    for (const auto& node : scene->nodes())
    {
        if (node->name() == name)
        {
            arguments.GetReturnValue().Set(
                v8::BigInt::NewFromUnsigned(arguments.GetIsolate(), node->id()));
            return;
        }
    }
}

void valid(const Arguments& arguments)
{
    arguments.GetReturnValue().Set(arguments.Length() == 1 &&
                                   BindingContext::scene(arguments) != nullptr);
}

void instantiate_model(const Arguments& arguments)
{
    arguments.GetReturnValue().SetNull();
    auto* scene = BindingContext::scene(arguments);
    std::string path;
    std::string name;
    if (!scene || arguments.Length() != 3 || !BindingContext::text(arguments, 1, path) ||
        !BindingContext::text(arguments, 2, name))
    {
        return;
    }
    if (const auto* root = scene->instantiate_model(path, name))
    {
        arguments.GetReturnValue().Set(
            v8::BigInt::NewFromUnsigned(arguments.GetIsolate(), root->id()));
    }
}

void destroy(const Arguments& arguments)
{
    arguments.GetReturnValue().Set(false);
    if (arguments.Length() == 1 && BindingContext::scene(arguments) != nullptr)
    {
        auto* binding = BindingContext::current(arguments);
        binding->world()->destroy_scene();
        binding->set_scene_id(0);
        arguments.GetReturnValue().Set(true);
    }
}

void active_camera(const Arguments& arguments)
{
    arguments.GetReturnValue().Set(false);
    auto* scene = BindingContext::scene(arguments);
    std::uint64_t id = 0;
    if (scene == nullptr || arguments.Length() != 2 ||
        !BindingContext::identifier(arguments, 1, id))
    {
        return;
    }
    if (id == 0)
    {
        arguments.GetReturnValue().Set(scene->set_active_camera(nullptr));
        return;
    }
    auto* node = scene->find_node(id);
    auto* camera = node == nullptr ? nullptr : node->get_component<Camera>();
    arguments.GetReturnValue().Set(camera != nullptr && scene->set_active_camera(camera));
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

bool SceneBinding::register_functions(v8::Local<v8::Context> context,
                                      v8::Local<v8::Object> native) const
{
    return BindingContext::function(context, native, "SceneCreate", create) &&
           BindingContext::function(context, native, "SceneLoad", load) &&
           BindingContext::function(context, native, "SceneFindNode", find_node) &&
           BindingContext::function(context, native, "SceneInstantiateModel", instantiate_model) &&
           BindingContext::function(context, native, "SceneValid", valid) &&
           BindingContext::function(context, native, "SceneDestroy", destroy) &&
           BindingContext::function(context, native, "ActiveCamera", active_camera);
}

} // namespace engine::plugins::script::bindings
