#if defined(ENGINE_HAS_CHARACTER_CONTROLLER_PLUGIN) && ENGINE_HAS_CHARACTER_CONTROLLER_PLUGIN

#include "plugins/runtime/script/bindings/character_controller_binding.hpp"

#include "plugins/runtime/character_controller/character_controller.hpp"
#include "plugins/runtime/character_controller/character_input.hpp"
#include "plugins/runtime/script/bindings/binding_context.hpp"
#include "runtime/scene/node.hpp"

namespace engine::plugins::script::bindings
{
namespace
{
constexpr std::string_view javascript = R"js(
export class CharacterController extends Component {
    constructor(token, scene, node) { super(token, scene, node, native.CharacterControllerKind); }
    SetInput(moveX, moveY, lookX, lookY, jump, toggleView) {
        const h = handles.get(this);
        return native.CharacterControllerInput(
            h.scene, h.node, moveX, moveY, lookX, lookY, jump, toggleView);
    }
    ClearInput() {
        const h = handles.get(this);
        return native.CharacterControllerClear(h.scene, h.node);
    }
}
componentTypes.set(CharacterController, {
    add: native.CharacterControllerAdd, kind: native.CharacterControllerKind
});
Object.freeze(CharacterController.prototype);
Object.freeze(CharacterController);
)js";

void add(const Arguments& arguments)
{
    auto* node = BindingContext::node(arguments);
    arguments.set_return(arguments.count() == 2 && node != nullptr &&
                         node->add_component<character_controller::CharacterController>() !=
                             nullptr);
}

void set_input(const Arguments& arguments)
{
    arguments.set_return(false);
    auto* node = BindingContext::node(arguments);
    auto* controller = node != nullptr
                           ? node->get_component<character_controller::CharacterController>()
                           : nullptr;
    double move_x = 0;
    double move_y = 0;
    double look_x = 0;
    double look_y = 0;
    if (controller == nullptr || arguments.count() != 8 ||
        !BindingContext::number(arguments, 2, move_x) ||
        !BindingContext::number(arguments, 3, move_y) ||
        !BindingContext::number(arguments, 4, look_x) ||
        !BindingContext::number(arguments, 5, look_y) || !arguments.is_boolean(6) ||
        !arguments.is_boolean(7))
    {
        return;
    }
    character_controller::CharacterInput input;
    input.move = {move_x, move_y};
    input.look = {look_x, look_y};
    input.jump = arguments.boolean(6);
    input.toggle_view = arguments.boolean(7);
    arguments.set_return(controller->set_input(input));
}

void clear_input(const Arguments& arguments)
{
    arguments.set_return(false);
    auto* node = BindingContext::node(arguments);
    auto* controller = node != nullptr
                           ? node->get_component<character_controller::CharacterController>()
                           : nullptr;
    if (controller != nullptr && arguments.count() == 2)
    {
        controller->clear_input();
        arguments.set_return(true);
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
    return vm.integer(
               "CharacterControllerKind",
               static_cast<int>(character_controller::CharacterController::component_type)) &&
           vm.function("CharacterControllerAdd", add) &&
           vm.function("CharacterControllerInput", set_input) &&
           vm.function("CharacterControllerClear", clear_input);
}
} // namespace engine::plugins::script::bindings

#endif
