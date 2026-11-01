#include "plugins/runtime/script/bindings/physics_binding.hpp"

#include "plugins/runtime/script/bindings/binding_context.hpp"
#include "runtime/physics/physics_system.hpp"
#include "runtime/scene/scene.hpp"
#include "runtime/world/world_system.hpp"

namespace engine::plugins::script::bindings
{
namespace
{
constexpr std::string_view javascript = R"js(
function physicsQuery(query, origin, displacement, radius, options) {
    if (options === null || typeof options !== 'object' || Array.isArray(options)) return null;
    const ignored = options.ignoreNode;
    if (ignored !== undefined && ignored !== null && !(ignored instanceof Node)) return null;
    const ignore = ignored == null ? null : handles.get(ignored);
    if (ignored != null && !ignore) return null;
    const option = (name, fallback) => options[name] === undefined ? fallback : options[name];
    const result = query(origin?.x, origin?.y, origin?.z,
        displacement?.x, displacement?.y, displacement?.z, radius,
        ignore?.scene ?? 0n, ignore?.node ?? 0n,
        option('ignoreDescendants', true), option('includeStatic', true),
        option('includeKinematic', true), option('includeDynamic', true),
        option('includeSensors', false));
    if (result === null) return null;
    result.node = result.hit ? new Node(key, result.sceneId, result.nodeId) : null;
    Object.freeze(result.point);
    Object.freeze(result.normal);
    return Object.freeze(result);
}
export class Physics {
    constructor() { throw new TypeError('Physics is static'); }
    static GetImpacts() { return native.PhysicsGetImpacts(); }
    static CastSphere(x, y, z, dx, dy, dz, radius) {
        return native.PhysicsCastSphere(x, y, z, dx, dy, dz, radius);
    }
    static Raycast(origin, displacement, options = {}) {
        return physicsQuery(native.PhysicsQueryRay, origin, displacement, 0, options);
    }
    static SphereCast(origin, displacement, radius, options = {}) {
        return physicsQuery(native.PhysicsQuerySphere, origin, displacement, radius, options);
    }
    static IsHitValid(hit) {
        return hit?.hit === true && native.PhysicsHitValid(hit.sceneId, hit.nodeId, hit.colliderId);
    }
}
Object.freeze(Physics.prototype);
Object.freeze(Physics);
)js";

vm::NativeValue vector(const Vec3& value)
{
    return vm::NativeValue::Object{{"x", value.x()}, {"y", value.y()}, {"z", value.z()}};
}

void impacts(const Arguments& arguments)
{
    vm::NativeValue::Array output;
    auto* binding = BindingContext::current(arguments);
    auto* physics = binding ? binding->physics() : nullptr;
    auto* world = binding ? binding->world() : nullptr;
    const auto* scene = world && world->is_initialized() ? world->scene() : nullptr;
    if (arguments.count() == 0 && physics && scene && scene->id() == binding->scene_id())
    {
        for (const auto& value : physics->impacts())
        {
            if (value.scene_id != scene->id())
            {
                continue;
            }
            output.emplace_back(
                vm::NativeValue::Object{{"frame", value.frame},
                                        {"sceneId", value.scene_id},
                                        {"nodeA", value.node_a},
                                        {"nodeB", value.node_b},
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
    const auto* world = binding ? binding->world() : nullptr;
    const auto* scene = world ? world->scene() : nullptr;
    double value[6]{};
    float radius = 0;
    if (!physics || !scene || scene->id() != binding->scene_id() || arguments.count() != 7 ||
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

void query(const Arguments& arguments)
{
    arguments.set_return({});
    auto* binding = BindingContext::current(arguments);
    auto* physics = binding ? binding->physics() : nullptr;
    const auto* world = binding ? binding->world() : nullptr;
    const auto* scene = world ? world->scene() : nullptr;
    if (!physics || !scene || scene->id() != binding->scene_id() || arguments.count() != 14)
    {
        return;
    }
    double values[6]{};
    for (int index = 0; index < 6; ++index)
    {
        if (!BindingContext::number(arguments, index, values[index]))
        {
            return;
        }
    }
    float radius = 0;
    std::uint64_t ignored_scene = 0;
    PhysicsQueryFilter filter;
    if (!BindingContext::scalar(arguments, 6, radius) ||
        !BindingContext::identifier(arguments, 7, ignored_scene) ||
        !BindingContext::identifier(arguments, 8, filter.ignore_node_id) ||
        (filter.ignore_node_id != 0 && ignored_scene != scene->id()))
    {
        return;
    }
    for (int index = 9; index < 14; ++index)
    {
        if (!arguments.is_boolean(index))
        {
            return;
        }
    }
    filter.ignore_descendants = arguments.boolean(9);
    filter.include_static = arguments.boolean(10);
    filter.include_kinematic = arguments.boolean(11);
    filter.include_dynamic = arguments.boolean(12);
    filter.include_sensors = arguments.boolean(13);
    const Vec3 origin(values[0], values[1], values[2]);
    const Vec3 displacement(values[3], values[4], values[5]);
    PhysicsQueryHit hit;
    const bool success = arguments.data() == 0
                             ? physics->query_ray(origin, displacement, filter, hit)
                             : physics->query_sphere(origin, displacement, radius, filter, hit);
    if (success)
    {
        arguments.set_return(vm::NativeValue::Object{{"hit", hit.hit},
                                                     {"sceneId", hit.scene_id},
                                                     {"nodeId", hit.node_id},
                                                     {"colliderId", hit.collider_id},
                                                     {"point", vector(hit.point)},
                                                     {"normal", vector(hit.normal)},
                                                     {"fraction", hit.fraction},
                                                     {"distance", hit.distance}});
    }
}

void hit_valid(const Arguments& arguments)
{
    arguments.set_return(false);
    auto* binding = BindingContext::current(arguments);
    auto* physics = binding ? binding->physics() : nullptr;
    PhysicsQueryHit hit;
    hit.hit = true;
    if (physics && arguments.count() == 3 &&
        BindingContext::identifier(arguments, 0, hit.scene_id) &&
        BindingContext::identifier(arguments, 1, hit.node_id) &&
        BindingContext::identifier(arguments, 2, hit.collider_id) &&
        hit.scene_id == binding->scene_id())
    {
        arguments.set_return(physics->is_hit_valid(hit));
    }
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
           vm.function("PhysicsQuerySphere", query, 1) && vm.function("PhysicsHitValid", hit_valid);
}
} // namespace engine::plugins::script::bindings
