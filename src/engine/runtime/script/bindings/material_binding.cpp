#include "runtime/script/bindings/material_binding.hpp"

#include "runtime/scene/mesh_renderer.hpp"
#include "runtime/scene/node.hpp"
#include "runtime/script/bindings/binding_context.hpp"

namespace engine::script_bindings
{
namespace
{
constexpr std::string_view javascript = R"js(
export class Material {
    constructor(token, scene, node) { attach(this, token, scene, node, native.MeshRendererKind); }
    IsValid() {
        const h = handles.get(this); return native.ComponentValid(h.scene, h.node, h.kind);
    }
    SetType(type) {
        const h = handles.get(this); return native.MaterialType(h.scene, h.node, type);
    }
    SetShader(path) {
        const h = handles.get(this); return native.MaterialShader(h.scene, h.node, path);
    }
    SetFloat(name, value) {
        const h = handles.get(this); return native.MaterialFloat(h.scene, h.node, name, value);
    }
    SetV2(name, x, y) {
        const h = handles.get(this); return native.MaterialV2(h.scene, h.node, name, x, y);
    }
    SetV3(name, x, y, z) {
        const h = handles.get(this); return native.MaterialV3(h.scene, h.node, name, x, y, z);
    }
    SetV4(name, x, y, z, w) {
        const h = handles.get(this); return native.MaterialV4(h.scene, h.node, name, x, y, z, w);
    }
}
Object.freeze(Material.prototype);
Object.freeze(Material);
)js";

Material* material(const Arguments& arguments, int count, std::string& name)
{
    arguments.GetReturnValue().Set(false);
    auto* node = BindingContext::node(arguments);
    auto* mesh = node == nullptr ? nullptr : node->get_component<MeshRenderer>();
    return mesh != nullptr && arguments.Length() == count &&
                   BindingContext::text(arguments, 2, name)
               ? &mesh->material()
               : nullptr;
}

void type(const Arguments& arguments)
{
    std::string path;
    auto* value = material(arguments, 3, path);
    arguments.GetReturnValue().Set(value != nullptr && value->set_type(path));
}

void shader(const Arguments& arguments)
{
    std::string path;
    auto* value = material(arguments, 3, path);
    arguments.GetReturnValue().Set(value != nullptr && value->set_shader(path));
}

void set_float(const Arguments& arguments)
{
    std::string name;
    auto* value = material(arguments, 4, name);
    float number = 0.0F;
    arguments.GetReturnValue().Set(value != nullptr &&
                                   BindingContext::scalar(arguments, 3, number) &&
                                   value->set_float(name, number));
}

void set_v2(const Arguments& arguments)
{
    std::string name;
    auto* value = material(arguments, 5, name);
    double x = 0.0;
    double y = 0.0;
    arguments.GetReturnValue().Set(value != nullptr && BindingContext::number(arguments, 3, x) &&
                                   BindingContext::number(arguments, 4, y) &&
                                   value->set_v2(name, Vec2(x, y)));
}

void set_v3(const Arguments& arguments)
{
    std::string name;
    auto* value = material(arguments, 6, name);
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    arguments.GetReturnValue().Set(value != nullptr && BindingContext::number(arguments, 3, x) &&
                                   BindingContext::number(arguments, 4, y) &&
                                   BindingContext::number(arguments, 5, z) &&
                                   value->set_v3(name, Vec3(x, y, z)));
}

void set_v4(const Arguments& arguments)
{
    std::string name;
    auto* value = material(arguments, 7, name);
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    double w = 0.0;
    arguments.GetReturnValue().Set(
        value != nullptr && BindingContext::number(arguments, 3, x) &&
        BindingContext::number(arguments, 4, y) && BindingContext::number(arguments, 5, z) &&
        BindingContext::number(arguments, 6, w) && value->set_v4(name, Vec4(x, y, z, w)));
}
} // namespace

std::string_view MaterialBinding::name() const noexcept
{
    return "Material";
}

std::string_view MaterialBinding::source() const noexcept
{
    return javascript;
}

bool MaterialBinding::register_functions(v8::Local<v8::Context> context,
                                         v8::Local<v8::Object> native) const
{
    return BindingContext::function(context, native, "MaterialType", type) &&
           BindingContext::function(context, native, "MaterialShader", shader) &&
           BindingContext::function(context, native, "MaterialFloat", set_float) &&
           BindingContext::function(context, native, "MaterialV2", set_v2) &&
           BindingContext::function(context, native, "MaterialV3", set_v3) &&
           BindingContext::function(context, native, "MaterialV4", set_v4);
}

} // namespace engine::script_bindings
