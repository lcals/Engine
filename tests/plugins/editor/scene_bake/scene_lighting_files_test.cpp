#include "resource_fixture.hpp"
#include "scene_snapshot.hpp"

#include "editor/asset_pipeline/asset_io.hpp"
#include "editor/asset_pipeline/importers/mesh_importer.hpp"
#include "editor/asset_pipeline/scene_importer.hpp"
#include "plugins/editor/scene_bake/lighting_bake_job.hpp"
#include "plugins/editor/scene_bake/scene_bake_files.hpp"
#include "runtime/engine_config.hpp"

#include <cstdio>
#include <fstream>
#include <map>

namespace
{
using namespace engine;
namespace io = editor::asset_pipeline;
namespace bake = plugins::scene_bake;
namespace fs = std::filesystem;

bool check(bool value, const char* message, const std::string& error = {})
{
    if (!value)
    {
        std::fprintf(stderr, "lighting files: %s: %s\n", message, error.c_str());
    }
    return value;
}

template <class T> void component(schema::NodeDataT& node, T data)
{
    auto c = test_support::component_data();
    c->payload.Set(std::move(data));
    node.components.push_back(std::move(c));
}

bool exercise()
{
    test_support::ResourceFixture fixture;
    if (fixture.root.empty())
    {
        return false;
    }
    std::error_code code;
    fs::create_directories(fixture.root / "assets", code);
    fs::create_directories(fixture.root / "cache", code);
    if (fixture.root.empty() || code)
    {
        return false;
    }
    schema::MeshDataT mesh;
    mesh.path = "source.mesh";
    constexpr std::uint32_t width = 4;
    for (std::uint32_t y = 0; y <= width; ++y)
    {
        for (std::uint32_t x = 0; x <= width; ++x)
        {
            mesh.vertices.emplace_back(x, y, 0, 0, 0, 1);
            mesh.lightmap_texcoords.emplace_back(double(x) / width, double(y) / width);
        }
    }
    for (std::uint32_t y = 0; y < width; ++y)
    {
        for (std::uint32_t x = 0; x < width; ++x)
        {
            const auto vertex = y * (width + 1) + x;
            mesh.indices.insert(mesh.indices.end(), {vertex, vertex + 1, vertex + width + 2, vertex,
                                                     vertex + width + 2, vertex + width + 1});
        }
    }
    if (!fixture.add(mesh))
    {
        return false;
    }
    const auto mesh_guid = fixture.catalog.entries.front()->guid;
    constexpr const char* scene_guid = "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa";
    schema::SceneDataT scene;
    scene.info = std::make_unique<schema::SceneInfoT>();
    scene.info->name = "Lighting test";
    auto node = std::make_unique<schema::NodeDataT>();
    node->info = test_support::node_info();
    node->info->properties->id = 1;
    node->info->properties->name = "Fixed mesh";
    component(*node, schema::TransformDataT{});
    schema::MeshRendererDataT renderer;
    renderer.mesh_guid = mesh_guid;
    renderer.material_guid = "00000000-0000-4000-8000-000000000003";
    renderer.bake_static = true;
    component(*node, renderer);
    scene.nodes.push_back(std::move(node));
    node = std::make_unique<schema::NodeDataT>();
    node->info = test_support::node_info();
    node->info->properties->id = 2;
    node->info->properties->name = "Probes";
    component(*node, schema::TransformDataT{});
    component(*node, schema::LightProbeVolumeDataT{});
    scene.nodes.push_back(std::move(node));
    std::string error, original;
    const auto scene_path = fixture.root / "assets/test.scene";
    fs::create_directories(fixture.root / "assets/test", code);
    const auto output_meta = fixture.root / "assets/test/lighting-1.mesh.meta";
    const std::string preserved_meta =
        "guid = \"bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb\"\nimporter = \"mesh\"\n"
        "meta_version = 1\nartist_note = \"retain import settings\"\n"
        "[lod]\nenabled = true\ntriangle_ratios = [0.5]\nscreen_heights = [0.2]\n"
        "max_relative_error = 0.01\n";
    if (!check(
            editor::SceneImporter::write(scene_path, scene, error) &&
                ResourceSystem::read_text(scene_path, original) &&
                io::write_text(fixture.root / "assets/source.mesh", "original geometry", error) &&
                io::write_text(fixture.root / "assets/source.mesh.meta",
                               "guid = \"" + mesh_guid + "\"\n", error) &&
                io::write_text(fixture.root / "assets/test.scene.meta",
                               "guid = \"" + std::string(scene_guid) + "\"\n", error) &&
                io::write_text(output_meta, preserved_meta, error),
            "source scene and metadata prepared", error))
    {
        return false;
    }
    flatbuffers::FlatBufferBuilder builder;
    schema::FinishSceneDataBuffer(builder, schema::SceneData::Pack(builder, &scene));
    auto entry = std::make_unique<schema::AssetEntryT>();
    entry->guid = scene_guid;
    entry->path = "test.scene";
    entry->kind = schema::AssetKind::scene;
    entry->dependencies = {mesh_guid};
    ByteBuffer entity;
    if (!ResourceSystem::encode_resource(
            scene_guid, entry->kind,
            std::as_bytes(std::span(builder.GetBufferPointer(), builder.GetSize())), entity,
            entry->header_size))
    {
        return false;
    }
    entry->content_hash = io::hash_bytes(entity);
    entry->content_size = entity.size();
    entry->location = std::make_unique<schema::FileLocationT>();
    entry->location->mount = "content";
    entry->location->path = "gen/" + entry->guid;
    entry->location->length = entity.size();
    if (!io::write_bytes(fixture.root / entry->location->path, entity, error))
    {
        return false;
    }
    fixture.catalog.entries.push_back(std::move(entry));
    ResourceSystem resources;
    if (!check(resources.initialize({}) && fixture.publish(resources), "catalog publishes"))
    {
        return false;
    }
    bake::LightingBakeSettings settings;
    settings.probes.push_back({2, schema::Vec3(-1, -1, -1), schema::Vec3(1, 1, 1), {2, 2, 2}});
    bake::SceneBakeFiles files;
    if (!check(files.initialize_lighting(fixture.root, "test.scene", "test-1", resources, settings,
                                         error) &&
                   files.outputs().size() == 3 && files.unchanged(resources, error),
               "outputs reserve identities", error))
    {
        return false;
    }
    bake::LightingBakeOutputs output;
    bake::BakedMeshLighting fixed;
    fixed.node_id = 1;
    fixed.mesh = std::make_shared<schema::MeshDataT>(mesh);
    fixed.transform = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    fixed.lightmap = std::make_shared<schema::TextureDataT>();
    fixed.lightmap->data = {1, 2, 3, 4};
    output.meshes.push_back(fixed);
    bake::BakedProbeLighting probe;
    probe.node_id = 2;
    probe.data.path = "unused.lightprobe";
    probe.data.bounds_min = schema::Vec3(-1, -1, -1);
    probe.data.bounds_max = schema::Vec3(1, 1, 1);
    probe.data.grid_x = probe.data.grid_y = probe.data.grid_z = 2;
    probe.data.valid.assign(8, 1);
    probe.data.coefficients.resize(72);
    probe.data.baked_transform.assign(fixed.transform.begin(), fixed.transform.end());
    probe.data.source_fingerprint = settings.source_fingerprint;
    output.probes.push_back(probe);
    if (!check(files.stage_lighting(output, error), "outputs stage without changing scene", error))
    {
        return false;
    }
    std::string staged_meta;
    ByteBuffer staged_mesh;
    if (!check(
            ResourceSystem::read_text(fixture.root / "cache/bake/test-1/0.new.meta", staged_meta) &&
                staged_meta == preserved_meta &&
                ResourceSystem::read_binary(fixture.root / "cache/bake/test-1/compiled" /
                                                files.outputs()[0].guid,
                                            staged_mesh),
            "staged source has its exact adjacent meta including existing LOD settings"))
    {
        return false;
    }
    const auto compiled = schema::UnPackMeshData(staged_mesh.data());
    if (!check(
            compiled && !compiled->lods.empty() &&
                compiled->lods[0]->indices.size() < compiled->indices.size(),
            "real staged importer applies preserved LOD settings rather than defaulting them away"))
    {
        return false;
    }
    std::string current;
    if (!check(ResourceSystem::read_text(scene_path, current) && current == original,
               "staging preserves original scene"))
    {
        return false;
    }
    unsigned refreshes = 0;
    if (!check(!files.publish(
                   [&](std::string& message)
                   {
                       ++refreshes;
                       message = "simulated import failure";
                       return refreshes > 1;
                   },
                   error) &&
                   refreshes == 2 && ResourceSystem::read_text(scene_path, current) &&
                   current == original &&
                   !fs::exists(fixture.root / "assets/test/lighting-1.mesh") &&
                   ResourceSystem::read_text(output_meta, staged_meta) &&
                   staged_meta == preserved_meta,
               "failed import rolls back whole batch", error))
    {
        return false;
    }
    files.cleanup();
    files = {};
    error.clear();
    if (!check(files.initialize_lighting(fixture.root, "test.scene", "test-2", resources, settings,
                                         error) &&
                   io::write_text(fixture.root / "assets/source.mesh", "edited geometry", error) &&
                   !files.unchanged(resources, error),
               "input edit invalidates bake", error))
    {
        return false;
    }
    if (!io::write_text(fixture.root / "assets/source.mesh", "original geometry", error))
    {
        return false;
    }
    error.clear();
    if (!check(files.unchanged(resources, error) && files.stage_lighting(output, error) &&
                   files.publish([](std::string&) { return true; }, error),
               "complete batch publishes", error))
    {
        return false;
    }
    schema::SceneDataT loaded;
    if (!check(editor::SceneImporter::read(scene_path, loaded, error), "published scene reads",
               error))
    {
        return false;
    }
    const auto* result = loaded.nodes[0]->components[1]->payload.AsMeshRendererData();
    if (!check(result && result->mesh_guid == files.outputs()[0].guid &&
                   result->lightmap_guid == files.outputs()[1].guid &&
                   result->lightmap_mesh_hash.size() == 16 &&
                   result->lightmap_transform.size() == 16 &&
                   loaded.nodes[1]->components[1]->payload.AsLightProbeVolumeData()->probes_guid ==
                       files.outputs()[2].guid &&
                   fs::exists(fixture.root / "assets/test/lighting-1.mesh.meta") &&
                   fs::exists(fixture.root / "assets/test/probes-2.lightprobe.meta") &&
                   ResourceSystem::read_text(output_meta, staged_meta) &&
                   staged_meta == preserved_meta,
               "published scene binds UV2 mesh hash, lightmap, transform, and probe identities"))
    {
        return false;
    }
    GuidMap identities;
    std::map<Guid, schema::AssetKind> kinds;
    io::ImportContext context{identities,
                              kinds,
                              fixture.root,
                              fixture.root / "assets",
                              fixture.root / "compiled-published",
                              {},
                              nullptr,
                              {}};
    fs::create_directories(context.output, code);
    io::ImportAsset asset;
    asset.guid = files.outputs()[0].guid;
    asset.path = files.outputs()[0].path;
    asset.source = fixture.root / "assets" / asset.path;
    asset.kind = schema::AssetKind::mesh;
    io::MeshImporter importer;
    ByteBuffer payload, encoded;
    std::uint32_t header = 0;
    return check(
        !code && importer.convert(asset, context, error) &&
            ResourceSystem::read_binary(context.output / asset.guid, payload) &&
            ResourceSystem::encode_resource(asset.guid, asset.kind, payload, encoded, header) &&
            io::hash_bytes(encoded) == result->lightmap_mesh_hash,
        "published importer and staged importer produce the exact bound mesh hash", error);
}
} // namespace
int main()
{
    return exercise() ? 0 : 1;
}
