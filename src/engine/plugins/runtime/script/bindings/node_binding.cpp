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
           vm.function("NodeDestroy", destroy) && vm.function("NodeParent", set_parent) &&
           vm.function("NodeFindChild", find_child);
}

} // namespace engine::plugins::script::bindings
