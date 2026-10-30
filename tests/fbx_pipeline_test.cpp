#include "editor/asset_pipeline/asset_pipeline_system.hpp"
#include "plugins/editor/fbx_import/fbx_importer.hpp"
#if defined(ENGINE_FBX_TEST_BUILD)
#include "plugins/editor/asset_build/asset_build_system.hpp"
#endif
#if defined(ENGINE_FBX_TEST_GRAPH)
#include "plugins/runtime/resource_graph/resource_graph_system.hpp"
#endif
#include "runtime/engine_config.hpp"
#include "runtime/resource/resource_system.hpp"
#include "runtime/schema/animation_generated.h"
#include "runtime/schema/mesh_generated.h"
#include "runtime/schema/model_generated.h"
#include "runtime/schema/skeleton_generated.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace
{
namespace fs = std::filesystem;
using namespace engine;

bool expect(bool condition, const std::string& message)
{
    if (!condition)
    {
        std::fprintf(stderr, "FBX pipeline: %s\n", message.c_str());
    }
    return condition;
}

struct Project
{
    fs::path root;
    ~Project()
    {
        std::error_code code;
        if (!root.empty())
        {
            fs::remove_all(root, code);
        }
    }
};

bool write(const fs::path& path, const std::string& text)
{
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream << text;
    stream.close();
    return static_cast<bool>(stream);
}

bool read(const ResourceCatalog& catalog, const fs::path& root, const std::string& path,
          ByteBuffer& bytes)
{
    Guid guid;
    return catalog.identities().path_to_guid(path, guid) &&
           ResourceSystem::read_resource(catalog, {{"content", root / "cache"}}, guid, bytes);
}

double mean_normal_orientation(const schema::MeshData& mesh)
{
    double sum = 0.0;
    std::size_t count = 0;
    for (flatbuffers::uoffset_t index = 0; index < mesh.indices()->size(); index += 3)
    {
        const auto* a = mesh.vertices()->Get(mesh.indices()->Get(index));
        const auto* b = mesh.vertices()->Get(mesh.indices()->Get(index + 1));
        const auto* c = mesh.vertices()->Get(mesh.indices()->Get(index + 2));
        const double ux = b->x() - a->x(), uy = b->y() - a->y(), uz = b->z() - a->z();
        const double vx = c->x() - a->x(), vy = c->y() - a->y(), vz = c->z() - a->z();
        const double nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
        const double length = std::hypot(nx, ny, nz);
        if (length <= 0.000001)
        {
            continue;
        }
        for (const auto* vertex : {a, b, c})
        {
            sum += (nx * vertex->nx() + ny * vertex->ny() + nz * vertex->nz()) / length;
            ++count;
        }
    }
    return count == 0 ? 0.0 : sum / static_cast<double>(count);
}

#if defined(ENGINE_FBX_TEST_GRAPH)
bool model_loading(const Project& project, const ResourceCatalog& catalog, const Guid& model_guid,
                   const Guid& mesh_guid, const Guid& skeleton_guid)
{
    ResourceSystem resources;
    plugins::resource_graph::ResourceGraphSystem graph(resources);
    ResourcePtr model;
    if (!expect(
            resources.initialize({}) &&
                resources.update_catalog(catalog.data(), {{"content", project.root / "cache"}}) &&
                graph.initialize({}) && graph.load(model_guid, model) && model->model() &&
                graph.find(mesh_guid) && graph.find(skeleton_guid),
            "model loading resolves its mesh and the mesh's skeleton"))
    {
        return false;
    }
    for (const auto& entry : catalog.data().entries)
    {
        if (entry->kind == schema::AssetKind::animation &&
            !expect(!graph.find(entry->guid), "model loading does not retain any source animation"))
        {
            return false;
        }
    }
    return true;
}
#endif

#if defined(ENGINE_FBX_TEST_BUILD)
bool build_selection(editor::AssetPipelineSystem& pipeline, const Project& project,
                     const Guid& model_guid, const Guid& mesh_guid, const Guid& skeleton_guid)
{
    constexpr auto manifest = "settings/fbx-selection.toml";
    if (!write(project.root / manifest,
               "target_platform = \"\"\noutput = \"cache/build/selection\"\n"
               "[[items]]\nname = \"model\"\nstorage = \"package\"\n"
               "roots = [\"project/models/skeleton.fbx\"]\n"
               "[[items]]\nname = \"walk\"\nstorage = \"package\"\n"
               "roots = [\"project/models/locomotions.fbx/animations/walk\"]\n"
               "[[items]]\nname = \"walk-files\"\nstorage = \"files\"\n"
               "roots = [\"project/models/locomotions.fbx/animations/walk\"]\n"))
    {
        return false;
    }
    plugins::asset_build::AssetBuildSystem builder(pipeline);
    const auto output = project.root / "cache/build/selection";
    if (!builder.initialize({}))
    {
        return false;
    }
    Guid walk_guid;
    ResourceCatalog catalog;
    ByteBuffer payload;
    const std::vector<std::string> model_only{"model"};
    const std::vector<std::string> model_walk{"model", "walk"};
    const std::vector<std::string> walk_only{"walk-files"};
    if (!expect(builder.build(project.root, manifest, model_only), builder.last_error()) ||
        !expect(ResourceSystem::read_catalog(output, catalog) &&
                    catalog.data().entries.size() == 3 && catalog.find(model_guid) &&
                    catalog.find(mesh_guid) && catalog.find(skeleton_guid) &&
                    ResourceSystem::read_resource(catalog, {{"content", output}}, model_guid,
                                                  payload) &&
                    schema::GetModelData(payload.data())->schema_version() == 2,
                "model-only package contains and reads only Model, Mesh and Skeleton"))
    {
        return false;
    }
    if (!expect(builder.build(project.root, manifest, model_walk), builder.last_error()) ||
        !expect(
            ResourceSystem::read_catalog(output, catalog) && catalog.data().entries.size() == 4 &&
                catalog.identities().path_to_guid("project/models/locomotions.fbx/animations/walk",
                                                  walk_guid) &&
                catalog.find(model_guid) && catalog.find(mesh_guid) &&
                catalog.find(skeleton_guid) && catalog.find(walk_guid) &&
                catalog.find(walk_guid)->location->path.starts_with("packs/walk-") &&
                ResourceSystem::read_resource(catalog, {{"content", output}}, walk_guid, payload) &&
                schema::GetAnimationData(payload.data())->skeleton_guid()->str() == skeleton_guid,
            "explicit walk selection adds one independently readable GUID action"))
    {
        return false;
    }
    for (const auto* excluded : {"project/models/locomotions.fbx/animations/run",
                                 "project/models/locomotions.fbx/animations/jog",
                                 "project/models/skeleton.fbx/animations/Take 001",
                                 "project/models/locomotions.fbx", "project/untouched.txt"})
    {
        Guid guid;
        if (!expect(!catalog.identities().path_to_guid(excluded, guid),
                    std::string("unselected source sibling is absent from the runtime catalog: ") +
                        excluded))
        {
            return false;
        }
    }
    const auto revision = builder.last_build().revision;
    if (!expect(builder.build(project.root, manifest, model_walk) &&
                    builder.last_build().revision == revision &&
                    builder.last_build().reused_package_count == 2,
                "unchanged selected entities reuse their package incrementally") ||
        !expect(builder.build(project.root, manifest, walk_only), builder.last_error()) ||
        !expect(
            ResourceSystem::read_catalog(output, catalog) && catalog.data().entries.size() == 2 &&
                catalog.find(walk_guid) && catalog.find(skeleton_guid) &&
                !catalog.find(model_guid) && !catalog.find(mesh_guid) &&
                catalog.find(walk_guid)->location->path == "gen/" + walk_guid &&
                catalog.find(skeleton_guid)->location->path == "gen/" + skeleton_guid &&
                ResourceSystem::read_resource(catalog, {{"content", output}}, walk_guid, payload),
            "walk-only build consumes only the action and skeleton, without a Model root"))
    {
        return false;
    }
    return true;
}
#endif
} // namespace

int main()
{
    std::error_code code;
    Project project;
    project.root = fs::temp_directory_path(code) /
                   ("engine-fbx-pipeline-" +
                    std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    if (code)
    {
        return 1;
    }
    for (const auto* directory : {"assets/models", "settings", "cache"})
    {
        fs::create_directories(project.root / directory, code);
        if (code)
        {
            return 1;
        }
    }
    for (const auto* filename : {"skeleton.fbx", "locomotions.fbx"})
    {
        fs::copy_file(fs::path(ENGINE_FBX_TEST_DATA_DIR) / filename,
                      project.root / "assets/models" / filename, code);
        if (code)
        {
            return 1;
        }
    }
    const auto skeleton_meta = project.root / "assets/models/skeleton.fbx.meta";
    const auto locomotion_meta = project.root / "assets/models/locomotions.fbx.meta";
    if (!write(skeleton_meta, "guid = \"b0ba0001-0000-4000-8000-000000000001\"\nimporter = "
                              "\"fbx\"\nmeta_version = 1\nsample_rate = 30\n") ||
        !write(locomotion_meta,
               "guid = \"b0ba0001-0000-4000-8000-000000000002\"\nimporter = \"fbx\"\nmeta_version "
               "= 1\nsample_rate = 30\nskeleton = \"project/models/skeleton.fbx/skeleton\"\n") ||
        !write(project.root / "assets/untouched.txt", "independent asset"))
    {
        return 1;
    }
    editor::AssetPipelineSystem pipeline;
    std::unique_ptr<editor::asset_pipeline::AssetImporter> importer =
        std::make_unique<plugins::fbx_import::FbxImporter>();
    if (!expect(!pipeline.add_importer(importer) && importer,
                "uninitialized pipeline preserves ownership") ||
        !expect(pipeline.initialize({}), "initialize pipeline") ||
        !expect(pipeline.add_importer(importer) && !importer, "register optional FBX importer") ||
        !expect(pipeline.build_assets(project.root), pipeline.last_error()))
    {
        return 1;
    }
    ResourceCatalog catalog;
    ByteBuffer bytes;
    if (!expect(ResourceSystem::read_catalog(project.root / "cache", catalog),
                "read runtime catalog") ||
        !expect(read(catalog, project.root, "project/models/skeleton.fbx", bytes),
                "read independent FBX Model resource"))
    {
        return 1;
    }
    flatbuffers::Verifier model_verifier(reinterpret_cast<const std::uint8_t*>(bytes.data()),
                                         bytes.size());
    if (!expect(schema::VerifyModelDataBuffer(model_verifier), "Model resource uses Model schema"))
    {
        return 1;
    }
    const auto* model = schema::GetModelData(bytes.data());
    Guid model_guid;
    Guid mesh_guid;
    for (const auto* node : *model->nodes())
    {
        if (node->mesh_guid() != nullptr && !node->mesh_guid()->str().empty())
        {
            mesh_guid = node->mesh_guid()->str();
            break;
        }
    }
    if (!expect(model->schema_version() == 2 && !mesh_guid.empty() && model->nodes()->size() > 60 &&
                    catalog.identities().path_to_guid("project/models/skeleton.fbx", model_guid) &&
                    catalog.find(model_guid)->dependencies.size() == 2,
                "Model v2 references only its nodes' mesh and default material") ||
        !expect(ResourceSystem::read_resource(catalog, {{"content", project.root / "cache"}},
                                              mesh_guid, bytes),
                "read mesh subresource"))
    {
        return 1;
    }
    const auto* mesh = schema::GetMeshData(bytes.data());
    const Guid skeleton_guid = mesh->skeleton_guid()->str();
    const auto original_vertex_count = mesh->vertices()->size();
    if (!expect(mesh->indices()->size() / 3 == 2811 &&
                    mesh->skin()->size() == mesh->vertices()->size(),
                "real FBX geometry and split-corner skin weights imported") ||
        !expect(!skeleton_guid.empty() && mesh->mesh_to_skeleton()->size() == 16,
                "mesh skin references skeleton and bind transform") ||
        !expect(mean_normal_orientation(*mesh) < -0.8,
                "default mode preserves the sample's original inward normals"))
    {
        return 1;
    }
    for (const auto* influence : *mesh->skin())
    {
        if (!expect(std::abs(influence->w0() + influence->w1() + influence->w2() + influence->w3() -
                             1.0F) < 0.00001F,
                    "four retained influences are normalized"))
        {
            return 1;
        }
    }
    if (!expect(
            read(catalog, project.root, "project/models/locomotions.fbx/animations/walk", bytes),
            "external animation child is independently loadable"))
    {
        return 1;
    }
    const auto* animation = schema::GetAnimationData(bytes.data());
    bool changed_pose = false;
    for (const auto* track : *animation->tracks())
    {
        const auto* keys = track->rotations();
        if (keys->size() > 2)
        {
            const auto& first = keys->Get(0)->value();
            const auto& middle = keys->Get(keys->size() / 2)->value();
            changed_pose = changed_pose || std::abs(first.x() - middle.x()) > 0.0001F ||
                           std::abs(first.y() - middle.y()) > 0.0001F ||
                           std::abs(first.z() - middle.z()) > 0.0001F;
        }
    }
    if (!expect(animation->skeleton_guid()->str() == skeleton_guid && changed_pose &&
                    animation->duration() > 1.0F,
                "walk contains changing sampled poses on the referenced skeleton"))
    {
        return 1;
    }
#if defined(ENGINE_FBX_TEST_GRAPH)
    if (!model_loading(project, catalog, model_guid, mesh_guid, skeleton_guid))
    {
        return 1;
    }
#endif
#if defined(ENGINE_FBX_TEST_BUILD)
    if (!build_selection(pipeline, project, model_guid, mesh_guid, skeleton_guid))
    {
        return 1;
    }
#endif
    const auto revision = pipeline.last_build().revision;
    const auto count = pipeline.last_build().scanned;
    std::string original_meta;
    std::string original_locomotion_meta;
    if (!expect(ResourceSystem::read_text(skeleton_meta, original_meta) &&
                    ResourceSystem::read_text(locomotion_meta, original_locomotion_meta),
                "read generated meta") ||
        !expect(original_meta.find("subresources") != std::string::npos,
                "meta persists child identities") ||
        !expect(pipeline.build_assets(project.root), pipeline.last_error()) ||
        !expect(pipeline.last_build().imported == 0 && pipeline.last_build().reused == count &&
                    pipeline.last_build().revision == revision,
                "unchanged FBX and child resources reuse all outputs without publishing"))
    {
        return 1;
    }
    auto modified = original_locomotion_meta;
    const auto setting = modified.find("sample_rate = 30");
    if (!expect(setting != std::string::npos, "sample rate persists") ||
        !expect(write(locomotion_meta, modified.replace(setting, 16, "sample_rate = 60")),
                "change import settings") ||
        !expect(pipeline.build_assets(project.root), pipeline.last_error()) ||
        !expect(pipeline.last_build().imported == 4 && pipeline.last_build().reused == count - 4,
                "changing animation sample rate rebuilds only its model and three clips"))
    {
        return 1;
    }
    const auto generated_meta = "normals = \"generate\"\n" + original_meta;
    if (!expect(write(skeleton_meta, generated_meta), "choose generated normals") ||
        !expect(pipeline.build_assets(project.root), pipeline.last_error()) ||
        !expect(pipeline.last_build().imported > 0 && pipeline.last_build().reused >= 1,
                "normal settings rebuild affected assets while retaining unrelated cache") ||
        !expect(ResourceSystem::read_catalog(project.root / "cache", catalog) &&
                    ResourceSystem::read_resource(catalog, {{"content", project.root / "cache"}},
                                                  mesh_guid, bytes),
                "normal changes preserve mesh GUID"))
    {
        return 1;
    }
    mesh = schema::GetMeshData(bytes.data());
    if (!expect(mesh->vertices()->size() == original_vertex_count &&
                    mesh->indices()->size() / 3 == 2811 &&
                    mesh->skin()->size() == original_vertex_count &&
                    mesh->skeleton_guid()->str() == skeleton_guid &&
                    mean_normal_orientation(*mesh) > 0.8,
                "generated normals face outward without changing mesh or skin identity"))
    {
        return 1;
    }
    const auto normal_revision = pipeline.last_build().revision;
    struct InvalidSetting
    {
        const char* key;
        const char* value;
    };
    constexpr InvalidSetting invalid_settings[] = {
        {"normals", "\"unknown\""}, {"normals", "true"}, {"sample_rate", "\"60\""},
        {"sample_rate", "true"},    {"skeleton", "123"}, {"skeleton", "false"}};
    for (const auto& invalid_setting : invalid_settings)
    {
        const auto key = std::string(invalid_setting.key) + " = ";
        auto invalid_meta = generated_meta;
        const auto begin = invalid_meta.find(key);
        const auto replacement = key + invalid_setting.value;
        if (begin == std::string::npos)
        {
            invalid_meta.insert(0, replacement + "\n");
        }
        else
        {
            const auto end = invalid_meta.find('\n', begin);
            invalid_meta.replace(begin, end - begin, replacement);
        }
        if (!expect(write(skeleton_meta, invalid_meta), "prepare invalid FBX setting") ||
            !expect(!pipeline.build_assets(project.root),
                    "unknown or incorrectly typed FBX settings fail") ||
            !expect(pipeline.last_build().revision == normal_revision &&
                        ResourceSystem::read_catalog(project.root / "cache", catalog) &&
                        ResourceSystem::read_resource(
                            catalog, {{"content", project.root / "cache"}}, mesh_guid, bytes) &&
                        mean_normal_orientation(*schema::GetMeshData(bytes.data())) > 0.8,
                    "invalid FBX settings preserve the last usable mesh"))
        {
            return 1;
        }
    }
    if (!expect(write(skeleton_meta, generated_meta) && pipeline.build_assets(project.root),
                "restore valid normals setting") ||
        !expect(pipeline.last_build().imported == 0 && pipeline.last_build().reused == count &&
                    pipeline.last_build().revision == normal_revision,
                "unchanged generated normals reuse the complete previous cache"))
    {
        return 1;
    }
    const auto stable_revision = pipeline.last_build().revision;
    // 重复源身份必须失败；完整运行目录仍保留上一个成功版本。
    if (!expect(
            write(project.root / "assets/copy.txt.meta",
                  "guid = \"" + skeleton_guid + "\"\nimporter = \"source\"\nmeta_version = 1\n") &&
                write(project.root / "assets/copy.txt", "duplicate child GUID"),
            "prepare identity conflict") ||
        !expect(!pipeline.build_assets(project.root),
                "source GUID cannot reuse another source's child GUID") ||
        !expect(pipeline.last_build().revision == stable_revision,
                "failed batch preserves successful stats") ||
        !expect(ResourceSystem::read_catalog(project.root / "cache", catalog),
                "failed batch preserves catalog") ||
        !expect(
            read(catalog, project.root, "project/models/locomotions.fbx/animations/walk", bytes),
            "previous resource remains loadable after failed batch"))
    {
        return 1;
    }
    std::printf("FBX object bundle: %zu resources, stable GUIDs, true sampled motion, incremental "
                "rebuild and conflict rollback passed\n",
                count);
    return 0;
}
