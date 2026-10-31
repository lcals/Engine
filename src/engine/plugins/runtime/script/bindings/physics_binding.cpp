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
export class Physics {
    constructor() { throw new TypeError('Physics is static'); }
    static GetImpacts() { return native.PhysicsGetImpacts(); }
    static CastSphere(x, y, z, dx, dy, dz, radius) {
        return native.PhysicsCastSphere(x, y, z, dx, dy, dz, radius);
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
    if (!physics || !scene || scene->id() != binding->scene_id() ||
        arguments.count() != 7 || !BindingContext::scalar(arguments, 6, radius))
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
    if (physics->cast_sphere({value[0], value[1], value[2]},
                             {value[3], value[4], value[5]}, radius, fraction))
    {
        arguments.set_return(static_cast<double>(fraction));
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
           vm.function("PhysicsCastSphere", cast_sphere);
}
} // namespace engine::plugins::script::bindings
