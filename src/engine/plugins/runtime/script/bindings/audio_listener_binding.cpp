#include "plugins/runtime/script/bindings/audio_listener_binding.hpp"
#include "plugins/runtime/script/bindings/binding_helpers.hpp"

#include "plugins/runtime/script/bindings/binding_context.hpp"
#include "runtime/scene/audio_listener.hpp"
#include "runtime/scene/node.hpp"

namespace engine::plugins::script::bindings
{
namespace
{
constexpr std::string_view javascript = R"js(
export class AudioListener extends Component {
    constructor(token, scene, node) { super(token, scene, node, native.AudioListenerKind); }
    SetVolume(value) {
        const h = handles.get(this);
        return native.AudioListenerVolume(h.scene, h.node, value);
    }
}
componentTypes.set(AudioListener, { add: native.AudioListenerAdd, kind: native.AudioListenerKind });
Object.freeze(AudioListener.prototype);
Object.freeze(AudioListener);
)js";

void volume(const Arguments& arguments)
{
    arguments.set_return(false);
    auto node = BindingContext::node(arguments);
    auto listener = !node ? AudioListener{} : node.get_component<AudioListener>();
    float value = 0.0F;
    if (listener.valid() && arguments.count() == 3 && BindingContext::scalar(arguments, 2, value))
    {
        AudioListener::Data settings{};
        if (!listener.get_settings(settings))
        {
            arguments.set_return(false);
            return;
        }
        settings.volume = value;
        arguments.set_return(listener.set_settings(settings));
    }
}
} // namespace

std::string_view AudioListenerBinding::name() const noexcept
{
    return "AudioListener";
}

std::string_view AudioListenerBinding::source() const noexcept
{
    return javascript;
}

bool AudioListenerBinding::register_functions(vm::ScriptVm& vm) const
{
    return vm.integer("AudioListenerKind", static_cast<int>(AudioListener::component_type)) &&
           vm.function("AudioListenerAdd", binding_detail::add_component<AudioListener>) &&
           vm.function("AudioListenerVolume", volume);
}
} // namespace engine::plugins::script::bindings
