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
    arguments.GetReturnValue().Set(arguments.Length() == 2 && node != nullptr &&
                                   node->add_component<MeshRenderer>() != nullptr);
}
void mesh(const Arguments& arguments)
{
    auto* node = BindingContext::node(arguments);
    auto* mesh = node == nullptr ? nullptr : node->get_component<MeshRenderer>();
    std::string path;
    arguments.GetReturnValue().Set(mesh != nullptr && arguments.Length() == 3 &&
                                   BindingContext::text(arguments, 2, path) &&
                                   mesh->set_mesh(path));
}

void material_count(const Arguments& arguments)
{
    auto* node = BindingContext::node(arguments);
    auto* mesh = node == nullptr ? nullptr : node->get_component<MeshRenderer>();
    const auto count = mesh != nullptr && arguments.Length() == 2 ? mesh->material_count() : 0;
    arguments.GetReturnValue().Set(static_cast<std::uint32_t>(count));
}

void set_material_count(const Arguments& arguments)
{
    auto* node = BindingContext::node(arguments);
    auto* mesh = node == nullptr ? nullptr : node->get_component<MeshRenderer>();
    arguments.GetReturnValue().Set(
        mesh != nullptr && arguments.Length() == 3 && arguments[2]->IsUint32() &&
        mesh->set_material_count(arguments[2].As<v8::Uint32>()->Value()));
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

bool MeshRendererBinding::register_functions(v8::Local<v8::Context> context,
                                             v8::Local<v8::Object> native) const
{
    return BindingContext::integer(context, native, "MeshRendererKind",
                                   static_cast<int>(MeshRenderer::component_type)) &&
           BindingContext::function(context, native, "MeshRendererAdd", add) &&
           BindingContext::function(context, native, "MeshRendererMesh", mesh) &&
           BindingContext::function(context, native, "MeshRendererMaterialCount", material_count) &&
           BindingContext::function(context, native, "MeshRendererSetMaterialCount",
                                    set_material_count);
}

} // namespace engine::plugins::script::bindings
