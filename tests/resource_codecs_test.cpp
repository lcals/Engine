#include "resource_fixture.hpp"

#include "runtime/engine_config.hpp"
#include "runtime/resource/resource_codec.hpp"
#include "runtime/resource/resource_system.hpp"

#include <array>
#include <cstddef>
#include <cstdio>
#include <fstream>
#include <memory>
#include <string>
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
    if (!resources.initialize({}))
    {
        return false;
    }
    test_support::ResourceFixture fixture;
    const auto* builtin_mesh = ResourceSystem::find_mesh("builtin/meshes/cube");
    const auto* builtin_texture = ResourceSystem::find_texture("builtin/textures/white-srgb");
    const auto* builtin_shader = ResourceSystem::find_shader("builtin/shaders/pbr");
    const auto* builtin_material = ResourceSystem::find_material("builtin/materials/default-pbr");
    if (!builtin_mesh || !builtin_texture || !builtin_shader || !builtin_material)
    {
        return false;
    }
    schema::MeshDataT mesh(*builtin_mesh);
    mesh.path = "project/cube.mesh";
    schema::TextureDataT texture(*builtin_texture);
    texture.path = "project/white.png";
    schema::ShaderDataT shader(*builtin_shader);
    shader.path = "project/lit.shader";
    schema::MaterialDataT material(*builtin_material);
    material.type_path = "project/lit.material";
    material.shader_path = shader.path;
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
    scene.info = std::make_unique<schema::SceneInfoT>();
    auto node = std::make_unique<schema::NodeDataT>();
    node->info = std::make_unique<schema::NodeInfoT>();
    node->info->id = 1;
    auto component = std::make_unique<schema::ComponentDataT>();
    schema::MeshRendererDataT renderer;
    renderer.mesh_guid = fixture.catalog.entries[0]->guid;
    renderer.material_guid = fixture.catalog.entries[3]->guid;
    component->payload.Set(renderer);
    node->components.push_back(std::move(component));
    scene.nodes.push_back(std::move(node));
    const std::array raw{std::byte{0}, std::byte{42}, std::byte{255}};
    if (!add_payload(fixture, "project/main.scene", schema::AssetKind::scene, pack(scene, "ESCN"),
                     {renderer.mesh_guid, renderer.material_guid}) ||
        !add_payload(fixture, "project/data", schema::AssetKind::source, raw) ||
        !add_payload(fixture, "project/main.ts", schema::AssetKind::script, {}) ||
        !fixture.publish(resources))
    {
        return false;
    }
    ResourcePtr mesh_resource;
    ResourcePtr texture_resource;
    ResourcePtr shader_resource;
    ResourcePtr material_resource;
    ResourcePtr scene_resource;
    ResourcePtr binary_resource;
    ResourcePtr script_resource;
    if (!expect(fixture.load(mesh.path, mesh_resource) && mesh_resource->mesh() &&
                    *mesh_resource->mesh() == mesh &&
                    fixture.load(texture.path, texture_resource) && texture_resource->texture() &&
                    *texture_resource->texture() == texture &&
                    fixture.load(shader.path, shader_resource) && shader_resource->shader() &&
                    *shader_resource->shader() == shader &&
                    fixture.load(material.type_path, material_resource) &&
                    material_resource->material() &&
                    material_resource->material()->shader_path == shader.path &&
                    fixture.load("project/main.scene", scene_resource) && scene_resource->scene() &&
                    *scene_resource->scene() == scene,
                "all structured kinds decode to their FlatBuffers models") ||
        !expect(fixture.load("project/data", binary_resource) &&
                    binary_resource->bytes().size() == 3 && binary_resource->bytes()[0] == raw[0] &&
                    binary_resource->bytes()[2] == raw[2] &&
                    fixture.load("project/main.ts", script_resource) &&
                    script_resource->bytes().empty(),
                "raw source retains embedded zeros and empty script is a valid byte payload") ||
        !expect(resources.validate_dependencies(*scene_resource,
                                                std::array{mesh_resource, material_resource}) &&
                    !resources.validate_dependencies(*scene_resource,
                                                     std::array{mesh_resource, texture_resource}),
                "type codec validates only explicitly supplied scene references") ||
        !expect(find_resource_codec(schema::AssetKind::script) ==
                        find_resource_codec(schema::AssetKind::source) &&
                    !find_resource_codec(static_cast<schema::AssetKind>(255)),
                "binary formats share one codec and unknown kinds have no decoder"))
    {
        return false;
    }

    // Catalog 和信封有效，但各类型实体错误；加载不能发布部分候选。
    mesh.path = "project/invalid.mesh";
    mesh.indices[0] = static_cast<std::uint32_t>(mesh.vertices.size());
    texture.path = "project/invalid.png";
    texture.width = 0;
    shader.path = "project/invalid.shader";
    shader.parameters.front()->uniform_name = "u_view";
    material.type_path = "project/invalid.material";
    material.parameters.push_back(
        std::make_unique<schema::MaterialParameterT>(*material.parameters.front()));
    scene.schema_version = 999;
    if (!fixture.add(mesh) || !fixture.add(texture) || !fixture.add(shader) ||
        !fixture.add(material) ||
        !add_payload(fixture, "project/invalid.scene", schema::AssetKind::scene,
                     pack(scene, "ESCN"), {renderer.mesh_guid, renderer.material_guid}) ||
        !add_payload(fixture, "project/wrong-kind.mesh", schema::AssetKind::mesh,
                     pack(texture, "ETEX")) ||
        !fixture.publish(resources))
    {
        return false;
    }
    for (const auto* path :
         {"project/invalid.mesh", "project/invalid.png", "project/invalid.shader",
          "project/invalid.material", "project/invalid.scene", "project/wrong-kind.mesh"})
    {
        auto output = mesh_resource;
        if (!expect(!fixture.load(path, output) && !output && !resources.last_error().empty(),
                    "invalid typed entity clears the output resource"))
        {
            return false;
        }
    }
    if (!expect(mesh_resource->mesh() && mesh_resource->mesh()->path == "project/cube.mesh" &&
                    mesh_resource->generation() < resources.generation() &&
                    scene_resource->scene()->schema_version == 1,
                "failed new-generation decoding never mutates old live models"))
    {
        return false;
    }

    mesh.path = "builtin/meshes/rejected";
    texture.path = "builtin/textures/rejected";
    shader.path = "builtin/shaders/rejected";
    material.type_path = "builtin/materials/rejected";
    material.shader_path = "builtin/shaders/pbr";
    return expect(!ResourceSystem::register_mesh(mesh) && !ResourceSystem::find_mesh(mesh.path) &&
                      !ResourceSystem::register_texture(texture) &&
                      !ResourceSystem::find_texture(texture.path) &&
                      !ResourceSystem::register_shader(shader) &&
                      !ResourceSystem::find_shader(shader.path) &&
                      !ResourceSystem::register_material(material) &&
                      !ResourceSystem::find_material(material.type_path),
                  "builtin registration reuses type validation and publishes nothing on failure");
}

} // namespace

int main()
{
    return codec_tests() ? 0 : 1;
}
