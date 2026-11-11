#if ENGINE_HAS_CAMERA_PLUGIN
#include "plugins/runtime/script/bindings/camera_controller_binding.hpp"
#include "plugins/runtime/camera/camera_status_json.hpp"
#include "plugins/runtime/script/bindings/control_binding_helpers.hpp"
#include "runtime/scene/scene.hpp"
#include "runtime/world/world_system.hpp"
namespace engine::plugins::script::bindings
{
namespace
{
using Controller = camera::CameraController;
constexpr std::string_view javascript = R"js(
export class CameraController extends Component {
    constructor(token,scene,node){super(token,scene,node,native.CameraControllerKind);}
    GetSettings(){const h=handles.get(this);return copyComponentData(native.CameraControllerSettings(h.scene,h.node));}
    SetSettings(settings){const h=handles.get(this);return native.CameraControllerSetSettings(h.scene,h.node,JSON.stringify(settings,(_,v)=>typeof v==='bigint'?v.toString():v));}
    SetParameters(values){const h=handles.get(this);return native.CameraControllerParameters(h.scene,h.node,JSON.stringify(values));}
    GetParameter(name){const h=handles.get(this);return native.CameraControllerParameter(h.scene,h.node,name);}
    Trigger(name){const h=handles.get(this);return native.CameraControllerTrigger(h.scene,h.node,name,false);}
    ResetTrigger(name){const h=handles.get(this);return native.CameraControllerTrigger(h.scene,h.node,name,true);}
    SetTarget(slot,target){const h=handles.get(this);const t=target===null?{scene:0n,node:0n}:handles.get(target);if(target!==null&&!(target instanceof Node))return false;return native.CameraControllerSetTarget(h.scene,h.node,slot,t.scene,t.node);}
    ClearTargetOverride(slot){const h=handles.get(this);return native.CameraControllerClearTarget(h.scene,h.node,slot);}
    GetTarget(slot){const h=handles.get(this);const t=native.CameraControllerTarget(h.scene,h.node,slot);return t===null?null:new Node(key,t.scene,t.node);}
    GetStatus(){const h=handles.get(this);return native.CameraControllerStatus(h.scene,h.node);}
    Reset(){const h=handles.get(this);return native.CameraControllerReset(h.scene,h.node);}
}
componentTypes.set(CameraController,{add:native.CameraControllerAdd,kind:native.CameraControllerKind});
Object.freeze(CameraController.prototype);Object.freeze(CameraController);
)js";
void parameters(const Arguments& a)
{
    auto c = binding_detail::component<Controller>(a, 3);
    control_detail::Json j;
    std::vector<schema::CameraParameterValueT> values;
    if (c && control_detail::argument(a, 2, j) && camera::detail::read_parameters(c, j, values))
    {
        a.set_return(c.set_parameters(values));
    }
}
void parameter(const Arguments& a)
{
    auto c = binding_detail::component<Controller>(a, 3);
    a.set_return({});
    std::string name;
    schema::CameraParameterValueT p;
    if (!c || !BindingContext::text(a, 2, name) || !c.get_parameter(name, p))
    {
        return;
    }
    switch (p.type)
    {
    case schema::CameraParameterType::float_value:
        a.set_return(static_cast<double>(p.float_value));
        break;
    case schema::CameraParameterType::int_value:
        a.set_return(p.int_value);
        break;
    default:
        a.set_return(p.bool_value);
        break;
    }
}
void trigger(const Arguments& a)
{
    auto c = binding_detail::component<Controller>(a, 4);
    std::string name;
    if (c && BindingContext::text(a, 2, name) && a.is_boolean(3))
    {
        a.set_return(a.boolean(3) ? c.reset_trigger(name) : c.trigger(name));
    }
}
void reset(const Arguments& a)
{
    auto c = binding_detail::component<Controller>(a, 2);
    a.set_return(c && c.reset());
}
void clear_target(const Arguments& a)
{
    auto c = binding_detail::component<Controller>(a, 3);
    std::string slot;
    if (c && BindingContext::text(a, 2, slot))
    {
        a.set_return(c.clear_target_override(slot));
    }
}
void set_target(const Arguments& a)
{
    auto c = binding_detail::component<Controller>(a, 5);
    std::string slot;
    std::uint64_t scene = 0, id = 0;
    if (!c || !BindingContext::text(a, 2, slot) || !a.identifier(3, scene) || !a.identifier(4, id))
    {
        return;
    }
    Node target;
    if (id)
    {
        auto* world = c.node().world();
        target = world->find_node(id);
        if (!target || target.scene()->id() != scene)
        {
            return;
        }
    }
    else if (scene)
    {
        return;
    }
    a.set_return(c.set_target(slot, target));
}
void target(const Arguments& a)
{
    auto c = binding_detail::component<Controller>(a, 3);
    a.set_return({});
    std::string slot;
    Node n;
    if (c && BindingContext::text(a, 2, slot) && c.get_target(slot, n) && n)
    {
        a.set_return(vm::NativeValue::Object{{"scene", n.scene()->id()}, {"node", n.id()}});
    }
}
void status(const Arguments& a)
{
    auto c = binding_detail::component<Controller>(a, 2);
    a.set_return({});
    camera::CameraStatus value;
    if (c && c.get_status(value))
    {
        a.set_return(control_detail::native(camera::detail::status_json(value)));
    }
}
} // namespace
std::string_view CameraControllerBinding::name() const noexcept
{
    return "CameraController";
}
std::string_view CameraControllerBinding::source() const noexcept
{
    return javascript;
}
bool CameraControllerBinding::register_functions(vm::ScriptVm& vm) const
{
    return vm.integer("CameraControllerKind", static_cast<int>(Controller::component_type)) &&
           vm.function("CameraControllerAdd", binding_detail::add_component<Controller>) &&
           vm.function("CameraControllerSettings", control_detail::get_settings<Controller>) &&
           vm.function("CameraControllerSetSettings", control_detail::set_settings<Controller>) &&
           vm.function("CameraControllerParameters", parameters) &&
           vm.function("CameraControllerParameter", parameter) &&
           vm.function("CameraControllerTrigger", trigger) &&
           vm.function("CameraControllerSetTarget", set_target) &&
           vm.function("CameraControllerClearTarget", clear_target) &&
           vm.function("CameraControllerTarget", target) &&
           vm.function("CameraControllerStatus", status) &&
           vm.function("CameraControllerReset", reset);
}
} // namespace engine::plugins::script::bindings
#endif
