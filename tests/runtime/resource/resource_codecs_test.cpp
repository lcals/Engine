#include "resource_fixture.hpp"
#include "scene_snapshot.hpp"

#include "runtime/engine_config.hpp"
#include "runtime/resource/codecs/material_resource_codec.hpp"
#include "runtime/resource/codecs/mesh_resource_codec.hpp"
#include "runtime/resource/codecs/shader_resource_codec.hpp"
#include "runtime/resource/codecs/texture_resource_codec.hpp"
#include "runtime/resource/resource_codec.hpp"
#include "runtime/resource/resource_system.hpp"
#include "runtime/schema/movie_generated.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace
{

using namespace engine;

bool expect(bool value, const char* message)
{
    if (!value)
    {
        std::fprintf(stderr, "resource codecs: %s\n", message);
    }
    return value;
}

template <typename T> ByteBuffer pack(const T& data, const char* identifier)
{
    flatbuffers::FlatBufferBuilder builder;
    builder.Finish(T::TableType::Pack(builder, &data), identifier);
    const auto* bytes = reinterpret_cast<const std::byte*>(builder.GetBufferPointer());
    return {bytes, bytes + builder.GetSize()};
}

bool add_payload(test_support::ResourceFixture& fixture, std::string path, schema::AssetKind kind,
                 ByteView payload, std::vector<Guid> dependencies = {})
{
    auto entry = std::make_unique<schema::AssetEntryT>();
    std::array<char, 37> guid{};
    std::snprintf(guid.data(), guid.size(), "30000000-0000-4000-8000-%012zu",
                  fixture.catalog.entries.size() + 1);
    entry->guid = guid.data();
    entry->path = std::move(path);
    entry->kind = kind;
    if (kind == schema::AssetKind::script)
    {
        entry->module_path = "main.js";
    }
    entry->dependencies = std::move(dependencies);
    if (kind == schema::AssetKind::script)
    {
        entry->module_path = "scripts/main.js";
    }
    ByteBuffer entity;
    if (!ResourceSystem::encode_resource(entry->guid, kind, payload, entity, entry->header_size))
    {
        return false;
    }
    std::uint64_t hash = 14695981039346656037ULL;
    for (const auto value : entity)
    {
        hash = (hash ^ std::to_integer<std::uint8_t>(value)) * 1099511628211ULL;
    }
    std::array<char, 17> digest{};
    std::snprintf(digest.data(), digest.size(), "%016llx", static_cast<unsigned long long>(hash));
    entry->content_hash = digest.data();
    entry->content_size = entity.size();
    entry->location = std::make_unique<schema::FileLocationT>();
    entry->location->mount = "content";
    entry->location->path = "gen/" + entry->guid;
    entry->location->length = entity.size();
    std::ofstream stream(fixture.root / entry->location->path, std::ios::binary);
    stream.write(reinterpret_cast<const char*>(entity.data()),
                 static_cast<std::streamsize>(entity.size()));
    stream.close();
    if (!stream)
    {
        return false;
    }
    fixture.catalog.entries.push_back(std::move(entry));
    return true;
}

bool codec_tests()
{
    ResourceSystem resources;
    if (!test_support::register_resource_codecs(resources) || !resources.initialize({}))
    {
        return false;
    }
    test_support::ResourceFixture fixture;
    const auto builtin_mesh = test_support::builtin<MeshResourceCodec>("builtin://meshes/cube");
    const auto builtin_texture =
        test_support::builtin<TextureResourceCodec>("builtin://textures/white-srgb");
    const auto builtin_shader = test_support::builtin<ShaderResourceCodec>("builtin://shaders/pbr");
    const auto builtin_material =
        test_support::builtin<MaterialResourceCodec>("builtin://materials/default-pbr");
    if (!builtin_mesh || !builtin_texture || !builtin_shader || !builtin_material)
    {
        return false;
    }
    schema::MeshDataT mesh(*builtin_mesh);
    mesh.path = "cube.mesh";
    schema::TextureDataT texture(*builtin_texture);
    texture.path = "white.png";
    schema::ShaderDataT shader(*builtin_shader);
    shader.path = "lit.shader";
    schema::MaterialDataT material(*builtin_material);
    material.type_path = "lit.material";
    material.shader_path = shader.path;
    auto editor_mesh = mesh;
    auto editor_texture = texture;
    auto editor_shader = shader;
    auto editor_material = material;
    editor_mesh.path = "editor://preview.mesh";
    editor_texture.path = "editor://preview.png";
    editor_shader.path = "editor://preview.shader";
    editor_material.type_path = "editor://preview.material";
    if (!expect(!MeshResourceCodec::validate(editor_mesh) &&
                    !TextureResourceCodec::validate(editor_texture) &&
                    !ShaderResourceCodec::validate(editor_shader) &&
                    !MaterialResourceCodec::validate(editor_material, &shader),
                "runtime codecs reject the reserved editor namespace"))
    {
        return false;
    }
    auto roughness = std::make_unique<schema::MaterialParameterT>();
    roughness->name = "roughness";
    schema::FloatValueT roughness_value;
    roughness_value.value = 0.4;
    roughness->value.Set(roughness_value);
    material.parameters.push_back(std::move(roughness));
    if (!fixture.add(mesh) || !fixture.add(texture) || !fixture.add(shader) ||
        !fixture.add(material))
    {
        return false;
    }
    schema::SceneDataT scene;
    scene.schema_version = 3;
    scene.info = std::make_unique<schema::SceneInfoT>();
    auto node = std::make_unique<schema::NodeDataT>();
    node->info = test_support::node_info();
    node->info->properties->id = 1;
    auto component = test_support::component_data();
    schema::MeshRendererDataT renderer;
    renderer.mesh_guid = fixture.catalog.entries[0]->guid;
    renderer.material_guid = fixture.catalog.entries[3]->guid;
    component->payload.Set(renderer);
    node->components.push_back(std::move(component));
    scene.nodes.push_back(std::move(node));
    const std::array raw{std::byte{0}, std::byte{42}, std::byte{255}};
    constexpr std::string_view json = "{\n  \"weapon\": \"carbine\", \"damage\": 24\n}\n";
    if (!add_payload(fixture, "main.scene", schema::AssetKind::scene, pack(scene, "ESCN"),
                     {renderer.mesh_guid, renderer.material_guid}) ||
        !add_payload(fixture, "data", schema::AssetKind::source, raw) ||
        !add_payload(fixture, "main.ts", schema::AssetKind::script, {}) ||
        !add_payload(fixture, "config/weapon.json", schema::AssetKind::json,
                     std::as_bytes(std::span(json.data(), json.size()))) ||
        !fixture.publish(resources))
    {
        return false;
    }
    ResourceError dependency_error{};
    std::string message;
    std::unique_ptr<Resource> mesh_resource;
    std::unique_ptr<Resource> texture_resource;
    std::unique_ptr<Resource> shader_resource;
    std::unique_ptr<Resource> material_resource;
    std::unique_ptr<Resource> scene_resource;
    std::unique_ptr<Resource> binary_resource;
    std::unique_ptr<Resource> script_resource;
    std::unique_ptr<Resource> json_resource;
    if (!expect(fixture.read(resources, mesh.path, mesh_resource) &&
                    resource_detail::data<MeshResource>(mesh_resource.get()) &&
                    *resource_detail::data<MeshResource>(mesh_resource.get()) == mesh &&
                    fixture.read(resources, texture.path, texture_resource) &&
                    resource_detail::data<TextureResource>(texture_resource.get()) &&
                    *resource_detail::data<TextureResource>(texture_resource.get()) == texture &&
                    fixture.read(resources, shader.path, shader_resource) &&
                    resource_detail::data<ShaderResource>(shader_resource.get()) &&
                    *resource_detail::data<ShaderResource>(shader_resource.get()) == shader &&
                    fixture.read(resources, material.type_path, material_resource) &&
                    resource_detail::data<MaterialResource>(material_resource.get()) &&
                    resource_detail::data<MaterialResource>(material_resource.get())->shader_path ==
                        shader.path &&
                    fixture.read(resources, "main.scene", scene_resource) &&
                    resource_detail::data<SceneResource>(scene_resource.get()) &&
                    *resource_detail::data<SceneResource>(scene_resource.get()) == scene,
                "all structured kinds decode to their FlatBuffers models") ||
        !expect(fixture.read(resources, "data", binary_resource) &&
                    (*resource_detail::data<SourceResource>(binary_resource.get())).size() == 3 &&
                    (*resource_detail::data<SourceResource>(binary_resource.get()))[0] == raw[0] &&
                    (*resource_detail::data<SourceResource>(binary_resource.get()))[2] == raw[2] &&
                    fixture.read(resources, "main.ts", script_resource) &&
                    (*resource_detail::data<ScriptResource>(script_resource.get())).empty(),
                "raw source retains embedded zeros and empty script is a valid byte payload") ||
        !expect(fixture.read(resources, "config/weapon.json", json_resource) &&
                    json_resource->info().kind == schema::AssetKind::json &&
                    json_resource->info().dependencies.empty() &&
                    (*resource_detail::data<JsonResource>(json_resource.get())).size() ==
                        json.size() &&
                    std::string_view(
                        reinterpret_cast<const char*>(
                            (*resource_detail::data<JsonResource>(json_resource.get())).data()),
                        (*resource_detail::data<JsonResource>(json_resource.get())).size()) == json,
                "JSON is a distinct resource kind retaining its original text bytes") ||
        !expect(resources.validate_dependencies(
                    *scene_resource, std::array{mesh_resource.get(), material_resource.get()},
                    dependency_error, message) &&
                    !resources.validate_dependencies(
                        *scene_resource, std::array{mesh_resource.get(), texture_resource.get()},
                        dependency_error, message),
                "type codec validates only explicitly supplied scene references") ||
        !expect(!resources.add<SourceResourceCodec>() && !resources.add<JsonResourceCodec>(),
                "codec registration is explicit and frozen after initialization"))
    {
        return false;
    }

    for (const auto* uniform :
         {"u_shadow_matrices", "u_shadow_rects", "u_shadow_splits", "u_shadow_params",
          "u_ambient_occlusion", "s_ambient_occlusion", "u_assao_params", "u_assao_rect",
          "u_hdr_params", "u_hdr_texel", "s_hdr_scene", "s_hdr_bloom"})
    {
        schema::ShaderDataT reserved(*builtin_shader);
        reserved.path = "builtin://shaders/reserved-renderer";
        reserved.parameters.front()->uniform_name = uniform;
        if (!expect(!ShaderResourceCodec::validate(reserved),
                    "material parameters cannot claim renderer uniforms"))
        {
            return false;
        }
        reserved.parameters.front()->uniform_name =
            builtin_shader->parameters.front()->uniform_name;
        reserved.samplers.front()->uniform_name = uniform;
        if (!expect(!ShaderResourceCodec::validate(reserved),
                    "material samplers cannot claim renderer uniforms"))
        {
            return false;
        }
    }

    schema::ShaderDataT sampler_boundary(*builtin_shader);
    sampler_boundary.samplers.resize(1);
    sampler_boundary.samplers.front()->stage = 4;
    if (!expect(ShaderResourceCodec::validate(sampler_boundary),
                "material sampler slot 4 remains available"))
    {
        return false;
    }
    for (std::uint8_t stage = 5; stage < 16; ++stage)
    {
        sampler_boundary.samplers.front()->stage = stage;
        if (!expect(!ShaderResourceCodec::validate(sampler_boundary),
                    "material samplers cannot occupy engine frame resource slots"))
        {
            return false;
        }
    }

    auto& dynamic_sampler = *sampler_boundary.samplers.front();
    dynamic_sampler.source = schema::ShaderTextureSource::scene_color;
    dynamic_sampler.stage = 9;
    dynamic_sampler.default_texture.clear();
    dynamic_sampler.exposed = false;
    if (!expect(ShaderResourceCodec::validate(sampler_boundary),
                "scene color uses its fixed slot without an asset dependency"))
    {
        return false;
    }
    dynamic_sampler.stage = 8;
    if (!expect(!ShaderResourceCodec::validate(sampler_boundary),
                "scene color cannot occupy the planar reflection slot"))
    {
        return false;
    }
    dynamic_sampler.source = schema::ShaderTextureSource::planar_reflection;
    if (!expect(ShaderResourceCodec::validate(sampler_boundary), "planar reflection uses slot 8"))
    {
        return false;
    }
    dynamic_sampler.exposed = true;
    if (!expect(!ShaderResourceCodec::validate(sampler_boundary),
                "dynamic scene inputs cannot be overridden as resource textures"))
    {
        return false;
    }
    dynamic_sampler.exposed = false;
    dynamic_sampler.default_texture = "builtin://textures/white";
    if (!expect(!ShaderResourceCodec::validate(sampler_boundary),
                "dynamic scene inputs cannot declare a fallback asset dependency"))
    {
        return false;
    }

    // Catalog 和信封有效，但各类型实体错误；加载不能发布部分候选。
    mesh.path = "invalid.mesh";
    mesh.indices[0] = static_cast<std::uint32_t>(mesh.vertices.size());
    texture.path = "invalid.png";
    texture.width = 0;
    shader.path = "invalid.shader";
    shader.parameters.front()->uniform_name = "u_view";
    material.type_path = "invalid.material";
    material.parameters.push_back(
        std::make_unique<schema::MaterialParameterT>(*material.parameters.front()));
    scene.schema_version = 1;
    if (!fixture.add(mesh) || !fixture.add(texture) || !fixture.add(shader) ||
        !fixture.add(material) ||
        !add_payload(fixture, "invalid.scene", schema::AssetKind::scene, pack(scene, "ESCN"),
                     {renderer.mesh_guid, renderer.material_guid}) ||
        !add_payload(fixture, "wrong-kind.mesh", schema::AssetKind::mesh, pack(texture, "ETEX")) ||
        !fixture.publish(resources))
    {
        return false;
    }
    for (const auto* path : {"invalid.mesh", "invalid.png", "invalid.shader", "invalid.material",
                             "invalid.scene", "wrong-kind.mesh"})
    {
        std::unique_ptr<Resource> output;
        if (!expect(!fixture.read(resources, path, output) && !output &&
                        !resources.last_error().empty(),
                    "invalid typed entity clears the output resource"))
        {
            return false;
        }
    }
    if (!expect(resource_detail::data<MeshResource>(mesh_resource.get()) &&
                    resource_detail::data<MeshResource>(mesh_resource.get())->path == "cube.mesh" &&
                    mesh_resource->generation() < resources.generation() &&
                    resource_detail::data<SceneResource>(scene_resource.get())->schema_version == 3,
                "failed new-generation decoding never mutates old live models"))
    {
        return false;
    }

    schema::AssetEntryT memory_info;
    memory_info.guid = "40000000-0000-4000-8000-000000000001";
    memory_info.path = "builtin://meshes/rejected";
    memory_info.kind = schema::AssetKind::mesh;
    mesh.path = memory_info.path;
    MeshResource invalid_memory(memory_info, mesh);
    return expect(!resources.validate_memory(invalid_memory),
                  "invalid native memory payload is rejected before graph admission");
}

bool movie_codec_tests()
{
    ResourceSystem resources;
    if (!test_support::register_resource_codecs(resources) || !resources.initialize({}))
    {
        return false;
    }
    test_support::ResourceFixture fixture;
    schema::MovieDataT movie;
    movie.video = std::make_unique<schema::MovieVideoDataT>();
    auto& video = *movie.video;
    video.width = 2;
    video.height = 2;
    video.frame_rate_num = 30;
    video.frame_rate_den = 1;
    video.time_base_num = 1;
    video.time_base_den = 30;
    video.duration_ticks = 1;
    video.frame_count = 1;
    video.color = std::make_unique<schema::MovieColorInfo>();
    // A bounded IVF envelope; VP9 bitstream validity is checked by the decoder, not this codec.
    video.payload = {'D', 'K', 'I', 'F', 0, 0, 32, 0, 'V', 'P', '9', '0', 2, 0, 2,
                     0,   30,  0,   0,   0, 1, 0,  0, 0,   1,   0,   0,   0, 0, 0,
                     0,   0,   1,   0,   0, 0, 0,  0, 0,   0,   0,   0,   0, 0, 42};
    if (!add_payload(fixture, "valid.mov", schema::AssetKind::movie, pack(movie, "EMOV")))
    {
        return false;
    }
    video.payload[32] = 2; // Claims two bytes for a one-byte frame.
    if (!add_payload(fixture, "truncated.mov", schema::AssetKind::movie, pack(movie, "EMOV")))
    {
        return false;
    }
    video.payload[32] = 1;
    video.payload[36] = 1; // The first timestamp must be zero.
    if (!add_payload(fixture, "bad-time.mov", schema::AssetKind::movie, pack(movie, "EMOV")) ||
        !fixture.publish(resources))
    {
        return false;
    }
    ResourceError dependency_error{};
    std::string message;
    std::unique_ptr<Resource> value;
    return expect(fixture.read(resources, "valid.mov", value) && value &&
                      resource_detail::data<MovieResource>(value.get()) != nullptr,
                  "valid movie envelope loads") &&
           expect(resources.validate_dependencies(*value, {}, dependency_error, message),
                  "silent movie has no dependencies") &&
           expect((value.reset(), !fixture.read(resources, "truncated.mov", value)) && !value,
                  "truncated IVF frame is rejected") &&
           expect(!fixture.read(resources, "bad-time.mov", value) && !value,
                  "invalid IVF timestamp is rejected");
}

} // namespace

int main()
{
    return codec_tests() && movie_codec_tests() ? 0 : 1;
}
