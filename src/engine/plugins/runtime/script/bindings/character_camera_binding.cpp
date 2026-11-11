#if ENGINE_HAS_CHARACTER_CONTROLLER_PLUGIN
#include "plugins/runtime/script/bindings/character_camera_binding.hpp"
#include "plugins/runtime/character_controller/character_camera.hpp"
#include "plugins/runtime/script/bindings/control_binding_helpers.hpp"
namespace engine::plugins::script::bindings
{
namespace
{
using Adapter = character_controller::CharacterCamera;
constexpr std::string_view javascript = R"js(
export class CharacterCamera extends Component {
    constructor(token,scene,node){super(token,scene,node,native.CharacterCameraKind);}
    GetSettings(){const h=handles.get(this);return copyComponentData(native.CharacterCameraSettings(h.scene,h.node));}
    SetSettings(settings){const h=handles.get(this);return native.CharacterCameraSetSettings(h.scene,h.node,JSON.stringify(settings));}
    GetAngles(){const h=handles.get(this);return native.CharacterCameraAngles(h.scene,h.node);}
    SetAngles(yaw,pitch){const h=handles.get(this);return native.CharacterCameraSetAngles(h.scene,h.node,yaw,pitch);}
    ApplyLook(delta,rate,seconds){if(!Array.isArray(delta)||delta.length!==2||!Array.isArray(rate)||rate.length!==2)return false;const h=handles.get(this);return native.CharacterCameraLook(h.scene,h.node,delta[0],delta[1],rate[0],rate[1],seconds);}
    ToggleView(){const h=handles.get(this);return native.CharacterCameraToggle(h.scene,h.node);}
}
componentTypes.set(CharacterCamera,{add:native.CharacterCameraAdd,kind:native.CharacterCameraKind});
Object.freeze(CharacterCamera.prototype);Object.freeze(CharacterCamera);
)js";
void angles(const Arguments& a)
{
    auto c = binding_detail::component<Adapter>(a, 2);
    a.set_return({});
    double yaw = 0, pitch = 0;
    if (c && c.get_angles(yaw, pitch))
    {
        a.set_return(vm::NativeValue::Array{yaw, pitch});
    }
}
void set_angles(const Arguments& a)
{
    auto c = binding_detail::component<Adapter>(a, 4);
    double yaw = 0, pitch = 0;
    if (c && BindingContext::number(a, 2, yaw) && BindingContext::number(a, 3, pitch))
    {
        a.set_return(c.set_angles(yaw, pitch));
    }
}
void look(const Arguments& a)
{
    auto c = binding_detail::component<Adapter>(a, 7);
    double v[5]{};
    if (!c)
    {
        return;
    }
    for (int i = 0; i < 5; ++i)
    {
        if (!BindingContext::number(a, i + 2, v[i]))
        {
            return;
        }
    }
    a.set_return(c.apply_look({v[0], v[1]}, {v[2], v[3]}, v[4]));
}
void toggle(const Arguments& a)
{
    auto c = binding_detail::component<Adapter>(a, 2);
    a.set_return(c && c.toggle_view());
}
} // namespace
std::string_view CharacterCameraBinding::name() const noexcept
{
    return "CharacterCamera";
}
std::string_view CharacterCameraBinding::source() const noexcept
{
    return javascript;
}
bool CharacterCameraBinding::register_functions(vm::ScriptVm& vm) const
{
    return vm.integer("CharacterCameraKind", static_cast<int>(Adapter::component_type)) &&
           vm.function("CharacterCameraAdd", binding_detail::add_component<Adapter>) &&
           vm.function("CharacterCameraSettings", control_detail::get_settings<Adapter>) &&
           vm.function("CharacterCameraSetSettings", control_detail::set_settings<Adapter>) &&
           vm.function("CharacterCameraAngles", angles) &&
           vm.function("CharacterCameraSetAngles", set_angles) &&
           vm.function("CharacterCameraLook", look) && vm.function("CharacterCameraToggle", toggle);
}
} // namespace engine::plugins::script::bindings
#endif
