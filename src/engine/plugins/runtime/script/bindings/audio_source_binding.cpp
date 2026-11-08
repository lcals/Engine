#include "plugins/runtime/script/bindings/audio_source_binding.hpp"
#include "plugins/runtime/script/bindings/binding_helpers.hpp"

#include "plugins/runtime/script/bindings/binding_context.hpp"
#include "runtime/scene/audio_source.hpp"
#include "runtime/scene/node.hpp"

namespace engine::plugins::script::bindings
{
namespace
{
constexpr std::string_view javascript = R"js(
export class AudioSource extends Component {
    constructor(token, scene, node) { super(token, scene, node, native.AudioSourceKind); }
    SetClip(path) {
        const h = handles.get(this);
        return native.AudioSourceClip(h.scene, h.node, path);
    }
    SetVolume(value) {
        const h = handles.get(this);
        return native.AudioSourceVolume(h.scene, h.node, value);
    }
    SetPitch(value) {
        const h = handles.get(this);
        return native.AudioSourcePitch(h.scene, h.node, value);
    }
    SetLoop(value) {
        const h = handles.get(this);
        return native.AudioSourceLoop(h.scene, h.node, value);
    }
    SetSpatial(value) {
        const h = handles.get(this);
        return native.AudioSourceSpatial(h.scene, h.node, value);
    }
    SetPlayOnAwake(value) {
        const h = handles.get(this);
        return native.AudioSourceAwake(h.scene, h.node, value);
    }
    SetDistances(minimum, maximum) {
        const h = handles.get(this);
        return native.AudioSourceDistances(h.scene, h.node, minimum, maximum);
    }
    Play() {
        const h = handles.get(this);
        return native.AudioSourcePlay(h.scene, h.node);
    }
    Pause() {
        const h = handles.get(this);
        return native.AudioSourcePause(h.scene, h.node);
    }
    Stop() {
        const h = handles.get(this);
        return native.AudioSourceStop(h.scene, h.node);
    }
    get isPlaying() {
        const h = handles.get(this);
        return native.AudioSourcePlaying(h.scene, h.node);
    }
    get playback() {
        const h = handles.get(this);
        return native.AudioSourcePlayback(h.scene, h.node);
    }
}
componentTypes.set(AudioSource, { add: native.AudioSourceAdd, kind: native.AudioSourceKind });
Object.freeze(AudioSource.prototype);
Object.freeze(AudioSource);
)js";

void clip(const Arguments& arguments)
{
    auto value = binding_detail::component<AudioSource>(arguments, 3);
    std::string path;
    if (value.valid() && BindingContext::text(arguments, 2, path))
    {
        arguments.set_return(value.set_clip(path));
    }
}

void set_scalar(const Arguments& arguments, float schema::AudioSourceDataT::* field)
{
    auto value = binding_detail::component<AudioSource>(arguments, 3);
    float number = 0.0F;
    if (value.valid() && BindingContext::scalar(arguments, 2, number))
    {
        AudioSource::Data settings{};
        if (!value.get_settings(settings))
        {
            arguments.set_return(false);
            return;
        }
        settings.*field = number;
        arguments.set_return(value.set_settings(settings));
    }
}

void volume(const Arguments& arguments)
{
    set_scalar(arguments, &schema::AudioSourceDataT::volume);
}

void pitch(const Arguments& arguments)
{
    set_scalar(arguments, &schema::AudioSourceDataT::pitch);
}

void set_boolean(const Arguments& arguments, bool schema::AudioSourceDataT::* field)
{
    auto value = binding_detail::component<AudioSource>(arguments, 3);
    if (value.valid() && arguments.is_boolean(2))
    {
        AudioSource::Data settings{};
        if (!value.get_settings(settings))
        {
            arguments.set_return(false);
            return;
        }
        settings.*field = arguments.boolean(2);
        arguments.set_return(value.set_settings(settings));
    }
}

void loop(const Arguments& arguments)
{
    set_boolean(arguments, &schema::AudioSourceDataT::loop);
}

void spatial(const Arguments& arguments)
{
    set_boolean(arguments, &schema::AudioSourceDataT::spatial);
}

void awake(const Arguments& arguments)
{
    set_boolean(arguments, &schema::AudioSourceDataT::play_on_awake);
}

void distances(const Arguments& arguments)
{
    auto value = binding_detail::component<AudioSource>(arguments, 4);
    float minimum = 0.0F;
    float maximum = 0.0F;
    if (value.valid() && BindingContext::scalar(arguments, 2, minimum) &&
        BindingContext::scalar(arguments, 3, maximum))
    {
        AudioSource::Data settings{};
        if (!value.get_settings(settings))
        {
            arguments.set_return(false);
            return;
        }
        settings.min_distance = minimum;
        settings.max_distance = maximum;
        arguments.set_return(value.set_settings(settings));
    }
}

void play(const Arguments& arguments)
{
    auto value = binding_detail::component<AudioSource>(arguments, 2);
    arguments.set_return(value.valid() && value.play());
}

void pause(const Arguments& arguments)
{
    auto value = binding_detail::component<AudioSource>(arguments, 2);
    arguments.set_return(value.valid() && value.pause());
}

void stop(const Arguments& arguments)
{
    auto value = binding_detail::component<AudioSource>(arguments, 2);
    arguments.set_return(value.valid() && value.stop());
}

void playing(const Arguments& arguments)
{
    const auto value = binding_detail::component<AudioSource>(arguments, 2);
    arguments.set_return(value.valid() && value.playing());
}

void playback(const Arguments& arguments)
{
    const auto value = binding_detail::component<AudioSource>(arguments, 2);
    arguments.set_return({});
    if (!value)
    {
        return;
    }
    const char* state = nullptr;
    switch (value.playback())
    {
    case AudioPlayback::stopped:
        state = "stopped";
        break;
    case AudioPlayback::pending:
        state = "pending";
        break;
    case AudioPlayback::playing:
        state = "playing";
        break;
    case AudioPlayback::paused:
        state = "paused";
        break;
    case AudioPlayback::failed:
        state = "failed";
        break;
    }
    if (state != nullptr)
    {
        arguments.set_return(state);
    }
}
} // namespace

std::string_view AudioSourceBinding::name() const noexcept
{
    return "AudioSource";
}

std::string_view AudioSourceBinding::source() const noexcept
{
    return javascript;
}

bool AudioSourceBinding::register_functions(vm::ScriptVm& vm) const
{
    return vm.integer("AudioSourceKind", static_cast<int>(AudioSource::component_type)) &&
           vm.function("AudioSourceAdd", binding_detail::add_component<AudioSource>) &&
           vm.function("AudioSourceClip", clip) && vm.function("AudioSourceVolume", volume) &&
           vm.function("AudioSourcePitch", pitch) && vm.function("AudioSourceLoop", loop) &&
           vm.function("AudioSourceSpatial", spatial) && vm.function("AudioSourceAwake", awake) &&
           vm.function("AudioSourceDistances", distances) && vm.function("AudioSourcePlay", play) &&
           vm.function("AudioSourcePause", pause) && vm.function("AudioSourceStop", stop) &&
           vm.function("AudioSourcePlaying", playing) &&
           vm.function("AudioSourcePlayback", playback);
}
} // namespace engine::plugins::script::bindings
