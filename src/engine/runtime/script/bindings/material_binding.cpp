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
    constructor(token, scene, node, slot = 0) {
        attach(this, token, scene, node, native.MeshRendererKind);
        handles.get(this).slot = slot;
    }
    IsValid() {
        const h = handles.get(this); return native.MaterialValid(h.scene, h.node, h.slot);
    }
    SetType(type) {
        const h = handles.get(this); return native.MaterialType(h.scene, h.node, h.slot, type);
    }
    SetShader(path) {
        const h = handles.get(this); return native.MaterialShader(h.scene, h.node, h.slot, path);
    }
    SetFloat(name, value) {
        const h = handles.get(this); return native.MaterialFloat(h.scene, h.node, h.slot, name, value);
    }
    SetV2(name, x, y) {
        const h = handles.get(this); return native.MaterialV2(h.scene, h.node, h.slot, name, x, y);
    }
    SetV3(name, x, y, z) {
        const h = handles.get(this); return native.MaterialV3(h.scene, h.node, h.slot, name, x, y, z);
    }
    SetV4(name, x, y, z, w) {
        const h = handles.get(this); return native.MaterialV4(h.scene, h.node, h.slot, name, x, y, z, w);
    }
    SetTexture(name, path) {
        const h = handles.get(this); return native.MaterialTexture(h.scene, h.node, h.slot, name, path);
    }
    GetTexture(name) {
        const h = handles.get(this); return native.MaterialGetTexture(h.scene, h.node, h.slot, name);
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
    return mesh != nullptr && arguments.Length() == count && arguments[2]->IsUint32() &&
                   BindingContext::text(arguments, 3, name)
               ? mesh->material(arguments[2].As<v8::Uint32>()->Value())
               : nullptr;
}

void valid(const Arguments& arguments)
{
    auto* node = BindingContext::node(arguments);
    auto* mesh = node == nullptr ? nullptr : node->get_component<MeshRenderer>();
    arguments.GetReturnValue().Set(
        mesh != nullptr && arguments.Length() == 3 && arguments[2]->IsUint32() &&
        mesh->material(arguments[2].As<v8::Uint32>()->Value()) != nullptr);
}

void set_texture(const Arguments& arguments)
{
    std::string name;
    std::string path;
    auto* value = material(arguments, 5, name);
    arguments.GetReturnValue().Set(value != nullptr && BindingContext::text(arguments, 4, path) &&
                                   value->set_texture(name, path));
}

void get_texture(const Arguments& arguments)
{
    std::string name;
    auto* value = material(arguments, 4, name);
    arguments.GetReturnValue().SetNull();
    std::string_view path;
    v8::Local<v8::String> output;
    if (value != nullptr && value->get_texture(name, path) &&
        v8::String::NewFromUtf8(arguments.GetIsolate(), path.data(), v8::NewStringType::kNormal,
                                static_cast<int>(path.size()))
            .ToLocal(&output))
    {
        arguments.GetReturnValue().Set(output);
    }
}

void type(const Arguments& arguments)
{
    std::string path;
    auto* value = material(arguments, 4, path);
    arguments.GetReturnValue().Set(value != nullptr && value->set_type(path));
}

void shader(const Arguments& arguments)
{
    std::string path;
    auto* value = material(arguments, 4, path);
    arguments.GetReturnValue().Set(value != nullptr && value->set_shader(path));
}

void set_float(const Arguments& arguments)
{
    std::string name;
    auto* value = material(arguments, 5, name);
    float number = 0.0F;
    arguments.GetReturnValue().Set(value != nullptr &&
                                   BindingContext::scalar(arguments, 4, number) &&
                                   value->set_float(name, number));
}

void set_v2(const Arguments& arguments)
{
    std::string name;
    auto* value = material(arguments, 6, name);
    double x = 0.0;
    double y = 0.0;
    arguments.GetReturnValue().Set(value != nullptr && BindingContext::number(arguments, 4, x) &&
                                   BindingContext::number(arguments, 5, y) &&
                                   value->set_v2(name, Vec2(x, y)));
}

void set_v3(const Arguments& arguments)
{
    std::string name;
    auto* value = material(arguments, 7, name);
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    arguments.GetReturnValue().Set(value != nullptr && BindingContext::number(arguments, 4, x) &&
                                   BindingContext::number(arguments, 5, y) &&
                                   BindingContext::number(arguments, 6, z) &&
                                   value->set_v3(name, Vec3(x, y, z)));
}

void set_v4(const Arguments& arguments)
{
    std::string name;
    auto* value = material(arguments, 8, name);
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    double w = 0.0;
    arguments.GetReturnValue().Set(
        value != nullptr && BindingContext::number(arguments, 4, x) &&
        BindingContext::number(arguments, 5, y) && BindingContext::number(arguments, 6, z) &&
        BindingContext::number(arguments, 7, w) && value->set_v4(name, Vec4(x, y, z, w)));
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
    return BindingContext::function(context, native, "MaterialValid", valid) &&
           BindingContext::function(context, native, "MaterialTexture", set_texture) &&
           BindingContext::function(context, native, "MaterialGetTexture", get_texture) &&
           BindingContext::function(context, native, "MaterialType", type) &&
           BindingContext::function(context, native, "MaterialShader", shader) &&
           BindingContext::function(context, native, "MaterialFloat", set_float) &&
           BindingContext::function(context, native, "MaterialV2", set_v2) &&
           BindingContext::function(context, native, "MaterialV3", set_v3) &&
           BindingContext::function(context, native, "MaterialV4", set_v4);
}

} // namespace engine::script_bindings
