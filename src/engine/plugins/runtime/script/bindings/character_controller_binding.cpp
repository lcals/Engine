#if ENGINE_HAS_CHARACTER_CONTROLLER_PLUGIN
#include "plugins/runtime/script/bindings/character_controller_binding.hpp"
#include "plugins/runtime/character_controller/character_controller.hpp"
#include "plugins/runtime/character_controller/character_input_json.hpp"
#include "plugins/runtime/character_controller/character_status_json.hpp"
#include "plugins/runtime/script/bindings/control_binding_helpers.hpp"
namespace engine::plugins::script::bindings
{
namespace
{
using Controller = character_controller::CharacterController;
constexpr std::string_view javascript = R"js(
export class CharacterController extends Component {
    constructor(token,scene,node){super(token,scene,node,native.CharacterControllerKind);}
    SetInput(input){const h=handles.get(this);return native.CharacterControllerInput(h.scene,h.node,JSON.stringify(input));}
    ClearInput(){const h=handles.get(this);return native.CharacterControllerClear(h.scene,h.node);}
    GetSettings(){const h=handles.get(this);return copyComponentData(native.CharacterControllerSettings(h.scene,h.node));}
    SetSettings(settings){const h=handles.get(this);return native.CharacterControllerSetSettings(h.scene,h.node,JSON.stringify(settings,(_,v)=>typeof v==='bigint'?v.toString():v));}
    GetStatus(){const h=handles.get(this);return native.CharacterControllerStatus(h.scene,h.node);}
}
componentTypes.set(CharacterController,{add:native.CharacterControllerAdd,kind:native.CharacterControllerKind});
Object.freeze(CharacterController.prototype);Object.freeze(CharacterController);
)js";
void input(const Arguments& a)
{
    auto c = binding_detail::component<Controller>(a, 3);
    control_detail::Json j;
    character_controller::CharacterInput value;
    std::string error;
    if (!c || !control_detail::argument(a, 2, j) ||
        !character_controller::detail::read_input(j, value, error))
    {
        return;
    }
    a.set_return(c.set_input(value));
}
void clear(const Arguments& a)
{
    auto c = binding_detail::component<Controller>(a, 2);
    a.set_return(c && c.clear_input());
}
void status(const Arguments& a)
{
    auto c = binding_detail::component<Controller>(a, 2);
    a.set_return({});
    character_controller::CharacterStatus value;
    if (c && c.get_status(value))
    {
        a.set_return(control_detail::native(character_controller::detail::status_json(value)));
    }
}
} // namespace
std::string_view CharacterControllerBinding::name() const noexcept
{
    return "CharacterController";
}
std::string_view CharacterControllerBinding::source() const noexcept
{
    return javascript;
}
bool CharacterControllerBinding::register_functions(vm::ScriptVm& vm) const
{
    return vm.integer("CharacterControllerKind", static_cast<int>(Controller::component_type)) &&
           vm.function("CharacterControllerAdd", binding_detail::add_component<Controller>) &&
           vm.function("CharacterControllerInput", input) &&
           vm.function("CharacterControllerClear", clear) &&
           vm.function("CharacterControllerSettings", control_detail::get_settings<Controller>) &&
           vm.function("CharacterControllerSetSettings",
                       control_detail::set_settings<Controller>) &&
           vm.function("CharacterControllerStatus", status);
}
} // namespace engine::plugins::script::bindings
#endif
