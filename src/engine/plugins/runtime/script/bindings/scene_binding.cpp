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

void find_node(const Arguments& arguments)
{
    arguments.set_return({});
    auto* scene = BindingContext::scene(arguments);
    std::string name;
    if (scene == nullptr || arguments.count() != 2 || !BindingContext::text(arguments, 1, name))
    {
        return;
    }
    for (const auto& node : scene->nodes())
    {
        if (node->name() == name)
        {
            arguments.set_return(node->id());
            return;
        }
    }
}

void valid(const Arguments& arguments)
{
    arguments.set_return(arguments.count() == 1 && BindingContext::scene(arguments) != nullptr);
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
    if (const auto* root = scene->instantiate_model(path, name))
    {
        arguments.set_return(root->id());
    }
}

void destroy(const Arguments& arguments)
{
    arguments.set_return(false);
    if (arguments.count() == 1 && BindingContext::scene(arguments) != nullptr)
    {
        auto* binding = BindingContext::current(arguments);
        binding->world()->destroy_scene();
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
        arguments.set_return(scene->set_active_camera(nullptr));
        return;
    }
    auto* node = scene->find_node(id);
    auto* camera = node == nullptr ? nullptr : node->get_component<Camera>();
    arguments.set_return(camera != nullptr && scene->set_active_camera(camera));
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
           vm.function("SceneFindNode", find_node) &&
           vm.function("SceneInstantiateModel", instantiate_model) &&
           vm.function("SceneValid", valid) && vm.function("SceneDestroy", destroy) &&
           vm.function("ActiveCamera", active_camera);
}

} // namespace engine::plugins::script::bindings
