#include "runtime/script/bindings/mesh_renderer_binding.hpp"

#include "runtime/scene/mesh_renderer.hpp"
#include "runtime/scene/node.hpp"
#include "runtime/script/bindings/binding_context.hpp"

namespace engine::script_bindings
{
namespace
{
constexpr std::string_view javascript = R"js(
export class MeshRenderer extends Component {
    constructor(token, scene, node) { super(token, scene, node, native.MeshRendererKind); }
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
           BindingContext::function(context, native, "MeshRendererAdd", add);
}

} // namespace engine::script_bindings
