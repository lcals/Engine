#include "plugins/runtime/script/bindings/physics_binding.hpp"

#include "plugins/runtime/script/bindings/binding_context.hpp"
#include "runtime/physics/physics_system.hpp"
#include "runtime/scene/character_motor.hpp"
#include "runtime/scene/collider.hpp"
#include "runtime/scene/scene.hpp"
#include "runtime/world/world_system.hpp"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

namespace engine::plugins::script::bindings
{
namespace
{
constexpr std::string_view javascript = R"js(
function physicsOptions(options) {
    if (options === null || typeof options !== 'object' || Array.isArray(options)) return null;
    const ignored = options.ignoreNode;
    if (ignored !== undefined && ignored !== null && !(ignored instanceof Node)) return null;
    const ignore = ignored == null ? null : handles.get(ignored);
    if (ignored != null && !ignore) return null;
    const option = (name, fallback) => options[name] === undefined ? fallback : options[name];
    return [ignore?.scene ?? 0n, ignore?.node ?? 0n,
        option('ignoreDescendants', true), option('includeStatic', true),
        option('includeKinematic', true), option('includeDynamic', true),
        option('includeSensors', false), option('includeCharacters', true)];
}
function physicsObject(value) {
    value.node = value.nodeId === 0n ? null : new Node(key, value.sceneId, value.nodeId);
    return Object.freeze(value);
}
function physicsQuery(query, origin, displacement, radius, options) {
    const filter = physicsOptions(options);
    if (filter === null) return null;
    const result = query(origin?.x, origin?.y, origin?.z,
        displacement?.x, displacement?.y, displacement?.z, radius, ...filter);
    if (result === null) return null;
    Object.freeze(result.point);
    Object.freeze(result.normal);
    return physicsObject(result);
}
function physicsOverlap(query, center, shape, options) {
    const filter = physicsOptions(options);
    if (filter === null) return null;
    const result = query(center?.x, center?.y, center?.z, shape?.x, shape?.y, shape?.z, ...filter);
    return result === null ? null : Object.freeze(result.map(physicsObject));
}
const physicsSubscriptions = new Map();
const physicsTargets = new Map();
let physicsSubscriptionSequence = 0;
let physicsContactFrame = 0n;
let physicsDispatching = false;
function physicsObjectKey(object) {
    return `${object.sceneId}:${object.nodeId}:${object.kind}:${object.instanceId}`;
}
function physicsUnsubscribe(id) {
    const target = physicsSubscriptions.get(id);
    if (target === undefined) return false;
    physicsSubscriptions.delete(id);
    target.subscriptions.delete(id);
    if (target.subscriptions.size === 0) physicsTargets.delete(physicsObjectKey(target.object));
    return true;
}
function physicsPruneSubscriptions() {
    for (const target of physicsTargets.values()) {
        if (!Physics.IsObjectValid(target.object)) {
            for (const id of target.subscriptions.keys()) physicsUnsubscribe(id);
        }
    }
}
function physicsSubscribe(node, callback, phase, isTrigger) {
    const handle = handles.get(node);
    if (!(node instanceof Node) || handle === undefined || typeof callback !== 'function' ||
        physicsSubscriptionSequence === Number.MAX_SAFE_INTEGER) return 0;
    const value = native.PhysicsContactTarget(handle.scene, handle.node);
    if (value === null) return 0;
    const key = physicsObjectKey(value.object);
    let target = physicsTargets.get(key);
    if (target === undefined) {
        target = {object: physicsObject(value.object), subscriptions: new Map()};
        physicsTargets.set(key, target);
    }
    const id = ++physicsSubscriptionSequence;
    target.subscriptions.set(id, {callback, phase, isTrigger, afterFrame: value.frame});
    physicsSubscriptions.set(id, target);
    return id;
}
function physicsDispatchContact(event, self, other, reverse, sequence) {
    const target = physicsTargets.get(physicsObjectKey(self));
    if (target === undefined) return;
    let contact;
    for (const [id, subscription] of target.subscriptions) {
        if (!Physics.IsObjectValid(self)) break;
        if (id > sequence || event.frame <= subscription.afterFrame ||
            event.phase !== subscription.phase || event.isTrigger !== subscription.isTrigger) continue;
        contact ??= Object.freeze({
            frame: event.frame, step: event.step, phase: event.phase,
            self, other, point: event.point, isTrigger: event.isTrigger,
            normal: reverse ? Object.freeze({x:-event.normal.x,y:-event.normal.y,z:-event.normal.z}) :
                event.normal
        });
        if (subscription.callback(contact) instanceof Promise) {
            throw new TypeError('Physics event callbacks must be synchronous');
        }
    }
}
function dispatchPhysicsEvents() {
    if (physicsDispatching) return;
    physicsPruneSubscriptions();
    if (physicsSubscriptions.size === 0) return;
    physicsDispatching = true;
    try {
        const sequence = physicsSubscriptionSequence;
        const events = Physics.GetContacts();
        const previousFrame = physicsContactFrame;
        if (events.length !== 0) physicsContactFrame = events[events.length - 1].frame;
        for (const event of events) {
            if (event.frame <= previousFrame) continue;
            physicsDispatchContact(event, event.a, event.b, false, sequence);
            physicsDispatchContact(event, event.b, event.a, true, sequence);
        }
    } finally {
        physicsPruneSubscriptions();
        physicsDispatching = false;
    }
}
function clearPhysicsSubscriptions() {
    physicsSubscriptions.clear();
    physicsTargets.clear();
    physicsContactFrame = 0n;
}
bindingEventHooks.push(dispatchPhysicsEvents);
bindingCleanupHooks.push(clearPhysicsSubscriptions);
export class Physics {
    constructor() { throw new TypeError('Physics is static'); }
    static GetImpacts() {
        return native.PhysicsGetImpacts().map(impact => {
            physicsObject(impact.a);
            physicsObject(impact.b);
            Object.freeze(impact.point);
            Object.freeze(impact.normal);
            return Object.freeze(impact);
        });
    }
    static GetContacts() {
        return Object.freeze(native.PhysicsGetContacts().map(event => {
            physicsObject(event.a);
            physicsObject(event.b);
            Object.freeze(event.point);
            Object.freeze(event.normal);
            return Object.freeze(event);
        }));
    }
    static OnCollisionEnter(node, callback) { return physicsSubscribe(node, callback, 'begin', false); }
    static OnCollisionStay(node, callback) { return physicsSubscribe(node, callback, 'stay', false); }
    static OnCollisionExit(node, callback) { return physicsSubscribe(node, callback, 'end', false); }
    static OnTriggerEnter(node, callback) { return physicsSubscribe(node, callback, 'begin', true); }
    static OnTriggerStay(node, callback) { return physicsSubscribe(node, callback, 'stay', true); }
    static OnTriggerExit(node, callback) { return physicsSubscribe(node, callback, 'end', true); }
    static Off(subscription) { return physicsUnsubscribe(subscription); }
    static CastSphere(x, y, z, dx, dy, dz, radius) {
        return native.PhysicsCastSphere(x, y, z, dx, dy, dz, radius);
    }
    static Raycast(origin, displacement, options = {}) {
        return physicsQuery(native.PhysicsQueryRay, origin, displacement, 0, options);
    }
    static SphereCast(origin, displacement, radius, options = {}) {
        return physicsQuery(native.PhysicsQuerySphere, origin, displacement, radius, options);
    }
    static OverlapSphere(center, radius, options = {}) {
        return physicsOverlap(native.PhysicsOverlapSphere, center, {x:radius,y:0,z:0}, options);
    }
    static OverlapBox(center, halfExtents, options = {}) {
        return physicsOverlap(native.PhysicsOverlapBox, center, halfExtents, options);
    }
    static IsObjectValid(object) {
        return native.PhysicsObjectValid(object?.sceneId, object?.nodeId, object?.kind,
                                         object?.instanceId);
    }
    static IsHitValid(hit) {
        return hit?.hit === true && Physics.IsObjectValid(hit);
    }
}
Object.freeze(Physics.prototype);
Object.freeze(Physics);
)js";

vm::NativeValue vector(const Vec3& value)
{
    return vm::NativeValue::Object{{"x", value.x()}, {"y", value.y()}, {"z", value.z()}};
}

vm::NativeValue::Object object_value(const PhysicsObject& object)
{
    return {{"sceneId", object.scene_id},
            {"nodeId", object.node_id},
            {"kind", object.kind == PhysicsObjectKind::character ? "character" : "collider"},
            {"instanceId", object.instance_id}};
}

bool query_allowed(const BindingContext& binding)
{
    const auto* world = binding.world();
    if (!world || !world->is_initialized())
    {
        return false;
    }
    if (world->scene() && world->scene()->id() == binding.scene_id())
    {
        return true;
    }
    return std::any_of(world->scenes().begin(), world->scenes().end(),
                       [&](const auto& scene)
                       {
                           return !scene->nodes().empty() &&
                                  binding.find_node(scene->id(), scene->nodes().front()->id());
                       });
}

void impacts(const Arguments& arguments)
{
    vm::NativeValue::Array output;
    auto* binding = BindingContext::current(arguments);
    auto* physics = binding ? binding->physics() : nullptr;
    if (arguments.count() == 0 && physics)
    {
        for (const auto& value : physics->impacts())
        {
            if (!binding->find_node(value.a.scene_id, value.a.node_id) ||
                !binding->find_node(value.b.scene_id, value.b.node_id) ||
                !physics->is_object_valid(value.a) || !physics->is_object_valid(value.b))
            {
                continue;
            }
            output.emplace_back(
                vm::NativeValue::Object{{"frame", value.frame},
                                        {"a", object_value(value.a)},
                                        {"b", object_value(value.b)},
                                        {"point", vector(value.point)},
                                        {"normal", vector(value.normal)},
                                        {"closingSpeed", value.closing_speed},
                                        {"estimatedImpulse", value.estimated_impulse}});
        }
    }
    arguments.set_return(std::move(output));
}

void cast_sphere(const Arguments& arguments)
{
    arguments.set_return({});
    auto* binding = BindingContext::current(arguments);
    auto* physics = binding ? binding->physics() : nullptr;
    double value[6]{};
    float radius = 0;
    if (!physics || !query_allowed(*binding) || arguments.count() != 7 ||
        !BindingContext::scalar(arguments, 6, radius))
    {
        return;
    }
    for (int index = 0; index < 6; ++index)
    {
        if (!BindingContext::number(arguments, index, value[index]))
        {
            return;
        }
    }
    float fraction = 0;
    if (physics->cast_sphere({value[0], value[1], value[2]}, {value[3], value[4], value[5]}, radius,
                             fraction))
    {
        arguments.set_return(static_cast<double>(fraction));
    }
}

void contact_target(const Arguments& arguments)
{
    arguments.set_return({});
    const auto* node = BindingContext::node(arguments);
    const auto* binding = BindingContext::current(arguments);
    const auto* physics = binding ? binding->physics() : nullptr;
    if (arguments.count() != 2 || !node || !physics)
    {
        return;
    }
    const auto* collider = node->get_component<Collider>();
    const auto* motor = node->get_component<CharacterMotor>();
    const bool has_collider = collider && collider->active();
    const bool has_motor = motor && motor->active();
    if (has_collider == has_motor)
    {
        return;
    }
    const PhysicsObject object{node->scene().id(), node->id(),
                               has_motor ? PhysicsObjectKind::character
                                         : PhysicsObjectKind::collider,
                               has_motor ? motor->instance_id() : collider->instance_id()};
    if (!physics->is_object_valid(object))
    {
        return;
    }
    const auto events = physics->contacts();
    const std::uint64_t frame = events.empty() ? 0 : events.back().frame;
    arguments.set_return(
        vm::NativeValue::Object{{"object", object_value(object)}, {"frame", frame}});
}

bool query_context(const Arguments& arguments, int filter_offset, PhysicsSystem*& physics,
                   PhysicsQueryFilter& filter)
{
    auto* binding = BindingContext::current(arguments);
    physics = binding ? binding->physics() : nullptr;
    std::uint64_t ignored_scene = 0;
    if (!physics || !query_allowed(*binding) ||
        arguments.count() != filter_offset + 8 ||
        !BindingContext::identifier(arguments, filter_offset, ignored_scene) ||
        !BindingContext::identifier(arguments, filter_offset + 1, filter.ignore_node_id) ||
        (filter.ignore_node_id != 0 &&
         !binding->find_node(ignored_scene, filter.ignore_node_id)))
    {
        return false;
    }
    for (int index = filter_offset + 2; index < filter_offset + 8; ++index)
    {
        if (!arguments.is_boolean(index))
        {
            return false;
        }
    }
    filter.ignore_descendants = arguments.boolean(filter_offset + 2);
    filter.include_static = arguments.boolean(filter_offset + 3);
    filter.include_kinematic = arguments.boolean(filter_offset + 4);
    filter.include_dynamic = arguments.boolean(filter_offset + 5);
    filter.include_sensors = arguments.boolean(filter_offset + 6);
    filter.include_characters = arguments.boolean(filter_offset + 7);
    return true;
}

bool query_vectors(const Arguments& arguments, Vec3& first, Vec3& second)
{
    double values[6]{};
    for (int index = 0; index < 6; ++index)
    {
        if (!BindingContext::number(arguments, index, values[index]))
        {
            return false;
        }
    }
    first = {values[0], values[1], values[2]};
    second = {values[3], values[4], values[5]};
    return true;
}

void query(const Arguments& arguments)
{
    arguments.set_return({});
    PhysicsSystem* physics = nullptr;
    PhysicsQueryFilter filter;
    Vec3 origin, displacement;
    float radius = 0;
    if (!query_context(arguments, 7, physics, filter) ||
        !query_vectors(arguments, origin, displacement) ||
        !BindingContext::scalar(arguments, 6, radius))
    {
        return;
    }
    PhysicsQueryHit hit;
    const bool success = arguments.data() == 0
                             ? physics->query_ray(origin, displacement, filter, hit)
                             : physics->query_sphere(origin, displacement, radius, filter, hit);
    if (success)
    {
        auto output = object_value(hit.object);
        output.emplace_back("hit", hit.hit);
        output.emplace_back("point", vector(hit.point));
        output.emplace_back("normal", vector(hit.normal));
        output.emplace_back("fraction", hit.fraction);
        output.emplace_back("distance", hit.distance);
        arguments.set_return(std::move(output));
    }
}

void overlap(const Arguments& arguments)
{
    arguments.set_return({});
    PhysicsSystem* physics = nullptr;
    PhysicsQueryFilter filter;
    Vec3 center, shape;
    if (!query_context(arguments, 6, physics, filter) || !query_vectors(arguments, center, shape))
    {
        return;
    }
    std::vector<PhysicsObject> objects;
    float radius = 0;
    if (arguments.data() == 0 && !BindingContext::scalar(arguments, 3, radius))
    {
        return;
    }
    const bool success = arguments.data() == 0
                             ? physics->overlap_sphere(center, radius, filter, objects)
                             : physics->overlap_box(center, shape, filter, objects);
    if (!success)
    {
        return;
    }
    vm::NativeValue::Array output;
    output.reserve(objects.size());
    for (const auto& object : objects)
    {
        output.emplace_back(object_value(object));
    }
    arguments.set_return(std::move(output));
}

void object_valid(const Arguments& arguments)
{
    arguments.set_return(false);
    auto* binding = BindingContext::current(arguments);
    auto* physics = binding ? binding->physics() : nullptr;
    PhysicsObject object;
    std::string kind;
    if (!physics || arguments.count() != 4 ||
        !BindingContext::identifier(arguments, 0, object.scene_id) ||
        !BindingContext::identifier(arguments, 1, object.node_id) ||
        !BindingContext::text(arguments, 2, kind) ||
        !BindingContext::identifier(arguments, 3, object.instance_id) ||
        !binding->find_node(object.scene_id, object.node_id) ||
        (kind != "collider" && kind != "character"))
    {
        return;
    }
    object.kind = kind == "character" ? PhysicsObjectKind::character : PhysicsObjectKind::collider;
    arguments.set_return(physics->is_object_valid(object));
}

void contacts(const Arguments& arguments)
{
    vm::NativeValue::Array output;
    auto* binding = BindingContext::current(arguments);
    auto* physics = binding ? binding->physics() : nullptr;
    if (arguments.count() == 0 && physics)
    {
        for (const auto& event : physics->contacts())
        {
            // Exit 可携带已删除对象的身份；只要仍有本 Run 的接收者就保留。
            if (!binding->find_node(event.a.scene_id, event.a.node_id) &&
                !binding->find_node(event.b.scene_id, event.b.node_id))
            {
                continue;
            }
            const char* phase = event.phase == PhysicsContactPhase::begin  ? "begin"
                                : event.phase == PhysicsContactPhase::stay ? "stay"
                                                                           : "end";
            output.emplace_back(vm::NativeValue::Object{{"frame", event.frame},
                                                        {"step", event.step},
                                                        {"phase", phase},
                                                        {"a", object_value(event.a)},
                                                        {"b", object_value(event.b)},
                                                        {"point", vector(event.point)},
                                                        {"normal", vector(event.normal)},
                                                        {"isTrigger", event.is_trigger}});
        }
    }
    arguments.set_return(std::move(output));
}
} // namespace

std::string_view PhysicsBinding::name() const noexcept
{
    return "Physics";
}
std::string_view PhysicsBinding::source() const noexcept
{
    return javascript;
}
bool PhysicsBinding::register_functions(vm::ScriptVm& vm) const
{
    return vm.function("PhysicsGetImpacts", impacts) &&
           vm.function("PhysicsCastSphere", cast_sphere) &&
           vm.function("PhysicsQueryRay", query, 0) &&
           vm.function("PhysicsQuerySphere", query, 1) &&
           vm.function("PhysicsOverlapSphere", overlap, 0) &&
           vm.function("PhysicsOverlapBox", overlap, 1) &&
           vm.function("PhysicsObjectValid", object_valid) &&
           vm.function("PhysicsContactTarget", contact_target) &&
           vm.function("PhysicsGetContacts", contacts);
}
} // namespace engine::plugins::script::bindings
