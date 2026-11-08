#include "plugins/runtime/script/bindings/animator_binding.hpp"
#include "plugins/runtime/script/bindings/binding_helpers.hpp"

#include "plugins/runtime/script/bindings/binding_context.hpp"
#include "runtime/resource/resource.hpp"
#include "runtime/scene/animator.hpp"
#include "runtime/scene/node.hpp"
#include "runtime/scene/scene.hpp"
#include "runtime/world/world_system.hpp"

#include <cmath>
#include <limits>
#include <nlohmann/json.hpp>

namespace engine::plugins::script::bindings
{
namespace
{
constexpr std::string_view javascript = R"js(
const animationEventListeners = new Map();
let animationEventSequence = 0n;
bindingCleanupHooks.push(() => animationEventListeners.clear());
bindingEventHooks.push(() => {
    for (const [token, listener] of animationEventListeners) {
        const value = native.AnimatorSnapshot(listener.scene, listener.node);
        if (!value || value.instance !== listener.instance) animationEventListeners.delete(token);
    }
});
function animationIkInput(animator, name, target, twoBone) {
    if (!target || typeof target !== "object") return false;
    const h = handles.get(animator), nodeMode = target.kind === "node";
    if (target.kind !== undefined && target.kind !== "point" && !nodeMode) return false;
    if (target.space !== undefined && target.space !== "world" && target.space !== "model") return false;
    if (!nodeMode && target.node !== undefined && target.node !== null) return false;
    const n = nodeMode && target.node instanceof Node ? handles.get(target.node) : null;
    if (nodeMode && !n) return false;
    const p = target.point ?? {x:0,y:0,z:0}, o = target.localOffset ?? {x:0,y:0,z:0};
    const pole = target.pole ?? {x:0,y:0,z:0};
    return native.AnimatorIkSet(h.scene, h.node, name, twoBone, nodeMode,
        target.space === "model", p.x,p.y,p.z,n?.scene ?? 0n,n?.node ?? 0n,
        o.x,o.y,o.z,pole.x,pole.y,pole.z,target.weight ?? 0,target.enabled ?? false);
}
export function __dispatchAnimationEvents() {
    // Snapshot listeners per event; newly added subscriptions start on the next event.
    let event;
    while ((event = native.AnimatorTakeEvent()) !== null) {
        for (const [token, listener] of Array.from(animationEventListeners)) {
            const snapshot = native.AnimatorSnapshot(listener.scene, listener.node);
            if (!snapshot || snapshot.instance !== listener.instance) {
                animationEventListeners.delete(token); continue;
            }
            if (!snapshot.active || !animationEventListeners.has(token) || listener.scene !== event.sceneId ||
                listener.node !== event.nodeId || listener.instance !== event.componentInstanceId ||
                snapshot.playbackId !== event.playbackId ||
                snapshot.trackRevision !== event.trackRevision ||
                snapshot.animationGuid !== event.animationGuid ||
                snapshot.resourceGeneration !== event.resourceGeneration) continue;
            animationSynchronous(listener.callback(Object.freeze({...event})));
        }
    }
}
export class Animator extends Component {
    constructor(token, scene, node) { super(token, scene, node, native.AnimatorKind); }
    SetAnimation(path) {
        if (animationMachineOwns(this)) return false;
        const h = handles.get(this);
        return native.AnimatorAnimation(h.scene, h.node, path);
    }
    CrossFade(path, seconds) {
        if (animationMachineOwns(this)) return false;
        const h = handles.get(this);
        return native.AnimatorCrossFade(h.scene, h.node, path, seconds);
    }
    SetSpeed(value) {
        if (animationMachineOwns(this)) return false;
        const h = handles.get(this);
        return native.AnimatorSpeed(h.scene, h.node, value);
    }
    SetLoop(value) {
        if (animationMachineOwns(this)) return false;
        const h = handles.get(this);
        return native.AnimatorLoop(h.scene, h.node, value);
    }
    SetPlayOnAwake(value) {
        if (animationMachineOwns(this)) return false;
        const h = handles.get(this);
        return native.AnimatorAwake(h.scene, h.node, value);
    }
    Play() {
        if (animationMachineOwns(this)) return false;
        const h = handles.get(this);
        return native.AnimatorPlay(h.scene, h.node);
    }
    Pause() {
        if (animationMachineOwns(this)) return false;
        const h = handles.get(this);
        return native.AnimatorPause(h.scene, h.node);
    }
    Stop() {
        if (animationMachineOwns(this)) return false;
        const h = handles.get(this);
        return native.AnimatorStop(h.scene, h.node);
    }
    Seek(seconds) {
        if (animationMachineOwns(this)) return false;
        const h = handles.get(this);
        return native.AnimatorSeek(h.scene, h.node, seconds);
    }
    PlayAnimation(request) {
        if (animationMachineOwns(this) || !request || typeof request !== "object") return false;
        const h = handles.get(this);
        return native.AnimatorPlayAnimation(h.scene, h.node, request.animation,
            request.retargetProfile ?? "", request.fadeSeconds ?? 0, request.speed ?? 1,
            request.loop ?? true, request.restart ?? true);
    }
    SetStateMachine(definition, aliases) { return animationMachineBind(this, definition, aliases); }
    ClearStateMachine() { return animationMachineClear(this); }
    SetParameters(values) { return animationMachineSetParameters(this, values); }
    Trigger(name) { return animationMachineTrigger(this, name); }
    ResetTrigger(name) { return animationMachineTrigger(this, name, true); }
    SetStateMachinePaused(value) { return animationMachinePause(this, value); }
    get stateMachineState() { return animationMachineState(this); }
    get stateMachinePaused() { return animationMachinePaused(this); }
    get stateMachineTransitioning() { return animationMachineTransitioning(this); }
    GetPlaybackInfo() {
        const h = handles.get(this); return native.AnimatorSnapshot(h.scene, h.node);
    }
    SetIkEnabled(value) {
        const h = handles.get(this); return native.AnimatorIkEnabled(h.scene, h.node, value);
    }
    GetBonePose(path) {
        const h = handles.get(this); return native.AnimatorBonePose(h.scene, h.node, path);
    }
    GetSocketPose(name) {
        const h = handles.get(this); return native.AnimatorSocketPose(h.scene, h.node, name);
    }
    SetAimIkTarget(name, target) { return animationIkInput(this, name, target, false); }
    SetTwoBoneIkTarget(name, target) { return animationIkInput(this, name, target, true); }
    GetIkTarget(name) {
        const h = handles.get(this), result = native.AnimatorIkTarget(h.scene, h.node, name);
        if (result && result.kind === "node") {
            result.node = new Node(key, result.targetScene, result.targetNode);
            delete result.targetScene; delete result.targetNode;
        }
        return result;
    }
    SetEventMarkers(markers) {
        if (!Array.isArray(markers) || markers.length > 4096) return false;
        const values = [];
        for (const marker of markers) {
            if (!marker || !Number.isInteger(marker.markerId) || marker.markerId <= 0 ||
                marker.markerId > 4294967295 || !Number.isFinite(marker.time) ||
                typeof marker.name !== "string" || !marker.name.length) return false;
            const payload = marker.payload ?? null;
            if (payload !== null && typeof payload !== "string" && typeof payload !== "boolean" &&
                !(typeof payload === "number" && Number.isFinite(payload))) return false;
            values.push({markerId: marker.markerId, time: marker.time, name: marker.name, payload});
        }
        const h = handles.get(this);
        return native.AnimatorEventMarkers(h.scene, h.node, JSON.stringify(values));
    }
    ClearEventMarkers() {
        const h = handles.get(this); return native.AnimatorClearEventMarkers(h.scene, h.node);
    }
    SubscribeEvents(callback) {
        const h = handles.get(this);
        if (typeof callback !== "function" || !native.AnimatorSnapshot(h.scene, h.node) ||
            animationEventListeners.size >= 4096) return null;
        const token = ++animationEventSequence;
        animationEventListeners.set(token, { ...h, callback });
        return token;
    }
    UnsubscribeEvents(token) {
        const h = handles.get(this), listener = animationEventListeners.get(token);
        return !!listener && listener.scene === h.scene && listener.node === h.node &&
            listener.instance === h.instance && animationEventListeners.delete(token);
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

void add(const Arguments& arguments)
{
    auto node = BindingContext::node(arguments);
    arguments.set_return(arguments.count() == 2 && node && node.add_component<Animator>());
}

void animation(const Arguments& arguments)
{
    auto value = binding_detail::component<Animator>(arguments, 3);
    std::string path;
    if (value && BindingContext::text(arguments, 2, path))
    {
        arguments.set_return(value.set_animation(path));
    }
}

void cross_fade(const Arguments& arguments)
{
    auto value = binding_detail::component<Animator>(arguments, 4);
    std::string path;
    double seconds = 0.0;
    if (value.valid() && BindingContext::text(arguments, 2, path) &&
        BindingContext::number(arguments, 3, seconds))
    {
        arguments.set_return(value.cross_fade(path, seconds));
    }
}

void speed(const Arguments& arguments)
{
    auto value = binding_detail::component<Animator>(arguments, 3);
    float number = 0;
    if (value && BindingContext::scalar(arguments, 2, number))
    {
        Animator::Data settings{};
        if (!value.get_settings(settings))
        {
            arguments.set_return(false);
            return;
        }
        settings.speed = number;
        arguments.set_return(value.set_settings(settings));
    }
}

void set_boolean(const Arguments& arguments, bool schema::AnimatorDataT::* field)
{
    auto value = binding_detail::component<Animator>(arguments, 3);
    if (value && arguments.is_boolean(2))
    {
        Animator::Data settings{};
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
    set_boolean(arguments, &schema::AnimatorDataT::loop);
}

void awake(const Arguments& arguments)
{
    set_boolean(arguments, &schema::AnimatorDataT::play_on_awake);
}

void play(const Arguments& arguments)
{
    auto value = binding_detail::component<Animator>(arguments, 2);
    arguments.set_return(value && value.play());
}

void pause(const Arguments& arguments)
{
    auto value = binding_detail::component<Animator>(arguments, 2);
    arguments.set_return(value && value.pause());
}

void stop(const Arguments& arguments)
{
    auto value = binding_detail::component<Animator>(arguments, 2);
    arguments.set_return(value && value.stop());
}

void seek(const Arguments& arguments)
{
    auto value = binding_detail::component<Animator>(arguments, 3);
    double seconds = 0;
    if (value && BindingContext::number(arguments, 2, seconds))
    {
        arguments.set_return(value.seek(seconds));
    }
}

void playing(const Arguments& arguments)
{
    const auto value = binding_detail::component<Animator>(arguments, 2);
    arguments.set_return(value && value.playback() == AnimationPlayback::playing);
}

void playback(const Arguments& arguments)
{
    const auto value = binding_detail::component<Animator>(arguments, 2);
    arguments.set_return({});
    if (!value)
    {
        return;
    }
    const char* state = nullptr;
    switch (value.playback())
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
    const auto value = binding_detail::component<Animator>(arguments, 2);
    arguments.set_return({});
    if (value)
    {
        arguments.set_return(value.time());
    }
}
const char* playback_name(AnimationPlayback value)
{
    switch (value)
    {
    case AnimationPlayback::playing:
        return "playing";
    case AnimationPlayback::paused:
        return "paused";
    case AnimationPlayback::stopped:
        return "stopped";
    }
    return "stopped";
}

void snapshot(const Arguments& arguments)
{
    const auto value = binding_detail::component<Animator>(arguments, 2);
    arguments.set_return({});
    AnimatorPlaybackInfo info;
    const auto* context = BindingContext::current(arguments);
    if (!value || !context || !value.get_playback_info(info))
    {
        return;
    }
    arguments.set_return(
        vm::NativeValue::Object{{"animationGuid", info.animation_guid},
                                {"resourceGeneration", info.resource_generation},
                                {"trackRevision", info.track_revision},
                                {"instance", value.instance_id()},
                                {"playbackId", info.playback_id},
                                {"playback", playback_name(info.playback)},
                                {"time", info.time},
                                {"duration", info.duration},
                                {"normalizedTime", info.normalized_time},
                                {"completed", info.completed},
                                {"transitioning", info.transitioning},
                                {"active", value.active()},
                                {"worldPaused", !context->world() || context->world()->paused()}});
}

void play_animation(const Arguments& arguments)
{
    auto value = binding_detail::component<Animator>(arguments, 8);
    AnimationPlayRequest request;
    if (value && BindingContext::text(arguments, 2, request.animation_path) &&
        BindingContext::text(arguments, 3, request.retarget_profile_path) &&
        BindingContext::number(arguments, 4, request.fade_seconds) &&
        BindingContext::scalar(arguments, 5, request.speed) && arguments.is_boolean(6) &&
        arguments.is_boolean(7))
    {
        request.loop = arguments.boolean(6);
        request.restart = arguments.boolean(7);
        arguments.set_return(value.play_animation(request));
    }
}

void ik_enabled(const Arguments& arguments)
{
    auto value = binding_detail::component<Animator>(arguments, 3);
    if (value && arguments.is_boolean(2))
    {
        Animator::Data settings{};
        if (!value.get_settings(settings))
        {
            arguments.set_return(false);
            return;
        }
        settings.ik_enabled = arguments.boolean(2);
        arguments.set_return(value.set_settings(settings));
    }
}

vm::NativeValue vector_value(const Vec3& value)
{
    return vm::NativeValue::Object{{"x", value.x()}, {"y", value.y()}, {"z", value.z()}};
}

void ik_set(const Arguments& arguments)
{
    auto value = binding_detail::component<Animator>(arguments, 19);
    std::string name;
    AnimationIkTarget target;
    float px, py, pz, ox, oy, oz, qx, qy, qz;
    std::uint64_t scene = 0, node = 0;
    if (!value || !BindingContext::text(arguments, 2, name) || !arguments.is_boolean(3) ||
        !arguments.is_boolean(4) || !arguments.is_boolean(5) ||
        !BindingContext::scalar(arguments, 6, px) || !BindingContext::scalar(arguments, 7, py) ||
        !BindingContext::scalar(arguments, 8, pz) ||
        !BindingContext::identifier(arguments, 9, scene) ||
        !BindingContext::identifier(arguments, 10, node) ||
        !BindingContext::scalar(arguments, 11, ox) || !BindingContext::scalar(arguments, 12, oy) ||
        !BindingContext::scalar(arguments, 13, oz) || !BindingContext::scalar(arguments, 14, qx) ||
        !BindingContext::scalar(arguments, 15, qy) || !BindingContext::scalar(arguments, 16, qz) ||
        !BindingContext::scalar(arguments, 17, target.weight) || !arguments.is_boolean(18))
    {
        return;
    }
    target.kind = arguments.boolean(4) ? AnimationIkTargetKind::node : AnimationIkTargetKind::point;
    target.space = arguments.boolean(5) ? AnimationIkSpace::model : AnimationIkSpace::world;
    target.enabled = arguments.boolean(18);
    target.point = Vec3(px, py, pz);
    target.local_offset = Vec3(ox, oy, oz);
    target.pole = Vec3(qx, qy, qz);
    auto* context = BindingContext::current(arguments);
    if (scene != 0 || node != 0)
    {
        target.target_node = context->find_node(scene, node);
        if (!target.target_node)
        {
            return;
        }
    }
    arguments.set_return(arguments.boolean(3) ? value.set_two_bone_ik_target(name, target)
                                              : value.set_aim_ik_target(name, target));
}

void ik_target(const Arguments& arguments)
{
    const auto value = binding_detail::component<Animator>(arguments, 3);
    arguments.set_return({});
    std::string name;
    AnimationIkTarget target;
    if (!value || !BindingContext::text(arguments, 2, name) || !value.get_ik_target(name, target))
    {
        return;
    }
    arguments.set_return(vm::NativeValue::Object{
        {"kind", target.kind == AnimationIkTargetKind::node ? "node" : "point"},
        {"space", target.space == AnimationIkSpace::model ? "model" : "world"},
        {"point", vector_value(target.point)},
        {"localOffset", vector_value(target.local_offset)},
        {"pole", vector_value(target.pole)},
        {"weight", target.weight},
        {"enabled", target.enabled},
        {"targetNode", target.target_node ? target.target_node.id() : std::uint64_t{0}},
        {"targetScene", target.target_node ? target.target_node.scene()->id() : std::uint64_t{0}}});
}

void pose(const Arguments& arguments)
{
    const auto value = binding_detail::component<Animator>(arguments, 3);
    arguments.set_return({});
    std::string path;
    AnimationPoseTransform output;
    if (!value || !BindingContext::text(arguments, 2, path) ||
        !(arguments.data() ? value.get_socket_pose(path, output)
                           : value.get_bone_pose(path, output)))
    {
        return;
    }
    vm::NativeValue::Array model, world;
    for (const float element : output.model)
    {
        model.emplace_back(element);
    }
    for (const float element : output.world)
    {
        world.emplace_back(element);
    }
    arguments.set_return(vm::NativeValue::Object{{"model", std::move(model)},
                                                 {"world", std::move(world)},
                                                 {"poseRevision", output.pose_revision}});
}

void event_markers(const Arguments& arguments)
{
    auto value = binding_detail::component<Animator>(arguments, 3);
    std::string text;
    if (!value || !BindingContext::text(arguments, 2, text) || text.size() > 256 * 1024)
    {
        return;
    }
    const auto json = nlohmann::json::parse(text, nullptr, false);
    if (!json.is_array() || json.size() > 4096)
    {
        return;
    }
    std::vector<schema::AnimationEventMarkerDataT> markers;
    for (const auto& item : json)
    {
        if (!item.is_object() || !item.contains("markerId") ||
            !item["markerId"].is_number_unsigned() ||
            item["markerId"].get<std::uint64_t>() > std::numeric_limits<std::uint32_t>::max() ||
            !item.contains("time") || !item["time"].is_number() || !item.contains("name") ||
            !item["name"].is_string() || !item.contains("payload"))
        {
            return;
        }
        schema::AnimationEventMarkerDataT marker;
        marker.marker_id = item["markerId"].get<std::uint32_t>();
        const double seconds = item["time"].get<double>();
        if (!std::isfinite(seconds) || seconds < 0 || seconds > 86400)
        {
            return;
        }
        marker.time = static_cast<float>(seconds);
        marker.name = item["name"].get<std::string>();
        const auto& payload = item["payload"];
        if (payload.is_boolean())
        {
            schema::AnimationEventBooleanT data;
            data.value = payload.get<bool>();
            marker.payload.Set(std::move(data));
        }
        else if (payload.is_number())
        {
            schema::AnimationEventNumberT data;
            data.value = payload.get<double>();
            marker.payload.Set(std::move(data));
        }
        else if (payload.is_string())
        {
            schema::AnimationEventStringT data;
            data.value = payload.get<std::string>();
            marker.payload.Set(std::move(data));
        }
        else if (!payload.is_null())
        {
            return;
        }
        markers.push_back(std::move(marker));
    }
    arguments.set_return(value.set_event_markers(markers));
}

void clear_events(const Arguments& arguments)
{
    auto value = binding_detail::component<Animator>(arguments, 2);
    if (value)
    {
        value.clear_event_markers();
        arguments.set_return(true);
    }
}

void take_event(const Arguments& arguments)
{
    arguments.set_return({});
    auto* context = BindingContext::current(arguments);
    AnimationEvent event;
    if (!context || arguments.count() != 0 || !context->take_animation_event(event))
    {
        return;
    }
    vm::NativeValue payload;
    if (const auto* data = event.marker.payload.AsAnimationEventBoolean())
    {
        payload = data->value;
    }
    else if (const auto* data = event.marker.payload.AsAnimationEventNumber())
    {
        payload = data->value;
    }
    else if (const auto* data = event.marker.payload.AsAnimationEventString())
    {
        payload = data->value;
    }
    arguments.set_return(
        vm::NativeValue::Object{{"sceneId", event.scene_id},
                                {"nodeId", event.node_id},
                                {"componentInstanceId", event.component_instance_id},
                                {"playbackId", event.playback_id},
                                {"animationGuid", event.animation_guid},
                                {"resourceGeneration", event.resource_generation},
                                {"trackRevision", event.track_revision},
                                {"cycle", event.cycle},
                                {"markerId", event.marker.marker_id},
                                {"time", event.marker.time},
                                {"name", event.marker.name},
                                {"payload", std::move(payload)}});
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
           vm.function("AnimatorCrossFade", cross_fade) && vm.function("AnimatorSpeed", speed) &&
           vm.function("AnimatorLoop", loop) && vm.function("AnimatorAwake", awake) &&
           vm.function("AnimatorPlay", play) && vm.function("AnimatorPause", pause) &&
           vm.function("AnimatorStop", stop) && vm.function("AnimatorSeek", seek) &&
           vm.function("AnimatorPlaying", playing) && vm.function("AnimatorPlayback", playback) &&
           vm.function("AnimatorTime", time) && vm.function("AnimatorSnapshot", snapshot) &&
           vm.function("AnimatorPlayAnimation", play_animation) &&
           vm.function("AnimatorIkEnabled", ik_enabled) && vm.function("AnimatorIkSet", ik_set) &&
           vm.function("AnimatorIkTarget", ik_target) && vm.function("AnimatorBonePose", pose, 0) &&
           vm.function("AnimatorSocketPose", pose, 1) &&
           vm.function("AnimatorEventMarkers", event_markers) &&
           vm.function("AnimatorClearEventMarkers", clear_events) &&
           vm.function("AnimatorTakeEvent", take_event);
}
} // namespace engine::plugins::script::bindings
