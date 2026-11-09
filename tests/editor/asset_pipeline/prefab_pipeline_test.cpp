#include "editor/asset_pipeline/asset_io.hpp"
#include "editor/asset_pipeline/importers/prefab_importer.hpp"
#include "editor/asset_pipeline/importers/scene_importer.hpp"
#include "editor/asset_pipeline/prefab_importer.hpp"
#include "editor/asset_pipeline/scene_importer.hpp"
#include "runtime/resource/resource_system.hpp"
#include "runtime/resource/virtual_file_system.hpp"
#include "scene_snapshot.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <flatbuffers/verifier.h>
#include <fstream>
#include <limits>
#include <map>

namespace
{
using namespace engine;
using namespace engine::editor;
using namespace engine::editor::asset_pipeline;
constexpr auto prefab_guid = "10000000-0000-4000-8000-000000000001";
constexpr auto scene_guid = "10000000-0000-4000-8000-000000000002";
constexpr auto mesh_guid = "10000000-0000-4000-8000-000000000003";
constexpr auto material_a = "10000000-0000-4000-8000-000000000004";
constexpr auto material_b = "10000000-0000-4000-8000-000000000005";

bool expect(bool result, const char* message, const std::string& error = {})
{
    if (!result)
    {
        std::fprintf(stderr, "%s: %s\n", message, error.c_str());
    }
    return result;
}

std::unique_ptr<schema::NodeDataT> node(std::uint64_t id, std::uint64_t parent = 0)
{
    auto result = std::make_unique<schema::NodeDataT>();
    result->info = test_support::node_info();
    result->info->properties->id = id;
    result->info->parent_id = parent;
    result->info->properties->name = "Template";
    auto transform = test_support::component_data();
    transform->payload.Set(schema::TransformDataT{});
    result->components.push_back(std::move(transform));
    return result;
}

schema::PrefabInstanceDataT instance(std::uint64_t root)
{
    schema::PrefabInstanceDataT result;
    result.prefab_guid = prefab_guid;
    result.root_id = root;
    auto mapping = std::make_unique<schema::NodeIdMappingT>();
    mapping->source_id = 1;
    mapping->target_id = root;
    result.node_mappings.push_back(std::move(mapping));
    return result;
}

bool composition(const std::filesystem::path& directory)
{
    std::string error;
    schema::PrefabDataT prefab;
    prefab.schema_version = 3;
    prefab.root_id = 1;
    prefab.nodes.push_back(node(1));
    prefab.nodes.push_back(node(2, 1));
    prefab.nodes.push_back(node(3, 2));
    auto first = instance(10);
    auto override = std::make_unique<schema::PrefabNodeOverrideT>();
    override->node_id = 1;
    override->info = test_support::node_info();
    override->info->properties->enabled = false;
    override->fields = {"enabled"};
    auto transform = std::make_unique<schema::PrefabComponentOverrideT>();
    transform->component = test_support::component_data();
    schema::TransformDataT settings;
    settings.position = {0, 0, 0};
    transform->component->payload.Set(settings);
    transform->fields = {"position"};
    override->components.push_back(std::move(transform));
    first.overrides.push_back(std::move(override));
    std::vector<std::unique_ptr<schema::NodeDataT>> expanded;
    if (!expect(PrefabImporter::expand_instance(prefab, first, 11, expanded, error),
                "expand first instance", error))
    {
        return false;
    }
    prefab.nodes[0]->info->properties->name = "Changed template";
    prefab.nodes[0]->components[0]->payload.AsTransformData()->position = {8, 0, 0};
    prefab.nodes[0]->components[0]->payload.AsTransformData()->scale = {2, 2, 2};
    if (!expect(PrefabImporter::expand_instance(prefab, first, 20, expanded, error),
                "refresh first instance", error) ||
        !expect(!expanded[0]->info->properties->enabled &&
                    expanded[0]->info->properties->name == "Changed template" &&
                    expanded[0]->components[0]->payload.AsTransformData()->position.x() == 0 &&
                    expanded[0]->components[0]->payload.AsTransformData()->scale.x() == 2,
                "false/zero persist while unselected fields inherit"))
    {
        return false;
    }
    schema::SceneDataT scene;
    scene.schema_version = 3;
    scene.info = std::make_unique<schema::SceneInfoT>();
    scene.info->name = "Instances";
    scene.nodes = std::move(expanded);
    scene.prefab_instances.push_back(std::make_unique<schema::PrefabInstanceDataT>(first));
    auto second = instance(30);
    if (!expect(PrefabImporter::expand_instance(prefab, second, 31, expanded, error),
                "expand independent second instance", error) ||
        !expect(expanded[0]->info->properties->enabled &&
                    expanded[0]->components[0]->payload.AsTransformData()->position.x() == 8,
                "instances do not share mutable state"))
    {
        return false;
    }
    for (auto& value : expanded)
    {
        scene.nodes.push_back(std::move(value));
    }
    scene.prefab_instances.push_back(std::make_unique<schema::PrefabInstanceDataT>(second));
    if (!expect(SceneImporter::write(directory / "instances.scene", scene, error),
                "save source association", error))
    {
        return false;
    }
    schema::SceneDataT source;
    if (!expect(SceneImporter::read(directory / "instances.scene", source, error),
                "read source association", error) ||
        !expect(source.nodes.empty() && source.prefab_instances.size() == 2 &&
                    source.prefab_instances[0]->node_mappings[1]->target_id == 11,
                "mapped nodes omitted and identities preserved"))
    {
        return false;
    }
    auto removal = std::make_unique<schema::PrefabNodeOverrideT>();
    removal->node_id = 2;
    removal->removed = true;
    first.overrides.push_back(std::move(removal));
    if (!expect(PrefabImporter::expand_instance(prefab, first, 50, expanded, error),
                "remove subtree", error) ||
        !expect(expanded.size() == 1 && first.overrides.back()->node_id == 3 &&
                    first.overrides.back()->removed,
                "descendants receive explicit removal"))
    {
        return false;
    }
    prefab.nodes.resize(1);
    if (!expect(PrefabImporter::expand_instance(prefab, second, 50, expanded, error),
                "template removes nodes", error) ||
        !expect(second.node_mappings[1]->retired, "deleted template identities stay reserved"))
    {
        return false;
    }
    prefab.nodes.push_back(node(2, 1));
    if (!expect(PrefabImporter::expand_instance(prefab, second, 50, expanded, error),
                "template restores stable node", error) ||
        !expect(!second.node_mappings[1]->retired && expanded[1]->info->properties->id == 31,
                "restored node reuses its own identity"))
    {
        return false;
    }
    first.overrides[0]->components[0]->fields = {"not_a_field"};
    return expect(!PrefabImporter::expand_instance(prefab, first, 50, expanded, error),
                  "unknown override field rejected");
}

bool model_identity()
{
    std::string error;
    schema::ModelDataT model;
    model.schema_version = 2;
    model.path = "models/test.model";
    for (const auto* key : {"/Root/Left", "/Root/Right"})
    {
        auto value = std::make_unique<schema::ModelNodeDataT>();
        value->name = key;
        value->source_key = key;
        value->mesh_guid = mesh_guid;
        model.nodes.push_back(std::move(value));
    }
    schema::PrefabDataT prefab;
    prefab.schema_version = 3;
    prefab.root_id = 1;
    if (!expect(PrefabImporter::from_model(model, material_a, prefab, error),
                "Model uses injected default material offline", error))
    {
        return false;
    }
    const auto left = prefab.nodes[1]->info->properties->id;
    std::swap(model.nodes[0], model.nodes[1]);
    return expect(PrefabImporter::from_model(model, material_a, prefab, error),
                  "reordered Model rebuild", error) &&
           expect(prefab.nodes[2]->info->properties->id == left &&
                      prefab.nodes[2]->components[1]->payload.AsMeshRendererData()->material_guid ==
                          material_a,
                  "source keys preserve identity across sibling reorder");
}

bool dependencies(const std::filesystem::path& directory)
{
    std::string error;
    GuidMap identities;
    std::map<Guid, schema::AssetKind> kinds;
    const auto add = [&](const char* path, const char* guid, schema::AssetKind kind)
    {
        kinds.emplace(guid, kind);
        return identities.add(path, guid);
    };
    if (!add("template.prefab", prefab_guid, schema::AssetKind::prefab) ||
        !add("level.scene", scene_guid, schema::AssetKind::scene) ||
        !add("mesh.mesh", mesh_guid, schema::AssetKind::mesh) ||
        !add("a.material", material_a, schema::AssetKind::material) ||
        !add("b.material", material_b, schema::AssetKind::material))
    {
        return false;
    }
    ImportContext context{identities, kinds, directory, directory, directory, "test", nullptr, {}};
    schema::PrefabDataT prefab;
    prefab.schema_version = 3;
    prefab.root_id = 1;
    prefab.nodes.push_back(node(1));
    schema::MeshRendererDataT mesh;
    mesh.mesh_guid = mesh_guid;
    mesh.material_guid = material_a;
    auto component = test_support::component_data();
    component->payload.Set(mesh);
    prefab.nodes[0]->components.push_back(std::move(component));
    ImportAsset template_asset;
    template_asset.guid = prefab_guid;
    template_asset.kind = schema::AssetKind::prefab;
    template_asset.source = directory / "template.prefab";
    PrefabAssetImporter prefab_importer;
    if (!expect(PrefabImporter::write(template_asset.source, prefab, error) &&
                    prefab_importer.scan_dependencies(template_asset, context, error) &&
                    prefab_importer.convert(template_asset, context, error) &&
                    prefab_importer.finalize_dependencies(template_asset, context, error),
                "import direct prefab", error))
    {
        return false;
    }
    auto placed = instance(10);
    auto override = std::make_unique<schema::PrefabNodeOverrideT>();
    override->node_id = 1;
    auto material = std::make_unique<schema::PrefabComponentOverrideT>();
    material->component = test_support::component_data();
    mesh.material_guid = material_b;
    material->component->payload.Set(mesh);
    material->fields = {"material"};
    override->components.push_back(std::move(material));
    auto text = std::make_unique<schema::PrefabComponentOverrideT>();
    text->component = test_support::component_data();
    schema::TextDataT text_data;
    text_data.text = scene_guid;
    text_data.shader_path = "builtin://shaders/font";
    text->component->payload.Set(text_data);
    text->fields = {"text"};
    override->components.push_back(std::move(text));
    placed.overrides.push_back(std::move(override));
    schema::SceneDataT scene;
    scene.schema_version = 3;
    scene.info = std::make_unique<schema::SceneInfoT>();
    scene.info->name = "Dependency test";
    if (!PrefabImporter::expand_instance(prefab, placed, 11, scene.nodes, error))
    {
        return false;
    }
    scene.prefab_instances.push_back(std::make_unique<schema::PrefabInstanceDataT>(placed));
    ImportAsset scene_asset;
    scene_asset.guid = scene_guid;
    scene_asset.kind = schema::AssetKind::scene;
    scene_asset.source = directory / "level.scene";
    SceneAssetImporter scene_importer;
    if (!expect(SceneImporter::write(scene_asset.source, scene, error) &&
                    scene_importer.scan_dependencies(scene_asset, context, error) &&
                    scene_importer.convert(scene_asset, context, error) &&
                    scene_importer.finalize_dependencies(scene_asset, context, error),
                "compose scene with exact dependencies", error))
    {
        return false;
    }
    const auto& dependencies = scene_asset.runtime_dependencies;
    if (!expect(dependencies.size() == 2 &&
                    std::find(dependencies.begin(), dependencies.end(), material_b) !=
                        dependencies.end() &&
                    std::find(dependencies.begin(), dependencies.end(), mesh_guid) !=
                        dependencies.end(),
                "scene excludes source prefab and replaced material"))
    {
        return false;
    }
    if (!expect(std::find(scene_asset.build_dependencies.begin(),
                          scene_asset.build_dependencies.end(),
                          scene_guid) == scene_asset.build_dependencies.end(),
                "GUID-shaped text is not a resource dependency"))
    {
        return false;
    }
    schema::AssetCatalogDataT previous;
    const auto publish_scene = [&](bool legacy_node_layout = false)
    {
        std::vector<std::byte> payload, entity;
        if (!VirtualFileSystem::read_file(directory / scene_guid, payload))
        {
            return false;
        }
        if (legacy_node_layout)
        {
            const auto snapshot = schema::UnPackSceneData(payload.data());
            flatbuffers::FlatBufferBuilder builder;
            std::vector<flatbuffers::Offset<schema::PrefabInstanceData>> instances;
            for (const auto& value : snapshot->prefab_instances)
            {
                instances.push_back(schema::PrefabInstanceData::Pack(builder, value.get()));
            }
            const auto legacy_name = builder.CreateString("Pre-ECS cached node");
            const auto start = builder.StartTable();
            // Before ECS, NodeInfo stored id/name/parent directly at these field slots.
            // Current import must preserve identities without interpreting this old payload.
            builder.AddElement<std::uint64_t>(4, 10, 0);
            builder.AddOffset(6, legacy_name);
            const auto legacy_info = flatbuffers::Offset<schema::NodeInfo>(builder.EndTable(start));
            const auto legacy_node = schema::CreateNodeData(builder, legacy_info);
            const auto nodes = builder.CreateVector(std::vector{legacy_node});
            const auto mappings = builder.CreateVector(instances);
            const auto info = schema::SceneInfo::Pack(builder, snapshot->info.get());
            schema::FinishSceneDataBuffer(
                builder, schema::CreateSceneData(builder, 2, info, nodes, mappings));
            flatbuffers::Verifier verifier(builder.GetBufferPointer(), builder.GetSize());
            if (!expect(!schema::VerifySceneDataBuffer(verifier),
                        "fixture uses incompatible cached node layout"))
            {
                return false;
            }
            const auto* bytes = reinterpret_cast<const std::byte*>(builder.GetBufferPointer());
            payload.assign(bytes, bytes + builder.GetSize());
        }
        auto entry = std::make_unique<schema::AssetEntryT>();
        entry->guid = scene_guid;
        entry->kind = schema::AssetKind::scene;
        if (!ResourceSystem::encode_resource(scene_guid, entry->kind, payload, entity,
                                             entry->header_size))
        {
            return false;
        }
        entry->content_hash = hash_bytes(entity);
        entry->content_size = entity.size();
        std::error_code code;
        std::filesystem::create_directories(directory / "cache" / "gen", code);
        if (code || !write_bytes(directory / "cache" / "gen" / scene_guid, entity, error))
        {
            return false;
        }
        std::erase_if(previous.entries,
                      [](const auto& value) { return value->guid == scene_guid; });
        previous.entries.push_back(std::move(entry));
        context.previous_catalog = &previous;
        return true;
    };
    prefab.nodes.push_back(node(2, 1));
    if (!PrefabImporter::write(template_asset.source, prefab, error) ||
        !prefab_importer.convert(template_asset, context, error) ||
        !scene_importer.convert(scene_asset, context, error) || !publish_scene(true))
    {
        return false;
    }
    prefab.nodes.insert(prefab.nodes.begin() + 1, node(3, 1));
    if (!PrefabImporter::write(template_asset.source, prefab, error) ||
        !prefab_importer.convert(template_asset, context, error) ||
        !scene_importer.convert(scene_asset, context, error))
    {
        return false;
    }
    std::vector<std::byte> rebuilt;
    if (!VirtualFileSystem::read_file(directory / scene_guid, rebuilt))
    {
        return false;
    }
    const auto rebuilt_scene = schema::UnPackSceneData(rebuilt.data());
    const auto stable = std::find_if(rebuilt_scene->prefab_instances[0]->node_mappings.begin(),
                                     rebuilt_scene->prefab_instances[0]->node_mappings.end(),
                                     [](const auto& value) { return value->source_id == 2; });
    if (!expect(stable != rebuilt_scene->prefab_instances[0]->node_mappings.end() &&
                    (*stable)->target_id == 11,
                "cache schema upgrade and template additions preserve generated IDs without saving "
                "scene source"))
    {
        return false;
    }
    std::vector<std::byte> payload, entity;
    if (!VirtualFileSystem::read_file(directory / prefab_guid, payload))
    {
        return false;
    }
    auto entry = std::make_unique<schema::AssetEntryT>();
    entry->guid = prefab_guid;
    entry->kind = schema::AssetKind::prefab;
    if (!ResourceSystem::encode_resource(prefab_guid, entry->kind, payload, entity,
                                         entry->header_size))
    {
        return false;
    }
    entry->content_size = entity.size();
    entry->content_hash = hash_bytes(entity);
    std::ofstream stream(directory / prefab_guid, std::ios::binary | std::ios::trunc);
    stream.write(reinterpret_cast<const char*>(entity.data()),
                 static_cast<std::streamsize>(entity.size()));
    stream.close();
    previous.entries.push_back(std::move(entry));
    context.previous_catalog = &previous;
    if (!expect(scene_importer.convert(scene_asset, context, error),
                "compose against reused wrapped prefab", error) ||
        !publish_scene())
    {
        return false;
    }
    if (!VirtualFileSystem::read_file(directory / scene_guid, payload))
    {
        return false;
    }
    auto* corrupted_scene = schema::GetMutableSceneData(payload.data());
    auto* mappings =
        corrupted_scene->mutable_prefab_instances()->GetMutableObject(0)->mutable_node_mappings();
    // The resource wrapper is valid; the cached identity vector itself exceeds its payload.
    flatbuffers::WriteScalar<flatbuffers::uoffset_t>(
        reinterpret_cast<std::uint8_t*>(mappings),
        std::numeric_limits<flatbuffers::uoffset_t>::max());
    auto& scene_entry = **std::find_if(previous.entries.begin(), previous.entries.end(),
                                       [](const auto& value) { return value->guid == scene_guid; });
    if (!ResourceSystem::encode_resource(scene_guid, schema::AssetKind::scene, payload, entity,
                                         scene_entry.header_size))
    {
        return false;
    }
    scene_entry.content_size = entity.size();
    scene_entry.content_hash = hash_bytes(entity);
    if (!write_bytes(directory / "cache" / "gen" / scene_guid, entity, error))
    {
        return false;
    }
    error.clear();
    return expect(!scene_importer.convert(scene_asset, context, error) &&
                      error == "Invalid previous scene instance identities",
                  "malformed cached identity vector is rejected", error);
}
} // namespace

int main()
{
    std::error_code error;
    auto directory = std::filesystem::temp_directory_path(error);
    if (error)
    {
        return 1;
    }
    directory /= "engine-prefab-test-" +
                 std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    std::filesystem::create_directories(directory, error);
    if (error)
    {
        return 1;
    }
    const bool success = composition(directory) && model_identity() && dependencies(directory);
    std::filesystem::remove_all(directory, error);
    return success ? 0 : 1;
}
