#include "runtime/script/bindings/scene_binding.hpp"

#include "runtime/scene/camera.hpp"
#include "runtime/scene/scene.hpp"
#include "runtime/script/bindings/binding_context.hpp"
#include "runtime/world/world_system.hpp"

namespace engine::script_bindings
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
    IsValid() { return native.SceneValid(handles.get(this).scene); }
    Destroy() { return native.SceneDestroy(handles.get(this).scene); }
    CreateNode(name) {
        const scene = handles.get(this).scene;
        const node = native.NodeCreate(scene, name);
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

void valid(const Arguments& arguments)
{
    arguments.GetReturnValue().Set(arguments.Length() == 1 &&
                                   BindingContext::scene(arguments) != nullptr);
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
           BindingContext::function(context, native, "SceneValid", valid) &&
           BindingContext::function(context, native, "SceneDestroy", destroy) &&
           BindingContext::function(context, native, "ActiveCamera", active_camera);
}

} // namespace engine::script_bindings
