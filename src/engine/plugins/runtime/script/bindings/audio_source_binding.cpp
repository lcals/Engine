#include "plugins/runtime/script/bindings/audio_source_binding.hpp"

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

AudioSource* audio_source(const Arguments& arguments, int count)
{
    arguments.GetReturnValue().Set(false);
    auto* node = BindingContext::node(arguments);
    return node != nullptr && arguments.Length() == count ? node->get_component<AudioSource>()
                                                          : nullptr;
}

void add(const Arguments& arguments)
{
    auto* node = BindingContext::node(arguments);
    arguments.GetReturnValue().Set(arguments.Length() == 2 && node != nullptr &&
                                   node->add_component<AudioSource>() != nullptr);
}

void clip(const Arguments& arguments)
{
    auto* value = audio_source(arguments, 3);
    std::string path;
    if (value != nullptr && BindingContext::text(arguments, 2, path))
    {
        arguments.GetReturnValue().Set(value->set_clip(path));
    }
}

void set_scalar(const Arguments& arguments, float schema::AudioSourceDataT::* field)
{
    auto* value = audio_source(arguments, 3);
    float number = 0.0F;
    if (value != nullptr && BindingContext::scalar(arguments, 2, number))
    {
        auto settings = value->settings();
        settings.*field = number;
        arguments.GetReturnValue().Set(value->set_settings(settings));
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
    auto* value = audio_source(arguments, 3);
    if (value != nullptr && arguments[2]->IsBoolean())
    {
        auto settings = value->settings();
        settings.*field = arguments[2]->IsTrue();
        arguments.GetReturnValue().Set(value->set_settings(settings));
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
    auto* value = audio_source(arguments, 4);
    float minimum = 0.0F;
    float maximum = 0.0F;
    if (value != nullptr && BindingContext::scalar(arguments, 2, minimum) &&
        BindingContext::scalar(arguments, 3, maximum))
    {
        auto settings = value->settings();
        settings.min_distance = minimum;
        settings.max_distance = maximum;
        arguments.GetReturnValue().Set(value->set_settings(settings));
    }
}

void play(const Arguments& arguments)
{
    auto* value = audio_source(arguments, 2);
    arguments.GetReturnValue().Set(value != nullptr && value->play());
}

void pause(const Arguments& arguments)
{
    auto* value = audio_source(arguments, 2);
    arguments.GetReturnValue().Set(value != nullptr && value->pause());
}

void stop(const Arguments& arguments)
{
    auto* value = audio_source(arguments, 2);
    arguments.GetReturnValue().Set(value != nullptr && value->stop());
}

void playing(const Arguments& arguments)
{
    const auto* value = audio_source(arguments, 2);
    arguments.GetReturnValue().Set(value != nullptr && value->playing());
}

void playback(const Arguments& arguments)
{
    const auto* value = audio_source(arguments, 2);
    arguments.GetReturnValue().SetNull();
    if (value == nullptr)
    {
        return;
    }
    const char* state = nullptr;
    switch (value->playback())
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
    v8::Local<v8::String> text;
    if (state != nullptr && v8::String::NewFromUtf8(arguments.GetIsolate(), state).ToLocal(&text))
    {
        arguments.GetReturnValue().Set(text);
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

bool AudioSourceBinding::register_functions(v8::Local<v8::Context> context,
                                            v8::Local<v8::Object> native) const
{
    return BindingContext::integer(context, native, "AudioSourceKind",
                                   static_cast<int>(AudioSource::component_type)) &&
           BindingContext::function(context, native, "AudioSourceAdd", add) &&
           BindingContext::function(context, native, "AudioSourceClip", clip) &&
           BindingContext::function(context, native, "AudioSourceVolume", volume) &&
           BindingContext::function(context, native, "AudioSourcePitch", pitch) &&
           BindingContext::function(context, native, "AudioSourceLoop", loop) &&
           BindingContext::function(context, native, "AudioSourceSpatial", spatial) &&
           BindingContext::function(context, native, "AudioSourceAwake", awake) &&
           BindingContext::function(context, native, "AudioSourceDistances", distances) &&
           BindingContext::function(context, native, "AudioSourcePlay", play) &&
           BindingContext::function(context, native, "AudioSourcePause", pause) &&
           BindingContext::function(context, native, "AudioSourceStop", stop) &&
           BindingContext::function(context, native, "AudioSourcePlaying", playing) &&
           BindingContext::function(context, native, "AudioSourcePlayback", playback);
}
} // namespace engine::plugins::script::bindings
