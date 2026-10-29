#include "runtime/script/bindings/script_binding_registry.hpp"

#include "runtime/script/bindings/app_binding.hpp"
#include "runtime/script/bindings/binding_context.hpp"
#include "runtime/script/bindings/camera_binding.hpp"
#include "runtime/script/bindings/component_binding.hpp"
#include "runtime/script/bindings/debug_binding.hpp"
#include "runtime/script/bindings/gameplay_binding.hpp"
#include "runtime/script/bindings/light_binding.hpp"
#include "runtime/script/bindings/material_binding.hpp"
#include "runtime/script/bindings/mesh_renderer_binding.hpp"
#include "runtime/script/bindings/node_binding.hpp"
#include "runtime/script/bindings/scene_binding.hpp"
#include "runtime/script/bindings/time_binding.hpp"
#include "runtime/script/bindings/transform_binding.hpp"

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
    std::unique_ptr<ScriptBinding> defaults[] = {
        std::make_unique<GameplayBinding>(),     std::make_unique<DebugBinding>(),
        std::make_unique<AppBinding>(),          std::make_unique<TimeBinding>(),
        std::make_unique<SceneBinding>(),        std::make_unique<NodeBinding>(),
        std::make_unique<ComponentBinding>(),    std::make_unique<TransformBinding>(),
        std::make_unique<CameraBinding>(),       std::make_unique<LightBinding>(),
        std::make_unique<MeshRendererBinding>(), std::make_unique<MaterialBinding>()};
    for (auto& binding : defaults)
    {
        if (!add_binding(binding))
        {
            bindings_.clear();
            return false;
        }
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
