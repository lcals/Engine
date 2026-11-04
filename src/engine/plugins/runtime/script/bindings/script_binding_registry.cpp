#include "plugins/runtime/script/bindings/script_binding_registry.hpp"
#include "plugins/runtime/script/bindings/animation_state_machine_binding.hpp"
#include "plugins/runtime/script/bindings/animator_binding.hpp"
#include "plugins/runtime/script/bindings/bone_attachment_binding.hpp"

#include "plugins/runtime/script/bindings/app_binding.hpp"
#include "plugins/runtime/script/bindings/audio_listener_binding.hpp"
#include "plugins/runtime/script/bindings/audio_source_binding.hpp"
#include "plugins/runtime/script/bindings/camera_binding.hpp"
#include "plugins/runtime/script/bindings/character_motor_binding.hpp"
#include "plugins/runtime/script/bindings/collider_binding.hpp"
#include "plugins/runtime/script/bindings/completion_binding.hpp"
#include "plugins/runtime/script/bindings/component_binding.hpp"
#include "plugins/runtime/script/bindings/debug_binding.hpp"
#include "plugins/runtime/script/bindings/engine_binding.hpp"
#include "plugins/runtime/script/bindings/environment_component_binding.hpp"
#include "plugins/runtime/script/bindings/gameplay_binding.hpp"
#include "plugins/runtime/script/bindings/input_binding.hpp"
#include "plugins/runtime/script/bindings/json_binding.hpp"
#include "plugins/runtime/script/bindings/light_binding.hpp"
#include "plugins/runtime/script/bindings/light_probe_volume_component_binding.hpp"
#include "plugins/runtime/script/bindings/material_binding.hpp"
#include "plugins/runtime/script/bindings/mesh_renderer_binding.hpp"
#include "plugins/runtime/script/bindings/navigation_binding.hpp"
#include "plugins/runtime/script/bindings/node_binding.hpp"
#include "plugins/runtime/script/bindings/physics_binding.hpp"
#include "plugins/runtime/script/bindings/planar_reflection_component_binding.hpp"
#include "plugins/runtime/script/bindings/post_process_component_binding.hpp"
#include "plugins/runtime/script/bindings/render_binding.hpp"
#include "plugins/runtime/script/bindings/rigid_body_binding.hpp"
#include "plugins/runtime/script/bindings/scene_binding.hpp"
#include "plugins/runtime/script/bindings/storage_binding.hpp"
#include "plugins/runtime/script/bindings/time_binding.hpp"
#include "plugins/runtime/script/bindings/transform_binding.hpp"
#include "plugins/runtime/script/bindings/ui_component_binding.hpp"
#include "plugins/runtime/script/bindings/ui_controller_binding.hpp"
#include "plugins/runtime/script/bindings/ui_element_binding.hpp"
#include "plugins/runtime/script/bindings/window_binding.hpp"

#if defined(ENGINE_HAS_TERRAIN_PLUGIN) && ENGINE_HAS_TERRAIN_PLUGIN
#include "plugins/runtime/script/bindings/terrain_binding.hpp"
#endif
#if defined(ENGINE_HAS_CHARACTER_CONTROLLER_PLUGIN) && ENGINE_HAS_CHARACTER_CONTROLLER_PLUGIN
#include "plugins/runtime/script/bindings/character_controller_binding.hpp"
#endif
#if defined(ENGINE_HAS_NAVIGATION_CROWD_PLUGIN) && ENGINE_HAS_NAVIGATION_CROWD_PLUGIN
#include "plugins/runtime/script/bindings/navigation_agent_binding.hpp"
#endif
#if defined(ENGINE_HAS_DESTRUCTION_PLUGIN) && ENGINE_HAS_DESTRUCTION_PLUGIN
#include "plugins/runtime/script/bindings/destruction_binding.hpp"
#endif

#if defined(ENGINE_HAS_EFFECTS_PLUGIN) && ENGINE_HAS_EFFECTS_PLUGIN
#include "plugins/runtime/script/bindings/effect_binding.hpp"
#endif

#include <algorithm>
#include <utility>

namespace engine::plugins::script::bindings
{

bool ScriptBindingRegistry::initialize()
{
    if (!bindings_.empty())
    {
        return false;
    }
    // 新增绑定仅在此注册实现类；各类自行注册native方法与组件工厂。
    if (!add<CompletionBinding>() || !add<GameplayBinding>() || !add<DebugBinding>() ||
        !add<AppBinding>() || !add<WindowBinding>() || !add<StorageBinding>() ||
        !add<JsonBinding>() || !add<InputBinding>() || !add<PhysicsBinding>() ||
        !add<NavigationBinding>() || !add<EngineBinding>() || !add<TimeBinding>() ||
        !add<RenderBinding>() || !add<SceneBinding>() || !add<NodeBinding>() ||
        !add<ComponentBinding>() || !add<TransformBinding>() || !add<CameraBinding>() ||
        !add<EnvironmentComponentBinding>() || !add<PostProcessComponentBinding>() ||
        !add<PlanarReflectionComponentBinding>() || !add<LightProbeVolumeComponentBinding>() ||
        !add<LightBinding>() || !add<MeshRendererBinding>() || !add<MaterialBinding>() ||
        !add<ColliderBinding>() || !add<RigidBodyBinding>() || !add<CharacterMotorBinding>() ||
        !add<AudioSourceBinding>() || !add<AudioListenerBinding>() || !add<AnimatorBinding>() ||
        !add<BoneAttachmentBinding>() || !add<UiComponentBinding>() || !add<UiElementBinding>() || !add<UiControllerBinding>() ||
        !add<AnimationStateMachineBinding>())
    {
        bindings_.clear();
        return false;
    }
#if defined(ENGINE_HAS_TERRAIN_PLUGIN) && ENGINE_HAS_TERRAIN_PLUGIN
    if (!add<TerrainBinding>())
    {
        bindings_.clear();
        return false;
    }
#endif
#if defined(ENGINE_HAS_CHARACTER_CONTROLLER_PLUGIN) && ENGINE_HAS_CHARACTER_CONTROLLER_PLUGIN
    if (!add<CharacterControllerBinding>())
    {
        bindings_.clear();
        return false;
    }
#endif
#if defined(ENGINE_HAS_NAVIGATION_CROWD_PLUGIN) && ENGINE_HAS_NAVIGATION_CROWD_PLUGIN
    if (!add<NavigationAgentBinding>())
    {
        bindings_.clear();
        return false;
    }
#endif
#if defined(ENGINE_HAS_DESTRUCTION_PLUGIN) && ENGINE_HAS_DESTRUCTION_PLUGIN
    if (!add<DestructionBinding>())
    {
        bindings_.clear();
        return false;
    }
#endif
#if defined(ENGINE_HAS_EFFECTS_PLUGIN) && ENGINE_HAS_EFFECTS_PLUGIN
    if (!add<EffectBinding>())
    {
        bindings_.clear();
        return false;
    }
#endif
    return true;
}

bool ScriptBindingRegistry::add_binding(std::unique_ptr<ScriptBinding>& binding)
{
    if (binding == nullptr || binding->name().empty() ||
        std::any_of(bindings_.begin(), bindings_.end(), [&binding](const auto& existing)
                    { return existing->name() == binding->name(); }))
    {
        return false;
    }
    bindings_.push_back(std::move(binding));
    return true;
}

bool ScriptBindingRegistry::register_functions(vm::ScriptVm& vm) const
{
    if (bindings_.empty())
    {
        return false;
    }
    for (const auto& binding : bindings_)
    {
        if (!binding->register_functions(vm))
        {
            return false;
        }
    }
    return vm.seal_bindings();
}

std::string ScriptBindingRegistry::source() const
{
    std::string result = R"js(
const native = globalThis.__engineNative;
const key = {};
const handleRecords = new WeakMap();
const handles = {
    get(object) {
        const handle = handleRecords.get(object);
        if (handle === undefined || handle.kind <= 0) return handle;
        if (handle.instance !== 0n &&
            native.ComponentIdentity(handle.scene, handle.node, handle.kind) === handle.instance &&
            (handle.material === undefined || (handle.material !== 0n &&
             native.MaterialIdentity(handle.scene, handle.node, handle.slot) === handle.material)))
            return handle;
        return { ...handle, scene: 0n, node: 0n };
    }
};
const componentTypes = new Map();
const bindingEventHooks = [];
const bindingCleanupHooks = [];
function attach(object, token, scene, node = 0n, kind = -1) {
    if (token !== key) throw new TypeError('Use scene and node factory methods');
    const instance = kind > 0 ? native.ComponentIdentity(scene, node, kind) : 0n;
    handleRecords.set(object, { scene, node, kind, instance });
}
)js";
    for (const auto& binding : bindings_)
    {
        result += binding->source();
        result += '\n';
    }
    result += R"js(
export function __dispatchBindingEvents() {
    for (const dispatch of bindingEventHooks) dispatch();
}
export function __clearBindings() {
    let failed = false, failure;
    for (const cleanup of bindingCleanupHooks) {
        try { cleanup(); }
        catch (error) { if (!failed) { failed = true; failure = error; } }
    }
    if (failed) throw failure;
}
)js";
    return result;
}

} // namespace engine::plugins::script::bindings
