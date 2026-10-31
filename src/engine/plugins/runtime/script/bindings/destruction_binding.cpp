#if defined(ENGINE_HAS_DESTRUCTION_PLUGIN) && ENGINE_HAS_DESTRUCTION_PLUGIN

#include "plugins/runtime/script/bindings/destruction_binding.hpp"

#include "plugins/runtime/destruction/destruction_component.hpp"
#include "plugins/runtime/script/bindings/binding_context.hpp"
#include "runtime/scene/collider.hpp"
#include "runtime/scene/node.hpp"
#include "runtime/scene/scene.hpp"

namespace engine::plugins::script::bindings
{
namespace
{
constexpr std::string_view javascript = R"js(
export class DestructionComponent extends Component {
    constructor(token, scene, node) { super(token, scene, node, native.DestructionKind); }
    DamageSphere(x, y, z, radius, damage, impulse = 0) {
        const h = handles.get(this);
        return native.DestructionDamage(h.scene, h.node, x, y, z, radius, damage, impulse);
    }
    RefreshSupport(support, maxGap, minCoverage) {
        const h = handles.get(this), other = handles.get(support);
        if (!other || other.kind !== native.ColliderKind || other.scene !== h.scene) return false;
        return native.DestructionSupport(h.scene, h.node, other.node, maxGap, minCoverage);
    }
    OwnsFragment(nodeId) {
        const h = handles.get(this); return native.DestructionOwns(h.scene, h.node, nodeId);
    }
    Reset() { const h = handles.get(this); return native.DestructionReset(h.scene, h.node); }
    get ready() { const h = handles.get(this); return native.DestructionReady(h.scene, h.node); }
    get revision() { const h = handles.get(this); return native.DestructionRevision(h.scene, h.node); }
    get chunkCount() { const h = handles.get(this); return native.DestructionChunks(h.scene, h.node); }
    get groupCount() { const h = handles.get(this); return native.DestructionGroups(h.scene, h.node); }
    get dynamicGroups() { const h = handles.get(this); return native.DestructionDynamic(h.scene, h.node); }
    get lastError() { const h = handles.get(this); return native.DestructionError(h.scene, h.node); }
}
componentTypes.set(DestructionComponent, { add: native.DestructionAdd, kind: native.DestructionKind });
Object.freeze(DestructionComponent.prototype);
Object.freeze(DestructionComponent);
)js";

destruction::DestructionComponent* component(const Arguments& arguments, int count)
{
    arguments.set_return(false);
    auto* node = BindingContext::node(arguments);
    return node && arguments.count() == count
               ? node->get_component<destruction::DestructionComponent>()
               : nullptr;
}
void add(const Arguments& arguments)
{
    auto* node = BindingContext::node(arguments);
    arguments.set_return(arguments.count() == 2 && node &&
                         node->add_component<destruction::DestructionComponent>() != nullptr);
}
void damage(const Arguments& arguments)
{
    auto* value = component(arguments, 8);
    double x = 0;
    double y = 0;
    double z = 0;
    float radius = 0;
    float amount = 0;
    float impulse = 0;
    if (value && BindingContext::number(arguments, 2, x) &&
        BindingContext::number(arguments, 3, y) && BindingContext::number(arguments, 4, z) &&
        BindingContext::scalar(arguments, 5, radius) &&
        BindingContext::scalar(arguments, 6, amount) &&
        BindingContext::scalar(arguments, 7, impulse))
    {
        arguments.set_return(value->damage_sphere({x, y, z}, radius, amount, impulse));
    }
}
void support(const Arguments& arguments)
{
    auto* value = component(arguments, 5);
    std::uint64_t node_id = 0;
    float max_gap = 0;
    float min_coverage = 0;
    if (!value || !BindingContext::identifier(arguments, 2, node_id) ||
        !BindingContext::scalar(arguments, 3, max_gap) ||
        !BindingContext::scalar(arguments, 4, min_coverage))
    {
        return;
    }
    auto* node = value->node().scene().find_node(node_id);
    const auto* collider = node ? node->get_component<Collider>() : nullptr;
    arguments.set_return(collider && value->refresh_support(*collider, max_gap, min_coverage));
}
void owns_fragment(const Arguments& arguments)
{
    const auto* value = component(arguments, 3);
    std::uint64_t node_id = 0;
    arguments.set_return(value && BindingContext::identifier(arguments, 2, node_id) &&
                         value->owns_fragment(node_id));
}
void reset(const Arguments& arguments)
{
    auto* value = component(arguments, 2);
    arguments.set_return(value && value->reset());
}
void statistic(const Arguments& arguments)
{
    const auto* value = component(arguments, 2);
    arguments.set_return({});
    if (!value)
    {
        return;
    }
    switch (arguments.data())
    {
    case 0:
        arguments.set_return(value->ready());
        break;
    case 1:
        arguments.set_return(value->revision());
        break;
    case 2:
        arguments.set_return(static_cast<double>(value->chunk_count()));
        break;
    case 3:
        arguments.set_return(static_cast<double>(value->group_count()));
        break;
    case 4:
        arguments.set_return(static_cast<double>(value->dynamic_groups()));
        break;
    case 5:
        arguments.set_return(value->last_error());
        break;
    }
}
} // namespace

std::string_view DestructionBinding::name() const noexcept
{
    return "DestructionComponent";
}
std::string_view DestructionBinding::source() const noexcept
{
    return javascript;
}
bool DestructionBinding::register_functions(vm::ScriptVm& vm) const
{
    return vm.integer("DestructionKind",
                      static_cast<int>(destruction::DestructionComponent::component_type)) &&
           vm.function("DestructionAdd", add) && vm.function("DestructionDamage", damage) &&
           vm.function("DestructionSupport", support) &&
           vm.function("DestructionOwns", owns_fragment) &&
           vm.function("DestructionReset", reset) &&
           vm.function("DestructionReady", statistic, 0) &&
           vm.function("DestructionRevision", statistic, 1) &&
           vm.function("DestructionChunks", statistic, 2) &&
           vm.function("DestructionGroups", statistic, 3) &&
           vm.function("DestructionDynamic", statistic, 4) &&
           vm.function("DestructionError", statistic, 5);
}
} // namespace engine::plugins::script::bindings

#endif
