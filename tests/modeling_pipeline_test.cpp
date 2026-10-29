#include "editor/asset_pipeline/asset_pipeline_system.hpp"
#include "plugins/modeling/editable_mesh.hpp"
#include "plugins/modeling/mesh_primitives.hpp"
#include "runtime/engine_config.hpp"
#include "runtime/resource/resource_system.hpp"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
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
} // namespace

int main()
{
    using namespace engine;
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
    if (!expect(write(path, source), "write source") ||
        !expect(pipeline.initialize({}), "initialize pipeline") ||
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
    const std::string invalid = "[modeling]\nformat = \"flatbuffers-hex\"\ndata = \"0Z\"\n";
    if (!expect(write(path, invalid), "write invalid source") ||
        !expect(!pipeline.build_assets(project.root), "reject malformed editable data") ||
        !expect(pipeline.last_build().revision == revision,
                "failed import keeps published version"))
    {
        return 1;
    }
    pipeline.shutdown();
    return 0;
}
