#include "runtime/engine_config.hpp"
#include "runtime/resource/resource_system.hpp"
#include "runtime/scene/camera.hpp"
#include "runtime/scene/mesh_renderer.hpp"
#include "runtime/scene/scene.hpp"
#include "runtime/world/world_system.hpp"

#include <flatbuffers/flatbuffers.h>
#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <memory>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace
{

using namespace engine;

bool expect(bool value, const char* message)
{
    if (!value)
    {
        std::fprintf(stderr, "resource catalog: %s\n", message);
    }
    return value;
}

std::string digest(const std::vector<std::byte>& bytes)
{
    std::uint64_t value = 14695981039346656037ULL;
    for (std::byte byte : bytes)
    {
        value ^= std::to_integer<std::uint8_t>(byte);
        value *= 1099511628211ULL;
    }
    std::array<char, 17> text{};
    std::snprintf(text.data(), text.size(), "%016llx", static_cast<unsigned long long>(value));
    return text.data();
}

template <typename T> std::vector<std::byte> pack(const T& data, const char* identifier)
{
    flatbuffers::FlatBufferBuilder builder;
    builder.Finish(T::TableType::Pack(builder, &data), identifier);
    const auto* bytes = reinterpret_cast<const std::byte*>(builder.GetBufferPointer());
    return {bytes, bytes + builder.GetSize()};
}

void add(ResourceCatalog& catalog, const char* guid, const char* path, schema::AssetKind kind,
         std::vector<std::byte> bytes, std::vector<std::string> dependencies = {})
{
    auto entry = std::make_unique<schema::AssetEntryT>();
    entry->guid = guid;
    entry->path = path;
    entry->kind = kind;
    entry->content_size = bytes.size();
    entry->content_hash = digest(bytes);
    entry->dependencies = std::move(dependencies);
    (void)catalog.identities.add(path, guid);
    catalog.artifacts.emplace(guid, std::move(bytes));
    catalog.data.entries.push_back(std::move(entry));
}

bool write(const std::filesystem::path& path, const std::vector<std::byte>& bytes)
{
    std::ofstream stream(path, std::ios::binary);
    stream.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    stream.close();
    return static_cast<bool>(stream);
}

bool run(const std::filesystem::path& temporary)
{
    ResourceSystem resources;
    WorldSystem world;
    EngineConfig config;
    if (!resources.initialize(config) || !world.initialize(config))
    {
        return false;
    }
    auto* source = world.create_scene("External resources");
    auto* camera_node = source ? source->create_node("Camera") : nullptr;
    auto* cube_node = source ? source->create_node("Triangle") : nullptr;
    auto* camera = camera_node ? camera_node->add_component<Camera>() : nullptr;
    auto* renderer = cube_node ? cube_node->add_component<MeshRenderer>() : nullptr;
    if (!camera || !renderer || !source->set_active_camera(camera) ||
        !cube_node->set_parent(camera_node))
    {
        return false;
    }
    schema::SceneDataT scene;
    if (!source->build_data(scene))
    {
        return false;
    }
    const auto runtime_camera_id = camera_node->id();
    if (!expect(scene.nodes[0]->info->id != runtime_camera_id,
                "serialized local IDs exclude flecs IDs"))
    {
        return false;
    }
    world.destroy_scene();

    ResourceCatalog catalog;
    catalog.data.revision = "test-revision";
#if defined(__ANDROID__)
    catalog.data.target_platform = "android";
#elif defined(__APPLE__) && TARGET_OS_IPHONE
    catalog.data.target_platform = "ios";
#elif defined(__APPLE__)
    catalog.data.target_platform = "macos";
#elif defined(_WIN32)
    catalog.data.target_platform = "windows";
#else
    catalog.data.target_platform = "linux";
#endif
    constexpr const char* shader_guid = "10000000-0000-4000-8000-000000000001";
    constexpr const char* material_guid = "10000000-0000-4000-8000-000000000002";
    constexpr const char* mesh_guid = "10000000-0000-4000-8000-000000000003";
    constexpr const char* scene_guid = "10000000-0000-4000-8000-000000000004";
    constexpr const char* bad_scene_guid = "10000000-0000-4000-8000-000000000005";
    auto shader = *ResourceSystem::find_shader("builtin/shaders/pbr");
    shader.path = "project/shaders/test.shader";
    auto material = *ResourceSystem::find_material("builtin/materials/default-pbr");
    material.type_path = "project/materials/test.material";
    material.shader_guid = shader_guid;
    material.shader_path = shader.path;
    auto mesh = *ResourceSystem::find_mesh("builtin/meshes/cube");
    mesh.path = "project/meshes/triangle.mesh";
    mesh.vertices.resize(3);
    mesh.indices = {0, 1, 2};
    for (auto& component : scene.nodes[1]->components)
    {
        if (auto* data = component->payload.AsMeshRendererData())
        {
            data->mesh_guid = mesh_guid;
            data->material_guid = material_guid;
        }
    }
    add(catalog, shader_guid, shader.path.c_str(), schema::AssetKind::shader, pack(shader, "ESHD"));
    add(catalog, material_guid, material.type_path.c_str(), schema::AssetKind::material,
        pack(material, "EMAT"), {shader_guid});
    add(catalog, mesh_guid, mesh.path.c_str(), schema::AssetKind::mesh, pack(mesh, "EMSH"));
    add(catalog, scene_guid, "project/scenes/main.scene", schema::AssetKind::scene,
        pack(scene, "ESCN"), {mesh_guid, material_guid});
    auto wrong_reference = scene;
    for (auto& component : wrong_reference.nodes[1]->components)
    {
        if (auto* data = component->payload.AsMeshRendererData())
        {
            data->mesh_guid = material_guid;
        }
    }
    add(catalog, "10000000-0000-4000-8000-000000000006", "project/scenes/wrong-type.scene",
        schema::AssetKind::scene, pack(wrong_reference, "ESCN"), {material_guid});
    auto bad_scene = scene;
    bad_scene.nodes[0]->info->parent_id = bad_scene.nodes[1]->info->id;
    add(catalog, bad_scene_guid, "project/scenes/bad.scene", schema::AssetKind::scene,
        pack(bad_scene, "ESCN"), {mesh_guid, material_guid});
    std::error_code error;
    std::filesystem::create_directories(temporary / "gen", error);
    if (error || !write(temporary / "catalog.bin", pack(catalog.data, "EASC")))
    {
        return false;
    }
    for (const auto& [guid, bytes] : catalog.artifacts)
    {
        if (!write(temporary / "gen" / guid, bytes))
        {
            return false;
        }
    }
    ResourceCatalog loaded;
    const auto* builtin = ResourceSystem::find_shader("builtin/shaders/pbr");
    if (!expect(ResourceSystem::read_catalog(temporary, loaded) &&
                    ResourceSystem::activate_catalog(loaded),
                "read and activate generated GUID cache") ||
        !expect(ResourceSystem::find_shader("builtin/shaders/pbr") == builtin,
                "activation preserves builtin definition borrows"))
    {
        return false;
    }
    Guid resolved;
    std::string path;
    if (!expect(ResourceSystem::path_to_guid("project/scenes/main.scene", resolved) &&
                    resolved == scene_guid && ResourceSystem::guid_to_path(resolved, path) &&
                    path == "project/scenes/main.scene",
                "bidirectional resource identity"))
    {
        return false;
    }
    auto* restored = world.load_scene(path);
    if (!expect(restored && restored->nodes().size() == 2 && restored->active_camera() &&
                    restored->nodes()[1]->parent() == restored->nodes()[0].get(),
                "restore nodes, parent hierarchy and active camera"))
    {
        return false;
    }
    const auto* restored_mesh = restored->nodes()[1]->get_component<MeshRenderer>();
    if (!expect(restored_mesh && restored_mesh->mesh_path() == mesh.path &&
                    restored_mesh->material().shader_path() == shader.path &&
                    ResourceSystem::find_mesh(mesh.path)->indices.size() == 3,
                "scene resolves independently compiled mesh/material/shader GUIDs"))
    {
        return false;
    }
    schema::SceneDataT restored_data;
    if (!expect(restored->build_data(restored_data) && restored_data == scene,
                "stable local IDs and external references survive runtime reconstruction") ||
        !expect(world.load_scene("project/scenes/bad.scene") == nullptr &&
                    world.scene() == restored,
                "cyclic candidate failure preserves active scene") ||
        !expect(world.load_scene("project/scenes/wrong-type.scene") == nullptr &&
                    world.scene() == restored,
                "wrong resource kind cannot be used as mesh and preserves active scene"))
    {
        return false;
    }
    auto invalid = loaded;
    material.shader_guid = "10000000-0000-4000-8000-000000000099";
    invalid.artifacts[material_guid] = pack(material, "EMAT");
    for (auto& entry : invalid.data.entries)
    {
        if (entry->guid == material_guid)
        {
            entry->content_size = invalid.artifacts[material_guid].size();
            entry->content_hash = digest(invalid.artifacts[material_guid]);
        }
    }
    const auto* previous_material = ResourceSystem::find_material(material.type_path);
    if (!expect(!ResourceSystem::activate_catalog(invalid) &&
                    ResourceSystem::find_material(material.type_path) == previous_material &&
                    ResourceSystem::find_shader("builtin/shaders/pbr") == builtin,
                "failed activation preserves complete previous registry"))
    {
        return false;
    }
    auto foreign = catalog;
    foreign.data.target_platform = catalog.data.target_platform == "android" ? "macos" : "android";
    if (!write(temporary / "catalog.bin", pack(foreign.data, "EASC")) ||
        !expect(ResourceSystem::read_catalog(temporary, loaded) &&
                    !ResourceSystem::activate_catalog(loaded) &&
                    ResourceSystem::find_material(material.type_path) == previous_material,
                "host validates Android cache for sync but cannot activate Vulkan assets") ||
        !write(temporary / "catalog.bin", pack(catalog.data, "EASC")))
    {
        return false;
    }
    auto missing_dependency = catalog;
    missing_dependency.data.entries.front()->dependencies = {
        "10000000-0000-4000-8000-000000000099"};
    if (!write(temporary / "catalog.bin", pack(missing_dependency.data, "EASC")) ||
        !expect(!ResourceSystem::read_catalog(temporary, loaded),
                "unknown GUID dependency rejects catalog") ||
        !write(temporary / "catalog.bin", pack(catalog.data, "EASC")))
    {
        return false;
    }
    const std::vector<std::byte> changed{std::byte{0}};
    if (!write(temporary / "gen" / scene_guid, changed))
    {
        return false;
    }
    if (!expect(world.load_scene("project/scenes/main.scene") != nullptr,
                "loaded snapshot unaffected by mutable GUID disk overwrite") ||
        !expect(!ResourceSystem::read_catalog(temporary, loaded) && loaded.artifacts.empty() &&
                    loaded.identities.size() == 0,
                "mismatched content rejects cache and clears output"))
    {
        return false;
    }
    world.destroy_scene();
    ResourceSystem::clear_project_definitions();
    return expect(ResourceSystem::find_mesh(mesh.path) == nullptr &&
                      ResourceSystem::find_mesh("builtin/meshes/cube") != nullptr &&
                      !ResourceSystem::path_to_guid("project/scenes/main.scene", resolved),
                  "project stop clears catalog and project definitions, preserves builtins");
}

} // namespace

int main()
{
    std::error_code error;
    const auto temporary =
        std::filesystem::temp_directory_path(error) /
        ("engine-catalog-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    if (error)
    {
        return 1;
    }
    const bool result = run(temporary);
    std::filesystem::remove_all(temporary, error);
    return result && !error ? 0 : 1;
}
