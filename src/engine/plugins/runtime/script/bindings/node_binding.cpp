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
    get id() { return handles.get(this).node; }
    get sceneId() { return handles.get(this).scene; }
    IsValid() { const h = handles.get(this); return native.NodeValid(h.scene, h.node); }
    get enabled() { const h = handles.get(this); return native.NodeLocalEnabled(h.scene, h.node); }
    get active() { const h = handles.get(this); return native.NodeActive(h.scene, h.node); }
    get visible() { const h = handles.get(this); return native.NodeVisible(h.scene, h.node); }
    get visibleInHierarchy() {
        const h = handles.get(this); return native.NodeVisibleInHierarchy(h.scene, h.node);
    }
    SetVisible(visible) {
        const h = handles.get(this); return native.NodeVisibleSet(h.scene, h.node, visible);
    }
    SetEnabled(enabled) {
        const h = handles.get(this); return native.NodeEnabled(h.scene, h.node, enabled);
    }
    SetPersistent(value) {
        const h = handles.get(this);
        return native.NodePersistentSet(h.scene, h.node, value);
    }
    IsPersistent() { const h = handles.get(this); return native.NodePersistent(h.scene, h.node); }
    Destroy() { const h = handles.get(this); return native.NodeDestroy(h.scene, h.node); }
    SetParent(parent) {
        const h = handles.get(this);
        if (parent === null) return native.NodeParent(h.scene, h.node, 0n);
        const other = handles.get(parent);
        return parent instanceof Node && other?.scene === h.scene &&
            native.NodeParent(h.scene, h.node, other.node);
    }
    FindChild(path) {
        const h = handles.get(this);
        const node = native.NodeFindChild(h.scene, h.node, path);
        return node === null ? null : new Node(key, h.scene, node);
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
    arguments.set_return({});
    auto* scene = BindingContext::scene(arguments);
    std::string name;
    if (scene == nullptr || arguments.count() != 2 || !BindingContext::text(arguments, 1, name))
    {
        return;
    }
    if (auto* node = scene->create_node(name); node != nullptr)
    {
        arguments.set_return(node->id());
    }
}

void valid(const Arguments& arguments)
{
    arguments.set_return(arguments.count() == 2 && BindingContext::node(arguments) != nullptr);
}

void destroy(const Arguments& arguments)
{
    auto* node = BindingContext::node(arguments);
    arguments.set_return(arguments.count() == 2 && node != nullptr &&
                         node->scene().destroy_node(node->id()));
}

void set_persistent(const Arguments& arguments)
{
    auto* node = BindingContext::node(arguments);
    arguments.set_return(arguments.count() == 3 && node && arguments.is_boolean(2) &&
                         node->set_persistent(arguments.boolean(2)));
}

void persistent(const Arguments& arguments)
{
    const auto* node = BindingContext::node(arguments);
    arguments.set_return(arguments.count() == 2 && node && node->persistent());
}

void set_parent(const Arguments& arguments)
{
    arguments.set_return(false);
    auto* node = BindingContext::node(arguments);
    std::uint64_t id = 0;
    if (node == nullptr || arguments.count() != 3 || !BindingContext::identifier(arguments, 2, id))
    {
        return;
    }
    auto* parent = id == 0 ? nullptr : node->scene().find_node(id);
    arguments.set_return((id == 0 || parent != nullptr) && node->set_parent(parent));
}

void find_child(const Arguments& arguments)
{
    arguments.set_return({});
    auto* current = BindingContext::node(arguments);
    std::string path;
    if (current == nullptr || arguments.count() != 3 || !BindingContext::text(arguments, 2, path) ||
        path.empty() || path.front() == '/' || path.back() == '/')
    {
        return;
    }
    std::string_view remaining(path);
    while (!remaining.empty())
    {
        const auto separator = remaining.find('/');
        const auto segment = remaining.substr(0, separator);
        if (segment.empty() || segment == "." || segment == "..")
        {
            return;
        }
        Node* found = nullptr;
        for (const auto& candidate : current->scene().nodes())
        {
            if (candidate->parent() != current || candidate->name() != segment)
            {
                continue;
            }
            if (found != nullptr)
            {
                return;
            }
            found = candidate.get();
        }
        if (found == nullptr)
        {
            return;
        }
        current = found;
        if (separator == std::string_view::npos)
        {
            arguments.set_return(current->id());
            return;
        }
        remaining.remove_prefix(separator + 1);
    }
}
void state(const Arguments& arguments)
{
    arguments.set_return({});
    const auto* target = BindingContext::node(arguments);
    if (target == nullptr || arguments.count() != 2)
    {
        return;
    }
    switch (arguments.data())
    {
    case 0:
        arguments.set_return(target->enabled());
        break;
    case 1:
        arguments.set_return(target->active());
        break;
    case 2:
        arguments.set_return(target->visible());
        break;
    case 3:
        arguments.set_return(target->visible_in_hierarchy());
        break;
    }
}

void set_visible(const Arguments& arguments)
{
    arguments.set_return(false);
    auto* target = BindingContext::node(arguments);
    if (target != nullptr && arguments.count() == 3 && arguments.is_boolean(2))
    {
        target->set_visible(arguments.boolean(2));
        arguments.set_return(true);
    }
}

void enabled(const Arguments& arguments)
{
    arguments.set_return(false);
    auto* node = BindingContext::node(arguments);
    if (node != nullptr && arguments.count() == 3 && arguments.is_boolean(2))
    {
        node->set_enabled(arguments.boolean(2));
        arguments.set_return(true);
    }
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

bool NodeBinding::register_functions(vm::ScriptVm& vm) const
{
    return vm.function("NodeCreate", create) && vm.function("NodeValid", valid) &&
           vm.function("NodePersistentSet", set_persistent) &&
           vm.function("NodePersistent", persistent) && vm.function("NodeDestroy", destroy) &&
           vm.function("NodeParent", set_parent) && vm.function("NodeFindChild", find_child) &&
           vm.function("NodeLocalEnabled", state, 0) && vm.function("NodeActive", state, 1) &&
           vm.function("NodeVisible", state, 2) &&
           vm.function("NodeVisibleInHierarchy", state, 3) &&
           vm.function("NodeVisibleSet", set_visible) && vm.function("NodeEnabled", enabled);
}

} // namespace engine::plugins::script::bindings
