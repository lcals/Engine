#include "plugins/runtime/script/bindings/node_binding.hpp"

#include "plugins/runtime/script/bindings/binding_context.hpp"
#include "runtime/scene/scene.hpp"

namespace engine::plugins::script::bindings
{
namespace
{
constexpr std::string_view javascript = R"js(
export class Node {
    constructor(token, scene, node) { attach(this, token, scene, node); }
    IsValid() { const h = handles.get(this); return native.NodeValid(h.scene, h.node); }
    Destroy() { const h = handles.get(this); return native.NodeDestroy(h.scene, h.node); }
    SetParent(parent) {
        const h = handles.get(this);
        if (parent === null) return native.NodeParent(h.scene, h.node, 0n);
        const other = handles.get(parent);
        return parent instanceof Node && other?.scene === h.scene &&
            native.NodeParent(h.scene, h.node, other.node);
    }
    AddComponent(type) {
        const factory = componentTypes.get(type);
        if (factory === undefined) return null;
        const h = handles.get(this);
        return factory.add(h.scene, h.node) ? new type(key, h.scene, h.node) : null;
    }
    GetComponent(type) {
        const factory = componentTypes.get(type);
        if (factory === undefined) return null;
        const h = handles.get(this);
        return native.ComponentValid(h.scene, h.node, factory.kind) ? new type(key, h.scene, h.node) : null;
    }
}
Object.freeze(Node.prototype);
Object.freeze(Node);
)js";

void create(const Arguments& arguments)
{
    arguments.GetReturnValue().SetNull();
    auto* scene = BindingContext::scene(arguments);
    std::string name;
    if (scene == nullptr || arguments.Length() != 2 || !BindingContext::text(arguments, 1, name))
    {
        return;
    }
    if (auto* node = scene->create_node(name); node != nullptr)
    {
        arguments.GetReturnValue().Set(
            v8::BigInt::NewFromUnsigned(arguments.GetIsolate(), node->id()));
    }
}

void valid(const Arguments& arguments)
{
    arguments.GetReturnValue().Set(arguments.Length() == 2 &&
                                   BindingContext::node(arguments) != nullptr);
}

void destroy(const Arguments& arguments)
{
    auto* node = BindingContext::node(arguments);
    arguments.GetReturnValue().Set(arguments.Length() == 2 && node != nullptr &&
                                   node->scene().destroy_node(node->id()));
}

void set_parent(const Arguments& arguments)
{
    arguments.GetReturnValue().Set(false);
    auto* node = BindingContext::node(arguments);
    std::uint64_t id = 0;
    if (node == nullptr || arguments.Length() != 3 || !BindingContext::identifier(arguments, 2, id))
    {
        return;
    }
    auto* parent = id == 0 ? nullptr : node->scene().find_node(id);
    arguments.GetReturnValue().Set((id == 0 || parent != nullptr) && node->set_parent(parent));
}
} // namespace

std::string_view NodeBinding::name() const noexcept
{
    return "Node";
}

std::string_view NodeBinding::source() const noexcept
{
    return javascript;
}

bool NodeBinding::register_functions(v8::Local<v8::Context> context,
                                     v8::Local<v8::Object> native) const
{
    return BindingContext::function(context, native, "NodeCreate", create) &&
           BindingContext::function(context, native, "NodeValid", valid) &&
           BindingContext::function(context, native, "NodeDestroy", destroy) &&
           BindingContext::function(context, native, "NodeParent", set_parent);
}

} // namespace engine::plugins::script::bindings
