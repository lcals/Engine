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
    arguments.set_return(false);
    auto* node = BindingContext::node(arguments);
    return node && arguments.count() == count ? node->get_component<Animator>() : nullptr;
}

void add(const Arguments& arguments)
{
    auto* node = BindingContext::node(arguments);
    arguments.set_return(arguments.count() == 2 && node && node->add_component<Animator>());
}

void animation(const Arguments& arguments)
{
    auto* value = animator(arguments, 3);
    std::string path;
    if (value && BindingContext::text(arguments, 2, path))
    {
        arguments.set_return(value->set_animation(path));
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
        arguments.set_return(value->set_settings(settings));
    }
}

void set_boolean(const Arguments& arguments, bool schema::AnimatorDataT::* field)
{
    auto* value = animator(arguments, 3);
    if (value && arguments.is_boolean(2))
    {
        auto settings = value->settings();
        settings.*field = arguments.boolean(2);
        arguments.set_return(value->set_settings(settings));
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
    arguments.set_return(value && value->play());
}

void pause(const Arguments& arguments)
{
    auto* value = animator(arguments, 2);
    arguments.set_return(value && value->pause());
}

void stop(const Arguments& arguments)
{
    auto* value = animator(arguments, 2);
    arguments.set_return(value && value->stop());
}

void seek(const Arguments& arguments)
{
    auto* value = animator(arguments, 3);
    double seconds = 0;
    if (value && BindingContext::number(arguments, 2, seconds))
    {
        arguments.set_return(value->seek(seconds));
    }
}

void playing(const Arguments& arguments)
{
    const auto* value = animator(arguments, 2);
    arguments.set_return(value && value->playback() == AnimationPlayback::playing);
}

void playback(const Arguments& arguments)
{
    const auto* value = animator(arguments, 2);
    arguments.set_return({});
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
    if (state != nullptr)
    {
        arguments.set_return(state);
    }
}

void time(const Arguments& arguments)
{
    const auto* value = animator(arguments, 2);
    arguments.set_return({});
    if (value)
    {
        arguments.set_return(value->time());
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

bool AnimatorBinding::register_functions(vm::ScriptVm& vm) const
{
    return vm.integer("AnimatorKind", static_cast<int>(Animator::component_type)) &&
           vm.function("AnimatorAdd", add) && vm.function("AnimatorAnimation", animation) &&
           vm.function("AnimatorSpeed", speed) && vm.function("AnimatorLoop", loop) &&
           vm.function("AnimatorAwake", awake) && vm.function("AnimatorPlay", play) &&
           vm.function("AnimatorPause", pause) && vm.function("AnimatorStop", stop) &&
           vm.function("AnimatorSeek", seek) && vm.function("AnimatorPlaying", playing) &&
           vm.function("AnimatorPlayback", playback) && vm.function("AnimatorTime", time);
}
} // namespace engine::plugins::script::bindings
