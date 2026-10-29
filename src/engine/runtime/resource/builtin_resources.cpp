#include "runtime/resource/builtin_resources.hpp"

#include "runtime/resource/resource_system.hpp"
#include "runtime/schema/material_generated.h"
#include "runtime/schema/mesh_generated.h"
#include "runtime/schema/shader_generated.h"

#include "fs_cube.bin.h"
#include "vs_cube.bin.h"

#include <iterator>
#include <memory>
#include <string>
#include <utility>

namespace engine
{

const schema::AssetCatalogDataT& builtin_resource_catalog()
{
    static const schema::AssetCatalogDataT catalog = []
    {
        schema::AssetCatalogDataT result;
        result.schema_version = 2;
        result.revision = "builtin-1";
        const auto add = [&result](const char* guid, const char* path, schema::AssetKind kind,
                                   const char* dependency)
        {
            auto entry = std::make_unique<schema::AssetEntryT>();
            entry->guid = guid;
            entry->path = path;
            entry->kind = kind;
            if (dependency != nullptr)
            {
                entry->dependencies.emplace_back(dependency);
            }
            result.entries.push_back(std::move(entry));
        };
        add("00000000-0000-4000-8000-000000000001", "builtin/shaders/pbr",
            schema::AssetKind::shader, nullptr);
        add("00000000-0000-4000-8000-000000000002", "builtin/shaders/unlit",
            schema::AssetKind::shader, nullptr);
        add("00000000-0000-4000-8000-000000000003", "builtin/materials/default-pbr",
            schema::AssetKind::material, "00000000-0000-4000-8000-000000000001");
        add("00000000-0000-4000-8000-000000000004", "builtin/materials/default-unlit",
            schema::AssetKind::material, "00000000-0000-4000-8000-000000000002");
        add("00000000-0000-4000-8000-000000000005", "builtin/meshes/cube", schema::AssetKind::mesh,
            nullptr);
        return result;
    }();
    return catalog;
}

const schema::AssetEntryT* builtin_resource_info(std::string_view guid)
{
    for (const auto& entry : builtin_resource_catalog().entries)
    {
        if (entry->guid == guid)
        {
            return entry.get();
        }
    }
    return nullptr;
}

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
    schema::MeshDataT cube;
    cube.path = "builtin/meshes/cube";
    cube.vertices = {{-1, -1, 1, 0, 0, 1},   {1, -1, 1, 0, 0, 1},   {1, 1, 1, 0, 0, 1},
                     {-1, 1, 1, 0, 0, 1},    {1, -1, 1, 1, 0, 0},   {1, -1, -1, 1, 0, 0},
                     {1, 1, -1, 1, 0, 0},    {1, 1, 1, 1, 0, 0},    {1, -1, -1, 0, 0, -1},
                     {-1, -1, -1, 0, 0, -1}, {-1, 1, -1, 0, 0, -1}, {1, 1, -1, 0, 0, -1},
                     {-1, -1, -1, -1, 0, 0}, {-1, -1, 1, -1, 0, 0}, {-1, 1, 1, -1, 0, 0},
                     {-1, 1, -1, -1, 0, 0},  {-1, 1, 1, 0, 1, 0},   {1, 1, 1, 0, 1, 0},
                     {1, 1, -1, 0, 1, 0},    {-1, 1, -1, 0, 1, 0},  {-1, -1, -1, 0, -1, 0},
                     {1, -1, -1, 0, -1, 0},  {1, -1, 1, 0, -1, 0},  {-1, -1, 1, 0, -1, 0}};
    cube.indices = {0,  1,  2,  0,  2,  3,  4,  5,  6,  4,  6,  7,  8,  9,  10, 8,  10, 11,
                    12, 13, 14, 12, 14, 15, 16, 17, 18, 16, 18, 19, 20, 21, 22, 20, 22, 23};
    const auto* existing = ResourceSystem::find_mesh(cube.path);
    return (existing ? *existing == cube : ResourceSystem::register_mesh(cube)) &&
           register_definition(false) && register_definition(true);
}

} // namespace engine
