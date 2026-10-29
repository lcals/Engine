#include "runtime/resource/builtin_resources.hpp"

#include "runtime/resource/resource_system.hpp"
#include "runtime/schema/scene_generated.h"

#include "fs_cube.bin.h"
#include "vs_cube.bin.h"

#include <iterator>
#include <memory>
#include <string>
#include <utility>

namespace engine
{
namespace
{

std::unique_ptr<schema::ShaderParameterT> scalar_parameter(const char* name, const char* uniform,
                                                           std::uint8_t offset, float value,
                                                           bool exposed)
{
    auto parameter = std::make_unique<schema::ShaderParameterT>();
    parameter->name = name;
    parameter->uniform_name = uniform;
    parameter->uniform_offset = offset;
    parameter->exposed = exposed;
    parameter->bounded = true;
    parameter->minimum = 0.0;
    parameter->maximum = 1.0;
    schema::FloatValueT initial;
    initial.value = value;
    parameter->default_value.Set(std::move(initial));
    return parameter;
}

bool register_definition(bool unlit)
{
    schema::ShaderDataT shader;
    shader.path = unlit ? "builtin/shaders/unlit" : "builtin/shaders/pbr";
    shader.vertex_code.assign(std::begin(vs_cube), std::end(vs_cube));
    shader.fragment_code.assign(std::begin(fs_cube), std::end(fs_cube));
    auto color = std::make_unique<schema::ShaderParameterT>();
    color->name = "baseColor";
    color->uniform_name = "u_base_color";
    color->bounded = true;
    color->minimum = 0.0;
    color->maximum = 1.0;
    schema::Vec4ValueT value;
    value.value = schema::Vec4(0.7, 0.3, 0.15, 1.0);
    color->default_value.Set(std::move(value));
    shader.parameters.push_back(std::move(color));
    if (!unlit)
    {
        shader.parameters.push_back(scalar_parameter("metallic", "u_material", 0, 0.0F, true));
        shader.parameters.push_back(scalar_parameter("roughness", "u_material", 1, 0.4F, true));
    }
    shader.parameters.push_back(
        scalar_parameter("lightingMode", "u_material", 2, unlit ? 1.0F : 0.0F, false));
    if (const auto* existing = ResourceSystem::find_shader(shader.path); existing != nullptr)
    {
        if (*existing != shader)
        {
            return false;
        }
    }
    else if (!ResourceSystem::register_shader(shader))
    {
        return false;
    }
    schema::MaterialDataT material;
    material.type_path =
        unlit ? "builtin/materials/default-unlit" : "builtin/materials/default-pbr";
    material.shader_path = shader.path;
    if (const auto* existing = ResourceSystem::find_material(material.type_path);
        existing != nullptr)
    {
        return *existing == material;
    }
    return ResourceSystem::register_material(material);
}
} // namespace

bool register_builtin_render_resources()
{
    return register_definition(false) && register_definition(true);
}

} // namespace engine
