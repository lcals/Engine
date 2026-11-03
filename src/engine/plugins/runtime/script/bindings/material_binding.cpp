#include "plugins/runtime/script/bindings/material_binding.hpp"

#include "plugins/runtime/script/bindings/binding_context.hpp"
#include "runtime/scene/mesh_renderer.hpp"
#include "runtime/scene/node.hpp"

namespace engine::plugins::script::bindings
{
namespace
{
constexpr std::string_view javascript = R"js(
export class Material {
    constructor(token, scene, node, slot = 0) {
        attach(this, token, scene, node, native.MeshRendererKind);
        const handle = handleRecords.get(this);
        handle.slot = slot;
        handle.material = native.MaterialIdentity(scene, node, slot);
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
    SetCullMode(mode) {
        const h = handles.get(this); return native.MaterialCullMode(h.scene, h.node, h.slot, mode);
    }
    GetCullMode() {
        const h = handles.get(this); return native.MaterialGetCullMode(h.scene, h.node, h.slot);
    }
    SetBlendMode(mode) {
        const h = handles.get(this); return native.MaterialBlendMode(h.scene, h.node, h.slot, mode);
    }
    GetBlendMode() {
        const h = handles.get(this); return native.MaterialGetBlendMode(h.scene, h.node, h.slot);
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

Material material(const Arguments& arguments, int count, std::string& name)
{
    arguments.set_return(false);
    auto node = BindingContext::node(arguments);
    auto mesh = !node ? MeshRenderer{} : node.get_component<MeshRenderer>();
    return mesh.valid() && arguments.count() == count && arguments.is_uint32(2) &&
                   BindingContext::text(arguments, 3, name)
               ? mesh.material(arguments.unsigned_integer(2))
               : Material{};
}

void valid(const Arguments& arguments)
{
    auto node = BindingContext::node(arguments);
    auto mesh = !node ? MeshRenderer{} : node.get_component<MeshRenderer>();
    arguments.set_return(mesh.valid() && arguments.count() == 3 && arguments.is_uint32(2) &&
                         mesh.material(arguments.unsigned_integer(2)).valid());
}

void identity(const Arguments& arguments)
{
    const auto node = BindingContext::node(arguments);
    const auto mesh = node ? node.get_component<MeshRenderer>() : MeshRenderer{};
    arguments.set_return(mesh && arguments.count() == 3 && arguments.is_uint32(2)
                             ? mesh.material(arguments.unsigned_integer(2)).instance_id()
                             : std::uint64_t{0});
}

void set_texture(const Arguments& arguments)
{
    std::string name;
    std::string path;
    auto value = material(arguments, 5, name);
    arguments.set_return(value.valid() && BindingContext::text(arguments, 4, path) &&
                         value.set_texture(name, path));
}

void get_texture(const Arguments& arguments)
{
    std::string name;
    auto value = material(arguments, 4, name);
    arguments.set_return({});
    std::string_view path;
    if (value.valid() && value.get_texture(name, path))
    {
        arguments.set_return(path);
    }
}

void type(const Arguments& arguments)
{
    std::string path;
    auto value = material(arguments, 4, path);
    arguments.set_return(value.valid() && value.set_type(path));
}

void shader(const Arguments& arguments)
{
    std::string path;
    auto value = material(arguments, 4, path);
    arguments.set_return(value.valid() && value.set_shader(path));
}

void set_cull_mode(const Arguments& arguments)
{
    std::string mode;
    auto value = material(arguments, 4, mode);
    if (!value || (mode != "back" && mode != "front" && mode != "none"))
    {
        return;
    }
    const auto cull = mode == "back"    ? schema::CullMode::back
                      : mode == "front" ? schema::CullMode::front
                                        : schema::CullMode::none;
    arguments.set_return(value.set_cull_mode(cull));
}

void get_cull_mode(const Arguments& arguments)
{
    arguments.set_return({});
    auto node = BindingContext::node(arguments);
    auto mesh = node.valid() ? node.get_component<MeshRenderer>() : MeshRenderer{};
    if (!mesh || arguments.count() != 3 || !arguments.is_uint32(2))
    {
        return;
    }
    if (const auto value = mesh.material(arguments.unsigned_integer(2)))
    {
        arguments.set_return(std::string_view(schema::EnumNameCullMode(value.cull_mode())));
    }
}

void set_blend_mode(const Arguments& arguments)
{
    std::string mode;
    auto value = material(arguments, 4, mode);
    if (!value || (mode != "opaque" && mode != "alpha_blend" && mode != "premultiplied"))
    {
        return;
    }
    const auto blend = mode == "opaque"        ? schema::BlendMode::opaque
                       : mode == "alpha_blend" ? schema::BlendMode::alpha_blend
                                               : schema::BlendMode::premultiplied;
    arguments.set_return(value.set_blend_mode(blend));
}

void get_blend_mode(const Arguments& arguments)
{
    arguments.set_return({});
    auto node = BindingContext::node(arguments);
    auto mesh = node ? node.get_component<MeshRenderer>() : MeshRenderer{};
    if (mesh && arguments.count() == 3 && arguments.is_uint32(2))
    {
        if (const auto value = mesh.material(arguments.unsigned_integer(2)))
        {
            arguments.set_return(std::string_view(schema::EnumNameBlendMode(value.blend_mode())));
        }
    }
}

void set_float(const Arguments& arguments)
{
    std::string name;
    auto value = material(arguments, 5, name);
    float number = 0.0F;
    arguments.set_return(value.valid() && BindingContext::scalar(arguments, 4, number) &&
                         value.set_float(name, number));
}

void set_v2(const Arguments& arguments)
{
    std::string name;
    auto value = material(arguments, 6, name);
    double x = 0.0;
    double y = 0.0;
    arguments.set_return(value.valid() && BindingContext::number(arguments, 4, x) &&
                         BindingContext::number(arguments, 5, y) && value.set_v2(name, Vec2(x, y)));
}

void set_v3(const Arguments& arguments)
{
    std::string name;
    auto value = material(arguments, 7, name);
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    arguments.set_return(value.valid() && BindingContext::number(arguments, 4, x) &&
                         BindingContext::number(arguments, 5, y) &&
                         BindingContext::number(arguments, 6, z) &&
                         value.set_v3(name, Vec3(x, y, z)));
}

void set_v4(const Arguments& arguments)
{
    std::string name;
    auto value = material(arguments, 8, name);
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    double w = 0.0;
    arguments.set_return(
        value.valid() && BindingContext::number(arguments, 4, x) &&
        BindingContext::number(arguments, 5, y) && BindingContext::number(arguments, 6, z) &&
        BindingContext::number(arguments, 7, w) && value.set_v4(name, Vec4(x, y, z, w)));
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

bool MaterialBinding::register_functions(vm::ScriptVm& vm) const
{
    return vm.function("MaterialValid", valid) && vm.function("MaterialIdentity", identity) &&
           vm.function("MaterialTexture", set_texture) &&
           vm.function("MaterialGetTexture", get_texture) && vm.function("MaterialType", type) &&
           vm.function("MaterialShader", shader) && vm.function("MaterialFloat", set_float) &&
           vm.function("MaterialCullMode", set_cull_mode) &&
           vm.function("MaterialGetCullMode", get_cull_mode) &&
           vm.function("MaterialBlendMode", set_blend_mode) &&
           vm.function("MaterialGetBlendMode", get_blend_mode) &&
           vm.function("MaterialV2", set_v2) && vm.function("MaterialV3", set_v3) &&
           vm.function("MaterialV4", set_v4);
}

} // namespace engine::plugins::script::bindings
