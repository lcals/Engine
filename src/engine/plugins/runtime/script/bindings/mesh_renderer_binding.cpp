#include "plugins/runtime/script/bindings/mesh_renderer_binding.hpp"

#include "plugins/runtime/script/bindings/binding_context.hpp"
#include "runtime/scene/mesh_renderer.hpp"
#include "runtime/scene/node.hpp"

namespace engine::plugins::script::bindings
{
namespace
{
constexpr std::string_view javascript = R"js(
export class MeshRenderer extends Component {
    constructor(token, scene, node) { super(token, scene, node, native.MeshRendererKind); }
    SetMesh(path) {
        const h = handles.get(this); return native.MeshRendererMesh(h.scene, h.node, path);
    }
    get castShadows() {
        const h = handles.get(this); return native.MeshRendererCastShadows(h.scene, h.node);
    }
    SetCastShadows(enabled) {
        const h = handles.get(this); return native.MeshRendererSetCastShadows(h.scene, h.node, enabled);
    }
    get materialCount() {
        const h = handles.get(this); return native.MeshRendererMaterialCount(h.scene, h.node);
    }
    SetMaterialCount(count) {
        const h = handles.get(this); return native.MeshRendererSetMaterialCount(h.scene, h.node, count);
    }
    GetMaterial(slot) {
        const h = handles.get(this);
        return native.MaterialValid(h.scene, h.node, slot) ? new Material(key, h.scene, h.node, slot) : null;
    }
    get material() {
        const h = handles.get(this); return new Material(key, h.scene, h.node);
    }
}
componentTypes.set(MeshRenderer, { add: native.MeshRendererAdd, kind: native.MeshRendererKind });
Object.freeze(MeshRenderer.prototype);
Object.freeze(MeshRenderer);
)js";

void add(const Arguments& arguments)
{
    auto* node = BindingContext::node(arguments);
    arguments.set_return(arguments.count() == 2 && node != nullptr &&
                         node->add_component<MeshRenderer>() != nullptr);
}
void mesh(const Arguments& arguments)
{
    auto* node = BindingContext::node(arguments);
    auto* mesh = node == nullptr ? nullptr : node->get_component<MeshRenderer>();
    std::string path;
    arguments.set_return(mesh != nullptr && arguments.count() == 3 &&
                         BindingContext::text(arguments, 2, path) && mesh->set_mesh(path));
}

void material_count(const Arguments& arguments)
{
    auto* node = BindingContext::node(arguments);
    auto* mesh = node == nullptr ? nullptr : node->get_component<MeshRenderer>();
    const auto count = mesh != nullptr && arguments.count() == 2 ? mesh->material_count() : 0;
    arguments.set_return(static_cast<std::uint32_t>(count));
}

void cast_shadows(const Arguments& arguments)
{
    auto* node = BindingContext::node(arguments);
    auto* mesh = node != nullptr ? node->get_component<MeshRenderer>() : nullptr;
    arguments.set_return(mesh != nullptr && arguments.count() == 2 && mesh->cast_shadows());
}

void set_cast_shadows(const Arguments& arguments)
{
    arguments.set_return(false);
    auto* node = BindingContext::node(arguments);
    auto* mesh = node != nullptr ? node->get_component<MeshRenderer>() : nullptr;
    if (mesh != nullptr && arguments.count() == 3 && arguments.is_boolean(2))
    {
        mesh->set_cast_shadows(arguments.boolean(2));
        arguments.set_return(true);
    }
}

void set_material_count(const Arguments& arguments)
{
    auto* node = BindingContext::node(arguments);
    auto* mesh = node == nullptr ? nullptr : node->get_component<MeshRenderer>();
    arguments.set_return(mesh != nullptr && arguments.count() == 3 && arguments.is_uint32(2) &&
                         mesh->set_material_count(arguments.unsigned_integer(2)));
}
} // namespace

std::string_view MeshRendererBinding::name() const noexcept
{
    return "MeshRenderer";
}

std::string_view MeshRendererBinding::source() const noexcept
{
    return javascript;
}

bool MeshRendererBinding::register_functions(vm::ScriptVm& vm) const
{
    return vm.integer("MeshRendererKind", static_cast<int>(MeshRenderer::component_type)) &&
           vm.function("MeshRendererAdd", add) && vm.function("MeshRendererMesh", mesh) &&
           vm.function("MeshRendererCastShadows", cast_shadows) &&
           vm.function("MeshRendererSetCastShadows", set_cast_shadows) &&
           vm.function("MeshRendererMaterialCount", material_count) &&
           vm.function("MeshRendererSetMaterialCount", set_material_count);
}

} // namespace engine::plugins::script::bindings
