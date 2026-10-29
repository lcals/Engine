#include "plugins/runtime/script/bindings/script_binding_registry.hpp"

#include "plugins/runtime/script/bindings/app_binding.hpp"
#include "plugins/runtime/script/bindings/binding_context.hpp"
#include "plugins/runtime/script/bindings/camera_binding.hpp"
#include "plugins/runtime/script/bindings/component_binding.hpp"
#include "plugins/runtime/script/bindings/debug_binding.hpp"
#include "plugins/runtime/script/bindings/gameplay_binding.hpp"
#include "plugins/runtime/script/bindings/light_binding.hpp"
#include "plugins/runtime/script/bindings/material_binding.hpp"
#include "plugins/runtime/script/bindings/mesh_renderer_binding.hpp"
#include "plugins/runtime/script/bindings/node_binding.hpp"
#include "plugins/runtime/script/bindings/scene_binding.hpp"
#include "plugins/runtime/script/bindings/time_binding.hpp"
#include "plugins/runtime/script/bindings/transform_binding.hpp"

#include <algorithm>
#include <utility>

namespace engine::script_bindings
{

bool ScriptBindingRegistry::initialize()
{
    if (!bindings_.empty())
    {
        return false;
    }
    // 新增绑定仅在此注册实现类；各类自行注册native方法与组件工厂。
    if (!add<GameplayBinding>() || !add<DebugBinding>() || !add<AppBinding>() ||
        !add<TimeBinding>() || !add<SceneBinding>() || !add<NodeBinding>() ||
        !add<ComponentBinding>() || !add<TransformBinding>() || !add<CameraBinding>() ||
        !add<LightBinding>() || !add<MeshRendererBinding>() || !add<MaterialBinding>())
    {
        bindings_.clear();
        return false;
    }
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

bool ScriptBindingRegistry::register_functions(v8::Local<v8::Context> context) const
{
    if (context.IsEmpty() || bindings_.empty())
    {
        return false;
    }
    const auto native = v8::Object::New(v8::Isolate::GetCurrent());
    for (const auto& binding : bindings_)
    {
        if (!binding->register_functions(context, native))
        {
            return false;
        }
    }
    return native->SetIntegrityLevel(context, v8::IntegrityLevel::kFrozen).FromMaybe(false) &&
           BindingContext::define(context, context->Global(), "__engineNative", native);
}

std::string ScriptBindingRegistry::source() const
{
    std::string result = R"js(
const native = globalThis.__engineNative;
const key = {};
const handles = new WeakMap();
const componentTypes = new Map();
function attach(object, token, scene, node = 0n, kind = -1) {
    if (token !== key) throw new TypeError('Use scene and node factory methods');
    handles.set(object, { scene, node, kind });
}
)js";
    for (const auto& binding : bindings_)
    {
        result += binding->source();
        result += '\n';
    }
    return result;
}

} // namespace engine::script_bindings
