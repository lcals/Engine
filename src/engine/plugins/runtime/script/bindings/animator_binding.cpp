#include "plugins/runtime/script/bindings/animator_binding.hpp"

#include "plugins/runtime/script/bindings/binding_context.hpp"
#include "runtime/scene/animator.hpp"
#include "runtime/scene/node.hpp"

namespace engine::plugins::script::bindings
{
namespace
{
constexpr std::string_view javascript = R"js(
export class Animator extends Component {
    constructor(token, scene, node) { super(token, scene, node, native.AnimatorKind); }
    SetAnimation(path) {
        const h = handles.get(this);
        return native.AnimatorAnimation(h.scene, h.node, path);
    }
    SetSpeed(value) {
        const h = handles.get(this);
        return native.AnimatorSpeed(h.scene, h.node, value);
    }
    SetLoop(value) {
        const h = handles.get(this);
        return native.AnimatorLoop(h.scene, h.node, value);
    }
    SetPlayOnAwake(value) {
        const h = handles.get(this);
        return native.AnimatorAwake(h.scene, h.node, value);
    }
    Play() {
        const h = handles.get(this);
        return native.AnimatorPlay(h.scene, h.node);
    }
    Pause() {
        const h = handles.get(this);
        return native.AnimatorPause(h.scene, h.node);
    }
    Stop() {
        const h = handles.get(this);
        return native.AnimatorStop(h.scene, h.node);
    }
    Seek(seconds) {
        const h = handles.get(this);
        return native.AnimatorSeek(h.scene, h.node, seconds);
    }
    get isPlaying() {
        const h = handles.get(this);
        return native.AnimatorPlaying(h.scene, h.node);
    }
    get playback() {
        const h = handles.get(this);
        return native.AnimatorPlayback(h.scene, h.node);
    }
    get time() {
        const h = handles.get(this);
        return native.AnimatorTime(h.scene, h.node);
    }
}
componentTypes.set(Animator, { add: native.AnimatorAdd, kind: native.AnimatorKind });
Object.freeze(Animator.prototype);
Object.freeze(Animator);
)js";

Animator* animator(const Arguments& arguments, int count)
{
    arguments.GetReturnValue().Set(false);
    auto* node = BindingContext::node(arguments);
    return node && arguments.Length() == count ? node->get_component<Animator>() : nullptr;
}

void add(const Arguments& arguments)
{
    auto* node = BindingContext::node(arguments);
    arguments.GetReturnValue().Set(arguments.Length() == 2 && node &&
                                   node->add_component<Animator>());
}

void animation(const Arguments& arguments)
{
    auto* value = animator(arguments, 3);
    std::string path;
    if (value && BindingContext::text(arguments, 2, path))
    {
        arguments.GetReturnValue().Set(value->set_animation(path));
    }
}

void speed(const Arguments& arguments)
{
    auto* value = animator(arguments, 3);
    float number = 0;
    if (value && BindingContext::scalar(arguments, 2, number))
    {
        auto settings = value->settings();
        settings.speed = number;
        arguments.GetReturnValue().Set(value->set_settings(settings));
    }
}

void set_boolean(const Arguments& arguments, bool schema::AnimatorDataT::* field)
{
    auto* value = animator(arguments, 3);
    if (value && arguments[2]->IsBoolean())
    {
        auto settings = value->settings();
        settings.*field = arguments[2]->IsTrue();
        arguments.GetReturnValue().Set(value->set_settings(settings));
    }
}

void loop(const Arguments& arguments)
{
    set_boolean(arguments, &schema::AnimatorDataT::loop);
}

void awake(const Arguments& arguments)
{
    set_boolean(arguments, &schema::AnimatorDataT::play_on_awake);
}

void play(const Arguments& arguments)
{
    auto* value = animator(arguments, 2);
    arguments.GetReturnValue().Set(value && value->play());
}

void pause(const Arguments& arguments)
{
    auto* value = animator(arguments, 2);
    arguments.GetReturnValue().Set(value && value->pause());
}

void stop(const Arguments& arguments)
{
    auto* value = animator(arguments, 2);
    arguments.GetReturnValue().Set(value && value->stop());
}

void seek(const Arguments& arguments)
{
    auto* value = animator(arguments, 3);
    double seconds = 0;
    if (value && BindingContext::number(arguments, 2, seconds))
    {
        arguments.GetReturnValue().Set(value->seek(seconds));
    }
}

void playing(const Arguments& arguments)
{
    const auto* value = animator(arguments, 2);
    arguments.GetReturnValue().Set(value && value->playback() == AnimationPlayback::playing);
}

void playback(const Arguments& arguments)
{
    const auto* value = animator(arguments, 2);
    arguments.GetReturnValue().SetNull();
    if (!value)
    {
        return;
    }
    const char* state = nullptr;
    switch (value->playback())
    {
    case AnimationPlayback::stopped:
        state = "stopped";
        break;
    case AnimationPlayback::playing:
        state = "playing";
        break;
    case AnimationPlayback::paused:
        state = "paused";
        break;
    }
    v8::Local<v8::String> text;
    if (state && v8::String::NewFromUtf8(arguments.GetIsolate(), state).ToLocal(&text))
    {
        arguments.GetReturnValue().Set(text);
    }
}

void time(const Arguments& arguments)
{
    const auto* value = animator(arguments, 2);
    arguments.GetReturnValue().SetNull();
    if (value)
    {
        arguments.GetReturnValue().Set(value->time());
    }
}
} // namespace

std::string_view AnimatorBinding::name() const noexcept
{
    return "Animator";
}

std::string_view AnimatorBinding::source() const noexcept
{
    return javascript;
}

bool AnimatorBinding::register_functions(v8::Local<v8::Context> context,
                                         v8::Local<v8::Object> native) const
{
    return BindingContext::integer(context, native, "AnimatorKind",
                                   static_cast<int>(Animator::component_type)) &&
           BindingContext::function(context, native, "AnimatorAdd", add) &&
           BindingContext::function(context, native, "AnimatorAnimation", animation) &&
           BindingContext::function(context, native, "AnimatorSpeed", speed) &&
           BindingContext::function(context, native, "AnimatorLoop", loop) &&
           BindingContext::function(context, native, "AnimatorAwake", awake) &&
           BindingContext::function(context, native, "AnimatorPlay", play) &&
           BindingContext::function(context, native, "AnimatorPause", pause) &&
           BindingContext::function(context, native, "AnimatorStop", stop) &&
           BindingContext::function(context, native, "AnimatorSeek", seek) &&
           BindingContext::function(context, native, "AnimatorPlaying", playing) &&
           BindingContext::function(context, native, "AnimatorPlayback", playback) &&
           BindingContext::function(context, native, "AnimatorTime", time);
}
} // namespace engine::plugins::script::bindings
