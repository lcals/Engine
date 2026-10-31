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
    return vm.function("PhysicsGetImpacts", impacts);
}
} // namespace engine::plugins::script::bindings
