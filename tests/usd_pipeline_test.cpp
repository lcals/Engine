#include "editor/asset_pipeline/asset_pipeline_system.hpp"
#include "plugins/editor/fbx_import/fbx_importer.hpp"
#include "plugins/editor/usd_import/usd_importer.hpp"
#include "runtime/engine_config.hpp"
#include "runtime/resource/resource_system.hpp"
#include "runtime/schema/mesh_generated.h"
#include "runtime/schema/model_generated.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

namespace
{
namespace fs = std::filesystem;
using namespace engine;

bool expect(bool condition, const std::string& message)
{
    if (!condition)
    {
        std::fprintf(stderr, "USD pipeline: %s\n", message.c_str());
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

std::string model_source(int translation)
{
    return "#usda 1.0\n(\n    metersPerUnit = 1\n    upAxis = \"Y\"\n)\n"
           "def Xform \"Root\"\n{\n    double3 xformOp:translate = (" +
           std::to_string(translation) +
           ", 0, 0)\n    uniform token[] xformOpOrder = [\"xformOp:translate\"]\n"
           "    def Mesh \"Quad\"\n    {\n"
           "        uniform token subdivisionScheme = \"none\"\n"
           "        int[] faceVertexCounts = [4]\n"
           "        int[] faceVertexIndices = [0, 1, 2, 3]\n"
           "        point3f[] points = [(0,0,0), (1,0,0), (1,1,0), (0,1,0)]\n"
           "    }\n}\n";
}

bool read(const ResourceCatalog& catalog, const fs::path& root, const std::string& path,
          ByteBuffer& bytes)
{
    Guid guid;
    return catalog.identities().path_to_guid(path, guid) &&
           ResourceSystem::read_resource(catalog, {{"content", root / "cache"}}, guid, bytes);
}
} // namespace

int main()
{
    std::error_code code;
    Project project;
    project.root = fs::temp_directory_path(code) /
                   ("engine-usd-pipeline-" +
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
    fs::copy_file(fs::path(ENGINE_FBX_TEST_DATA_DIR) / "skeleton.fbx",
                  project.root / "assets/models/person.fbx", code);
    const auto source = project.root / "assets/models/quad.usda";
    if (code || !write(source, model_source(2)))
    {
        return 1;
    }
    editor::AssetPipelineSystem pipeline;
    std::unique_ptr<editor::asset_pipeline::AssetImporter> fbx =
        std::make_unique<plugins::fbx_import::FbxImporter>();
    std::unique_ptr<editor::asset_pipeline::AssetImporter> usd =
        std::make_unique<plugins::usd_import::UsdImporter>();
    if (!expect(pipeline.initialize({}), "initialize pipeline") ||
        !expect(pipeline.add_importer(fbx) && pipeline.add_importer(usd),
                "register FBX and USD simultaneously despite both producing model assets") ||
        !expect(pipeline.build_assets(project.root), pipeline.last_error()))
    {
        return 1;
    }
    ResourceCatalog catalog;
    ByteBuffer bytes;
    Guid original_mesh_guid;
    if (!expect(ResourceSystem::read_catalog(project.root / "cache", catalog), "read catalog") ||
        !expect(read(catalog, project.root, "project/models/person.fbx", bytes),
                "FBX format remains independently routed") ||
        !expect(read(catalog, project.root, "project/models/quad.usda", bytes), "read USD model"))
    {
        return 1;
    }
    const auto* model = schema::GetModelData(bytes.data());
    if (!expect(model->schema_version() == 2 && model->nodes()->size() == 2 &&
                    model->nodes()->Get(1)->parent() == 0,
                "USD hierarchy retained in independent Model v2") ||
        !expect(catalog.identities().path_to_guid("project/models/quad.usda/meshes/Root/Quad",
                                                  original_mesh_guid),
                "USD child uses its stable prim path") ||
        !expect(read(catalog, project.root, "project/models/quad.usda/meshes/Root/Quad", bytes),
                "load USD mesh independently"))
    {
        return 1;
    }
    const auto* mesh = schema::GetMeshData(bytes.data());
    if (!expect(mesh->vertices()->size() == 4 && mesh->indices()->size() == 6 &&
                    std::abs(mesh->vertices()->Get(0)->x() - 2.0F) < 0.00001F,
                "convex polygon triangulated with world transform baked"))
    {
        return 1;
    }
    const auto count = pipeline.last_build().scanned;
    const auto first_revision = pipeline.last_build().revision;
    if (!expect(pipeline.build_assets(project.root), pipeline.last_error()) ||
        !expect(pipeline.last_build().imported == 0 && pipeline.last_build().reused == count &&
                    pipeline.last_build().revision == first_revision,
                "both source formats reuse unchanged cached resources") ||
        !expect(write(source, model_source(3)), "update source transform") ||
        !expect(pipeline.build_assets(project.root), pipeline.last_error()) ||
        !expect(pipeline.last_build().imported == 2 && pipeline.last_build().reused == count - 2,
                "USD change rebuilds its model/mesh without rebuilding FBX") ||
        !expect(ResourceSystem::read_catalog(project.root / "cache", catalog) &&
                    read(catalog, project.root, "project/models/quad.usda/meshes/Root/Quad", bytes),
                "read incremental mesh"))
    {
        return 1;
    }
    mesh = schema::GetMeshData(bytes.data());
    Guid next_mesh_guid;
    if (!expect(std::abs(mesh->vertices()->Get(0)->x() - 3.0F) < 0.00001F &&
                    catalog.identities().path_to_guid("project/models/quad.usda/meshes/Root/Quad",
                                                      next_mesh_guid) &&
                    next_mesh_guid == original_mesh_guid,
                "new USD contents replace cache while preserving child identity"))
    {
        return 1;
    }
    const auto stable_revision = pipeline.last_build().revision;
    auto external = model_source(3);
    external.insert(external.find("    metersPerUnit"), "    subLayers = [@missing.usda@]\n");
    if (!expect(write(source, external) && !pipeline.build_assets(project.root),
                "even missing external layers are rejected instead of producing partial output") ||
        !expect(pipeline.last_error().find("external") != std::string::npos,
                "failure identifies untracked external dependencies"))
    {
        return 1;
    }
    const auto unknown =
        model_source(3) + "def Sphere \"Unsupported\"\n{\n    double radius = 1\n}\n";
    if (!expect(write(source, unknown) && !pipeline.build_assets(project.root),
                "unsupported primitive cannot silently disappear from an otherwise valid model") ||
        !expect(pipeline.last_error().find("Unsupported USD primitive") != std::string::npos,
                "unknown primitive error explains the unsupported source type"))
    {
        return 1;
    }
    auto varying = model_source(3);
    varying.insert(varying.find("        point3f[] points ="),
                   "        point3f[] points.timeSamples = { 0: [(0,0,0), (1,0,0), (1,1,0), "
                   "(0,1,0)], 1: [(0,0,1), (1,0,1), (1,1,1), (0,1,1)] }\n");
    if (!expect(write(source, varying) && !pipeline.build_assets(project.root),
                "time-varying geometry is not silently frozen") ||
        !expect(write(source, "not a USD document") && !pipeline.build_assets(project.root),
                "malformed USD fails through return status") ||
        !expect(pipeline.last_build().revision == stable_revision,
                "failed batches preserve the last successful revision") ||
        !expect(ResourceSystem::read_catalog(project.root / "cache", catalog) &&
                    read(catalog, project.root, "project/models/quad.usda/meshes/Root/Quad", bytes),
                "previous runtime resources remain available after each rejected input") ||
        !expect(write(source, model_source(3)) && pipeline.build_assets(project.root),
                "restoring the source recovers the pipeline") ||
        !expect(pipeline.last_build().imported == 0 &&
                    pipeline.last_build().revision == stable_revision,
                "restored unchanged source reuses the last published version"))
    {
        return 1;
    }
    std::printf(
        "USD/FBX coexistence, incremental child resources and unsupported-input rollback passed\n");
    return 0;
}
