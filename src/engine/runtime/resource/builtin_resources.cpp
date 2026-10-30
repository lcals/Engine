#include "runtime/resource/builtin_resources.hpp"

#include "runtime/resource/resource_system.hpp"
#include "runtime/schema/asset_catalog_generated.h"
#include "runtime/schema/material_generated.h"
#include "runtime/schema/mesh_generated.h"
#include "runtime/schema/shader_generated.h"

#include "fs_cube.bin.h"
#include "fs_font.bin.h"
#include "vs_cube.bin.h"
#include "vs_font.bin.h"

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
        result.revision = "builtin-3";
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
        add("00000000-0000-4000-8000-000000000006", "builtin/textures/white-srgb",
            schema::AssetKind::texture, nullptr);
        add("00000000-0000-4000-8000-000000000007", "builtin/textures/flat-normal",
            schema::AssetKind::texture, nullptr);
        add("00000000-0000-4000-8000-000000000008", "builtin/textures/white-linear",
            schema::AssetKind::texture, nullptr);
        add("00000000-0000-4000-8000-000000000009", "builtin/shaders/font",
            schema::AssetKind::shader, nullptr);
        add("00000000-0000-4000-8000-00000000000a", "builtin/shaders/blinn-phong",
            schema::AssetKind::shader, nullptr);
        add("00000000-0000-4000-8000-00000000000b", "builtin/materials/default-blinn-phong",
            schema::AssetKind::material, "00000000-0000-4000-8000-00000000000a");
        for (auto& entry : result.entries)
        {
            if (entry->kind == schema::AssetKind::shader && entry->path != "builtin/shaders/font")
            {
                entry->dependencies = {"00000000-0000-4000-8000-000000000006",
                                       "00000000-0000-4000-8000-000000000007",
                                       "00000000-0000-4000-8000-000000000008"};
            }
        }
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

bool register_definition(const char* shader_path, const char* material_path, float lighting_mode)
{
    const bool unlit = lighting_mode == 1.0F;
    schema::ShaderDataT shader;
    shader.path = shader_path;
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
    auto lighting = scalar_parameter("lightingMode", "u_material", 2, lighting_mode, false);
    lighting->maximum = 2.0;
    shader.parameters.push_back(std::move(lighting));
    shader.parameters.push_back(scalar_parameter("alphaCutoff", "u_material", 3, 0.0F, true));
    shader.parameters.push_back(
        scalar_parameter("specularWorkflow", "u_specular", 3,
                         lighting_mode == 2.0F ? 1.0F : 0.0F, true));
    shader.parameters.push_back(
        scalar_parameter("smoothnessSource", "u_specular_options", 0, 0.0F, true));
    shader.parameters.push_back(
        scalar_parameter("specularHighlights", "u_specular_options", 1, 1.0F, true));
    auto specular = std::make_unique<schema::ShaderParameterT>();
    specular->name = "specularColor";
    specular->uniform_name = "u_specular";
    specular->bounded = true;
    specular->minimum = 0.0;
    specular->maximum = 1.0;
    schema::Vec3ValueT specular_value;
    specular_value.value = schema::Vec3(0.04, 0.04, 0.04);
    specular->default_value.Set(std::move(specular_value));
    shader.parameters.push_back(std::move(specular));
    auto emission = std::make_unique<schema::ShaderParameterT>();
    emission->name = "emissionColor";
    emission->uniform_name = "u_emission";
    emission->bounded = true;
    emission->minimum = 0.0;
    emission->maximum = 10000.0;
    schema::Vec3ValueT emission_value;
    emission_value.value = schema::Vec3(0.0, 0.0, 0.0);
    emission->default_value.Set(std::move(emission_value));
    shader.parameters.push_back(std::move(emission));
    shader.parameters.push_back(
        scalar_parameter("smoothness", "u_surface_parameters", 0, 0.5F, true));
    auto normal_scale = scalar_parameter("normalScale", "u_surface_parameters", 1, 1.0F, true);
    normal_scale->maximum = 8.0;
    shader.parameters.push_back(std::move(normal_scale));
    shader.parameters.push_back(
        scalar_parameter("fogInfluence", "u_surface_parameters", 2, 1.0F, true));
    auto uv_transform = std::make_unique<schema::ShaderParameterT>();
    uv_transform->name = "uvTransform";
    uv_transform->uniform_name = "u_uv_transform";
    schema::Vec4ValueT uv_value;
    uv_value.value = schema::Vec4(1.0, 1.0, 0.0, 0.0);
    uv_transform->default_value.Set(std::move(uv_value));
    shader.parameters.push_back(std::move(uv_transform));
    const auto add_sampler =
        [&shader](const char* name, const char* uniform, std::uint8_t stage, const char* texture)
    {
        auto sampler = std::make_unique<schema::ShaderSamplerT>();
        sampler->name = name;
        sampler->uniform_name = uniform;
        sampler->stage = stage;
        sampler->default_texture = texture;
        shader.samplers.push_back(std::move(sampler));
    };
    add_sampler("baseColorTexture", "s_base_color", 0, "builtin/textures/white-srgb");
    add_sampler("normalTexture", "s_normal", 1, "builtin/textures/flat-normal");
    add_sampler("ormTexture", "s_orm", 2, "builtin/textures/white-linear");
    add_sampler("specularTexture", "s_specular", 3, "builtin/textures/white-linear");
    add_sampler("emissionTexture", "s_emission", 4, "builtin/textures/white-srgb");
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
    material.type_path = material_path;
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
    schema::ShaderDataT font;
    font.path = "builtin/shaders/font";
    font.vertex_code.assign(std::begin(vs_font), std::end(vs_font));
    font.fragment_code.assign(std::begin(fs_font), std::end(fs_font));
    const auto* existing_font = ResourceSystem::find_shader(font.path);
    if (existing_font ? *existing_font != font : !ResourceSystem::register_shader(font))
    {
        return false;
    }
    const auto add_texture = [](const char* path, bool srgb, std::vector<std::uint8_t> pixels)
    {
        schema::TextureDataT texture;
        texture.path = path;
        texture.width = 1;
        texture.height = 1;
        texture.srgb = srgb;
        texture.encoding = schema::TextureEncoding::rgba8;
        texture.data = std::move(pixels);
        const auto* existing = ResourceSystem::find_texture(path);
        return existing ? *existing == texture : ResourceSystem::register_texture(texture);
    };
    if (!add_texture("builtin/textures/white-srgb", true, {255, 255, 255, 255}) ||
        !add_texture("builtin/textures/flat-normal", false, {128, 128, 255, 255}) ||
        !add_texture("builtin/textures/white-linear", false, {255, 255, 255, 255}))
    {
        return false;
    }
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
    for (std::size_t face = 0; face < 6; ++face)
    {
        cube.texcoords.insert(cube.texcoords.end(), {schema::Vec2(0, 1), schema::Vec2(1, 1),
                                                     schema::Vec2(1, 0), schema::Vec2(0, 0)});
    }
    const auto* existing = ResourceSystem::find_mesh(cube.path);
    return (existing ? *existing == cube : ResourceSystem::register_mesh(cube)) &&
           register_definition("builtin/shaders/pbr", "builtin/materials/default-pbr", 0.0F) &&
           register_definition("builtin/shaders/unlit", "builtin/materials/default-unlit", 1.0F) &&
           register_definition("builtin/shaders/blinn-phong",
                               "builtin/materials/default-blinn-phong", 2.0F);
}

} // namespace engine
