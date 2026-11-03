#if defined(ENGINE_HAS_EFFECTS_PLUGIN) && ENGINE_HAS_EFFECTS_PLUGIN

#include "plugins/runtime/script/bindings/effect_binding.hpp"

#include "plugins/runtime/effects/effect_component.hpp"
#include "plugins/runtime/script/bindings/binding_context.hpp"
#include "runtime/scene/node.hpp"

namespace engine::plugins::script::bindings
{
namespace
{
constexpr std::string_view javascript = R"js(
export class EffectComponent extends Component {
    constructor(token, scene, node) { super(token, scene, node, native.EffectKind); }
    SetEffect(path) { const h = handles.get(this); return native.EffectResource(h.scene, h.node, path); }
    SetLoop(value) { const h = handles.get(this); return native.EffectLoop(h.scene, h.node, value); }
    SetPlayOnAwake(value) { const h = handles.get(this); return native.EffectAwake(h.scene, h.node, value); }
    SetSpeed(value) { const h = handles.get(this); return native.EffectSpeed(h.scene, h.node, value); }
    Play() { const h = handles.get(this); return native.EffectPlay(h.scene, h.node); }
    Pause() { const h = handles.get(this); return native.EffectPause(h.scene, h.node); }
    Stop() { const h = handles.get(this); return native.EffectStop(h.scene, h.node); }
    get isPlaying() { const h = handles.get(this); return native.EffectPlaying(h.scene, h.node); }
}
componentTypes.set(EffectComponent, { add: native.EffectAdd, kind: native.EffectKind });
Object.freeze(EffectComponent.prototype);
Object.freeze(EffectComponent);
)js";

effects::EffectComponent component(const Arguments& arguments, int count)
{
    arguments.set_return(false);
    auto node = BindingContext::node(arguments);
    return node && arguments.count() == count ? node.get_component<effects::EffectComponent>()
                                              : effects::EffectComponent{};
}
void add(const Arguments& arguments)
{
    auto node = BindingContext::node(arguments);
    arguments.set_return(arguments.count() == 2 && node &&
                         node.add_component<effects::EffectComponent>().valid());
}
void resource(const Arguments& arguments)
{
    auto value = component(arguments, 3);
    std::string path;
    if (value && BindingContext::text(arguments, 2, path))
    {
        arguments.set_return(value.set_effect(path));
    }
}
void boolean_setting(const Arguments& arguments)
{
    auto value = component(arguments, 3);
    if (value && arguments.is_boolean(2))
    {
        auto settings = value.settings();
        if (arguments.data() == 0)
        {
            settings.loop = arguments.boolean(2);
        }
        else
        {
            settings.play_on_awake = arguments.boolean(2);
        }
        arguments.set_return(value.set_settings(settings));
    }
}
void speed(const Arguments& arguments)
{
    auto value = component(arguments, 3);
    float speed = 0;
    if (value && BindingContext::scalar(arguments, 2, speed))
    {
        auto settings = value.settings();
        settings.speed = speed;
        arguments.set_return(value.set_settings(settings));
    }
}
void playback(const Arguments& arguments)
{
    auto value = component(arguments, 2);
    if (!value)
    {
        return;
    }
    switch (arguments.data())
    {
    case 0:
        arguments.set_return(value.play());
        break;
    case 1:
        arguments.set_return(value.pause());
        break;
    case 2:
        arguments.set_return(value.stop());
        break;
    case 3:
        arguments.set_return(value.playing());
        break;
    }
}
} // namespace

std::string_view EffectBinding::name() const noexcept
{
    return "EffectComponent";
}
std::string_view EffectBinding::source() const noexcept
{
    return javascript;
}
bool EffectBinding::register_functions(vm::ScriptVm& vm) const
{
    return vm.integer("EffectKind", static_cast<int>(effects::EffectComponent::component_type)) &&
           vm.function("EffectAdd", add) && vm.function("EffectResource", resource) &&
           vm.function("EffectLoop", boolean_setting, 0) &&
           vm.function("EffectAwake", boolean_setting, 1) && vm.function("EffectSpeed", speed) &&
           vm.function("EffectPlay", playback, 0) && vm.function("EffectPause", playback, 1) &&
           vm.function("EffectStop", playback, 2) && vm.function("EffectPlaying", playback, 3);
}
} // namespace engine::plugins::script::bindings

#endif
