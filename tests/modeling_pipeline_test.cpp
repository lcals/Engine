#include "editor/asset_pipeline/asset_pipeline_system.hpp"
#include "plugins/editor/modeling/editable_mesh.hpp"
#include "plugins/editor/modeling/mesh_primitives.hpp"
#include "plugins/editor/modeling/modeling_mesh_importer.hpp"
#include "runtime/engine_config.hpp"
#include "runtime/resource/resource_system.hpp"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace
{
namespace fs = std::filesystem;

bool expect(bool condition, const std::string& message)
{
    if (!condition)
    {
        std::fprintf(stderr, "modeling pipeline: %s\n", message.c_str());
    }
    return condition;
}

struct Project
{
    fs::path root;
    ~Project()
    {
        std::error_code error;
        if (!root.empty())
        {
            fs::remove_all(root, error);
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

class CheckedMeshImporter final : public engine::editor::asset_pipeline::MeshImporter
{
public:
    CheckedMeshImporter(engine::editor::AssetPipelineSystem& pipeline, bool& rejected) noexcept
        : pipeline_(pipeline), rejected_(rejected)
    {
    }

    [[nodiscard]] bool inspect_batch(const engine::editor::asset_pipeline::ImportContext& context,
                                     std::string& fingerprint, std::string& error) override
    {
        std::thread concurrent(
            [this]
            {
                std::unique_ptr<engine::editor::asset_pipeline::AssetImporter> replacement =
                    std::make_unique<MeshImporter>();
                rejected_ = !pipeline_.set_importer(replacement) && replacement != nullptr;
            });
        concurrent.join();
        return MeshImporter::inspect_batch(context, fingerprint, error);
    }

private:
    engine::editor::AssetPipelineSystem& pipeline_;
    bool& rejected_;
};
} // namespace

int main()
{
    using namespace engine;
    using namespace engine::editor;
    using namespace engine::plugins::modeling;
    std::error_code filesystem_error;
    Project project;
    project.root = fs::temp_directory_path(filesystem_error);
    if (filesystem_error)
    {
        return 1;
    }
    project.root /= "engine-modeling-pipeline-" +
                    std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    for (const char* directory : {"assets", "settings", "cache"})
    {
        fs::create_directories(project.root / directory, filesystem_error);
        if (filesystem_error)
        {
            return 1;
        }
    }
    schema::EditableMeshDataT model;
    schema::MeshDataT expected;
    std::vector<std::uint8_t> bytes;
    std::string error;
    if (!expect(MeshPrimitives::create({}, model, error), error) ||
        !expect(EditableMesh::compile(model, "project/cube.mesh", expected, error), error) ||
        !expect(EditableMesh::serialize(model, bytes, error), error))
    {
        return 1;
    }
    constexpr std::string_view digits = "0123456789abcdef";
    std::string hex;
    for (std::uint8_t value : bytes)
    {
        hex += digits[value >> 4];
        hex += digits[value & 15];
    }
    // 渲染数组故意过期；导入必须以可编辑模型为权威重新编译。
    const std::string source = "vertices = []\nindices = []\n[modeling]\n"
                               "format = \"flatbuffers-hex\"\ndata = \"" +
                               hex + "\"\n";
    const fs::path path = project.root / "assets/cube.mesh";
    AssetPipelineSystem pipeline;
    std::unique_ptr<asset_pipeline::AssetImporter> importer =
        std::make_unique<ModelingMeshImporter>();
    if (!expect(write(path, source), "write source") ||
        !expect(!pipeline.set_importer(importer) && importer,
                "uninitialized pipeline rejects registration without consuming ownership") ||
        !expect(pipeline.initialize({}), "initialize pipeline") ||
        !expect(pipeline.set_importer(importer) && !importer, "host injects modeling importer") ||
        !expect(pipeline.build_assets(project.root), pipeline.last_error()))
    {
        return 1;
    }
    const auto imported = pipeline.last_build().imported;
    ResourceCatalog catalog;
    ByteBuffer payload;
    std::string guid;
    if (!expect(ResourceSystem::read_catalog(project.root / "cache", catalog), "read catalog") ||
        !expect(catalog.identities().path_to_guid("project/cube.mesh", guid), "find source GUID") ||
        !expect(ResourceSystem::read_resource(catalog, {{"content", project.root / "cache"}}, guid,
                                              payload),
                "read compiled mesh"))
    {
        return 1;
    }
    flatbuffers::Verifier verifier(reinterpret_cast<const std::uint8_t*>(payload.data()),
                                   payload.size());
    if (!expect(schema::VerifyMeshDataBuffer(verifier), "compiled mesh schema"))
    {
        return 1;
    }
    const auto* mesh = schema::GetMeshData(payload.data());
    if (!expect(mesh->vertices()->size() == expected.vertices.size() &&
                    mesh->indices()->size() == expected.indices.size(),
                "compiled geometry must come from editable model") ||
        !expect(imported > 0 && fs::is_regular_file(project.root / "assets/cube.mesh.meta"),
                "source receives unique adjacent meta") ||
        !expect(pipeline.build_assets(project.root), pipeline.last_error()) ||
        !expect(pipeline.last_build().imported == 0, "unchanged model reuses cache"))
    {
        return 1;
    }
    const auto revision = pipeline.last_build().revision;
    importer = std::make_unique<asset_pipeline::MeshImporter>();
    if (!expect(pipeline.set_importer(importer) && !importer,
                "host restores ordinary mesh importer") ||
        !expect(
            !pipeline.build_assets(project.root),
            "removing modeling importer rejects authoritative source instead of reusing cache") ||
        !expect(pipeline.last_build().revision == revision,
                "missing plugin preserves published version"))
    {
        return 1;
    }
    importer = std::make_unique<ModelingMeshImporter>();
    if (!expect(pipeline.set_importer(importer), "host restores modeling importer") ||
        !expect(pipeline.build_assets(project.root), pipeline.last_error()) ||
        !expect(pipeline.last_build().imported == 0,
                "restored plugin reuses matching authoritative cache"))
    {
        return 1;
    }
    const std::string invalid = "[modeling]\nformat = \"flatbuffers-hex\"\ndata = \"0Z\"\n";
    if (!expect(write(path, invalid), "write invalid source") ||
        !expect(!pipeline.build_assets(project.root), "reject malformed editable data") ||
        !expect(pipeline.last_build().revision == revision,
                "failed import keeps published version"))
    {
        return 1;
    }
    const std::string ordinary = "vertices = [[0,0,0,0,0,1],[1,0,0,0,0,1],[0,1,0,0,0,1]]\n"
                                 "indices = [0,1,2]\n";
    if (!expect(write(path, ordinary), "write ordinary mesh") ||
        !expect(pipeline.build_assets(project.root), pipeline.last_error()) ||
        !expect(pipeline.last_build().imported == 1,
                "modeling plugin delegates ordinary mesh to editor importer"))
    {
        return 1;
    }
    pipeline.shutdown();
    if (!expect(pipeline.initialize({}), "pipeline reinitializes with default importers") ||
        !expect(pipeline.build_assets(project.root), pipeline.last_error()) ||
        !expect(pipeline.last_build().imported == 1,
                "ordinary mesh remains importable without modeling plugin; type key changes") ||
        !expect(pipeline.build_assets(project.root), pipeline.last_error()) ||
        !expect(pipeline.last_build().imported == 0, "ordinary mesh cache remains incremental"))
    {
        return 1;
    }
    bool concurrent_rejected = false;
    importer = std::make_unique<CheckedMeshImporter>(pipeline, concurrent_rejected);
    if (!expect(pipeline.set_importer(importer), "install registration concurrency probe") ||
        !expect(pipeline.build_assets(project.root), pipeline.last_error()) ||
        !expect(concurrent_rejected, "in-flight build rejects concurrent importer replacement"))
    {
        return 1;
    }
    pipeline.shutdown();
    return 0;
}
