#include "runtime/resource/builtin_resources.hpp"

#include "runtime/resource/resource_system.hpp"
#include "runtime/schema/asset_catalog_generated.h"
#include "runtime/schema/material_generated.h"
#include "runtime/schema/mesh_generated.h"
#include "runtime/schema/shader_generated.h"

#include "fs_cube.bin.h"
#include "fs_font.bin.h"
#include "fs_surface.bin.h"
#include "fs_water.bin.h"
#include "vs_cube.bin.h"
#include "vs_font.bin.h"

#include <flatbuffers/flatbuffers.h>

#include <algorithm>
#include <iterator>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace engine
{

const schema::AssetCatalogDataT& builtin_resource_catalog()
{
    static const schema::AssetCatalogDataT catalog = []
    {
        schema::AssetCatalogDataT result;
        result.schema_version = 2;
        result.revision = "builtin-4";
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
        add("00000000-0000-4000-8000-000000000001", "builtin://shaders/pbr",
            schema::AssetKind::shader, nullptr);
        add("00000000-0000-4000-8000-000000000002", "builtin://shaders/unlit",
            schema::AssetKind::shader, nullptr);
        add("00000000-0000-4000-8000-000000000003", "builtin://materials/default-pbr",
            schema::AssetKind::material, "00000000-0000-4000-8000-000000000001");
        add("00000000-0000-4000-8000-000000000004", "builtin://materials/default-unlit",
            schema::AssetKind::material, "00000000-0000-4000-8000-000000000002");
        add("00000000-0000-4000-8000-000000000005", "builtin://meshes/cube",
            schema::AssetKind::mesh, nullptr);
        add("00000000-0000-4000-8000-000000000006", "builtin://textures/white-srgb",
            schema::AssetKind::texture, nullptr);
        add("00000000-0000-4000-8000-000000000007", "builtin://textures/flat-normal",
            schema::AssetKind::texture, nullptr);
        add("00000000-0000-4000-8000-000000000008", "builtin://textures/white-linear",
            schema::AssetKind::texture, nullptr);
        add("00000000-0000-4000-8000-000000000009", "builtin://shaders/font",
            schema::AssetKind::shader, nullptr);
        add("00000000-0000-4000-8000-00000000000a", "builtin://shaders/blinn-phong",
            schema::AssetKind::shader, nullptr);
        add("00000000-0000-4000-8000-00000000000b", "builtin://materials/default-blinn-phong",
            schema::AssetKind::material, "00000000-0000-4000-8000-00000000000a");
        add("00000000-0000-4000-8000-00000000000c", "builtin://shaders/water",
            schema::AssetKind::shader, nullptr);
        add("00000000-0000-4000-8000-00000000000d", "builtin://materials/default-water",
            schema::AssetKind::material, "00000000-0000-4000-8000-00000000000c");
        for (auto& entry : result.entries)
        {
            if (entry->kind == schema::AssetKind::shader && entry->path != "builtin://shaders/font")
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

schema::ShaderDataT surface_shader(const char* shader_path, float lighting_mode)
{
    const bool unlit = lighting_mode == 1.0F;
    schema::ShaderDataT shader;
    shader.path = shader_path;
    shader.vertex_code.assign(std::begin(vs_cube), std::end(vs_cube));
    shader.fragment_code.assign(std::begin(fs_cube), std::end(fs_cube));
    shader.surface_fragment_code.assign(std::begin(fs_surface), std::end(fs_surface));
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
    shader.parameters.push_back(scalar_parameter("specularWorkflow", "u_specular", 3,
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
    add_sampler("baseColorTexture", "s_base_color", 0, "builtin://textures/white-srgb");
    add_sampler("normalTexture", "s_normal", 1, "builtin://textures/flat-normal");
    add_sampler("ormTexture", "s_orm", 2, "builtin://textures/white-linear");
    add_sampler("specularTexture", "s_specular", 3, "builtin://textures/white-linear");
    add_sampler("emissionTexture", "s_emission", 4, "builtin://textures/white-srgb");
    return shader;
}

schema::ShaderDataT water_shader()
{
    auto shader = surface_shader("builtin://shaders/water", 0.0F);
    shader.fragment_code.assign(std::begin(fs_water), std::end(fs_water));
    shader.surface_fragment_code.clear();
    for (auto& parameter : shader.parameters)
    {
        if (parameter->name == "baseColor")
        {
            parameter->default_value.AsVec4Value()->value = schema::Vec4(0.025, 0.12, 0.16, 1.0);
        }
        else if (parameter->name == "roughness")
        {
            parameter->default_value.AsFloatValue()->value = 0.08;
        }
        else if (parameter->name == "specularColor")
        {
            parameter->default_value.AsVec3Value()->value = schema::Vec3(0.0204, 0.0204, 0.0204);
        }
    }
    auto flow = std::make_unique<schema::ShaderParameterT>();
    flow->name = "waterFlow";
    flow->uniform_name = "u_water_flow";
    schema::Vec2ValueT flow_value;
    flow_value.value = schema::Vec2(0.025, 0.015);
    flow->default_value.Set(std::move(flow_value));
    shader.parameters.push_back(std::move(flow));
    auto ior = scalar_parameter("waterIor", "u_water_flow", 2, 1.333F, true);
    ior->minimum = 1.0;
    ior->maximum = 2.5;
    shader.parameters.push_back(std::move(ior));
    shader.parameters.push_back(
        scalar_parameter("refractionStrength", "u_water_parameters", 0, 1.0F, true));
    auto absorption = scalar_parameter("absorptionDistance", "u_water_parameters", 1, 5.0F, true);
    absorption->minimum = 0.001;
    absorption->maximum = 10000.0;
    shader.parameters.push_back(std::move(absorption));
    auto shore = scalar_parameter("shoreFadeDistance", "u_water_parameters", 2, 0.15F, true);
    shore->minimum = 0.001;
    shore->maximum = 100.0;
    shader.parameters.push_back(std::move(shore));
    shader.parameters.push_back(
        scalar_parameter("reflectionStrength", "u_water_parameters", 3, 1.0F, true));
    auto absorption_color = std::make_unique<schema::ShaderParameterT>();
    absorption_color->name = "absorptionColor";
    absorption_color->uniform_name = "u_water_absorption";
    absorption_color->bounded = true;
    absorption_color->minimum = 0.0;
    absorption_color->maximum = 1.0;
    schema::Vec3ValueT absorption_value;
    absorption_value.value = schema::Vec3(0.45, 0.78, 0.85);
    absorption_color->default_value.Set(std::move(absorption_value));
    shader.parameters.push_back(std::move(absorption_color));
    const auto add_frame_sampler = [&shader](const char* name, const char* uniform,
                                             std::uint8_t stage, schema::ShaderTextureSource source)
    {
        auto sampler = std::make_unique<schema::ShaderSamplerT>();
        sampler->name = name;
        sampler->uniform_name = uniform;
        sampler->stage = stage;
        sampler->source = source;
        sampler->exposed = false;
        shader.samplers.push_back(std::move(sampler));
    };
    add_frame_sampler("planarReflection", "s_planar_reflection", 8,
                      schema::ShaderTextureSource::planar_reflection);
    add_frame_sampler("sceneColor", "s_scene_color", 9, schema::ShaderTextureSource::scene_color);
    add_frame_sampler("sceneDepth", "s_scene_depth", 10, schema::ShaderTextureSource::scene_depth);
    return shader;
}

struct BuiltinDefinitions
{
    std::vector<schema::ShaderDataT> shaders;
    std::vector<schema::MaterialDataT> materials;
    std::vector<schema::MeshDataT> meshes;
    std::vector<schema::TextureDataT> textures;
};

const BuiltinDefinitions& builtin_definitions()
{
    static const BuiltinDefinitions definitions = []
    {
        BuiltinDefinitions result;

        schema::ShaderDataT font;
        font.path = "builtin://shaders/font";
        font.vertex_code.assign(std::begin(vs_font), std::end(vs_font));
        font.fragment_code.assign(std::begin(fs_font), std::end(fs_font));
        result.shaders.push_back(std::move(font));
        const auto add_texture =
            [&result](const char* path, bool srgb, std::vector<std::uint8_t> pixels)
        {
            schema::TextureDataT texture;
            texture.path = path;
            texture.width = 1;
            texture.height = 1;
            texture.srgb = srgb;
            texture.encoding = schema::TextureEncoding::rgba8;
            texture.data = std::move(pixels);
            result.textures.push_back(std::move(texture));
        };
        add_texture("builtin://textures/white-srgb", true, {255, 255, 255, 255});
        add_texture("builtin://textures/flat-normal", false, {128, 128, 255, 255});
        add_texture("builtin://textures/white-linear", false, {255, 255, 255, 255});
        schema::MeshDataT cube;
        cube.path = "builtin://meshes/cube";
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
        result.meshes.push_back(std::move(cube));
        result.shaders.push_back(surface_shader("builtin://shaders/pbr", 0.0F));
        result.shaders.push_back(surface_shader("builtin://shaders/unlit", 1.0F));
        result.shaders.push_back(surface_shader("builtin://shaders/blinn-phong", 2.0F));
        result.shaders.push_back(water_shader());
        for (const auto& entry : builtin_resource_catalog().entries)
        {
            if (entry->kind != schema::AssetKind::material)
            {
                continue;
            }
            schema::MaterialDataT material;
            material.type_path = entry->path;
            material.shader_guid = entry->dependencies.front();
            material.shader_path = builtin_resource_info(material.shader_guid)->path;
            if (material.shader_path == "builtin://shaders/water")
            {
                material.blend_mode = schema::BlendMode::alpha_blend;
            }
            result.materials.push_back(std::move(material));
        }
        return result;
    }();
    return definitions;
}

} // namespace

bool builtin_resource_payload(std::string_view guid, ByteBuffer& output)
{
    output.clear();
    const auto* entry = builtin_resource_info(guid);
    if (entry == nullptr)
    {
        return false;
    }
    flatbuffers::FlatBufferBuilder builder;
    const auto serialize =
        [&]<typename Data>(const std::vector<Data>& definitions, const char* identifier)
    {
        for (const auto& data : definitions)
        {
            const std::string* path = nullptr;
            if constexpr (std::is_same_v<Data, schema::MaterialDataT>)
            {
                path = &data.type_path;
            }
            else
            {
                path = &data.path;
            }
            if (*path == entry->path)
            {
                builder.Finish(Data::TableType::Pack(builder, &data), identifier);
                return true;
            }
        }
        return false;
    };
    const auto& definitions = builtin_definitions();
    if (!serialize(definitions.shaders, "ESHD") && !serialize(definitions.materials, "EMAT") &&
        !serialize(definitions.meshes, "EMSH") && !serialize(definitions.textures, "ETEX"))
    {
        return false;
    }
    const auto* begin = reinterpret_cast<const std::byte*>(builder.GetBufferPointer());
    output.assign(begin, begin + builder.GetSize());
    return true;
}

bool register_builtin_render_resources()
{
    const auto register_all = [](const auto& definitions, auto add)
    {
        for (const auto& data : definitions)
        {
            if (!add(data))
            {
                return false;
            }
        }
        return true;
    };
    const auto& definitions = builtin_definitions();
    return register_all(definitions.textures, ResourceSystem::register_texture) &&
           register_all(definitions.shaders, ResourceSystem::register_shader) &&
           register_all(definitions.materials, ResourceSystem::register_material) &&
           register_all(definitions.meshes, ResourceSystem::register_mesh);
}

} // namespace engine
