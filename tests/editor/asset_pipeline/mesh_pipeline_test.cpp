#include "editor/asset_pipeline/asset_pipeline_system.hpp"
#include "file_text.hpp"
#include "runtime/engine_config.hpp"
#include "runtime/resource/resource_system.hpp"
#include "runtime/schema/mesh_generated.h"
#include "runtime/task/task_system.hpp"
#include "runtime/task/thread_system.hpp"

#include <flatbuffers/verifier.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>

namespace
{
using namespace engine;

bool expect(bool condition, const char* message)
{
    if (!condition)
    {
        std::fprintf(stderr, "mesh pipeline: %s\n", message);
    }
    return condition;
}

bool write(const std::filesystem::path& path, std::string_view content)
{
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write(content.data(), static_cast<std::streamsize>(content.size()));
    stream.close();
    return static_cast<bool>(stream);
}

std::string read(const std::filesystem::path& path)
{
    std::string text;
    return test_support::read_text(path, text) ? text : std::string{};
}

std::string meta(std::string_view guid, std::string_view importer)
{
    return "guid = \"" + std::string(guid) + "\"\nimporter = \"" + std::string(importer) +
           "\"\nmeta_version = 1\n";
}

struct Fixture
{
    std::filesystem::path root;

    bool initialize()
    {
        std::error_code error;
        const auto temporary = std::filesystem::temp_directory_path(error);
        if (error)
        {
            return false;
        }
        root = temporary /
               ("engine-mesh-test-" +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        for (const auto* directory : {"assets", "cache", "settings"})
        {
            std::filesystem::create_directories(root / directory, error);
            if (error)
            {
                return false;
            }
        }
        return write(root / "settings/editor-import.toml", "[common]\nimport_threads = 8\n");
    }

    ~Fixture() noexcept
    {
        std::error_code error;
        if (!root.empty())
        {
            std::filesystem::remove_all(root, error);
        }
    }
};

bool build(editor::AssetPipelineSystem& pipeline, const Fixture& fixture)
{
    if (!pipeline.build_assets(fixture.root))
    {
        std::fprintf(stderr, "mesh pipeline import: %s\n", pipeline.last_error().c_str());
        return false;
    }
    return true;
}

bool mesh_tangent_tests()
{
    Fixture fixture;
    ThreadSystem threads;
    TaskSystem tasks(threads, {"Editor", 4, TaskPriority::low});
    editor::AssetPipelineSystem pipeline(tasks);
    if (!fixture.initialize() || !threads.initialize({}) || !tasks.initialize({}) ||
        !pipeline.initialize({}))
    {
        return false;
    }
    constexpr auto generated_guid = "b0000000-0000-4000-8000-000000000011";
    constexpr auto authored_guid = "b0000000-0000-4000-8000-000000000012";
    constexpr auto no_uv_guid = "b0000000-0000-4000-8000-000000000013";
    constexpr auto thin_guid = "b0000000-0000-4000-8000-000000000014";
    const std::string geometry =
        "vertices=[[0,0,0,0,0,2],[1,0,0,0,0,2],[1,1,0,0,0,2],[0,1,0,0,0,2],"
        "[2,0,0,0,0,2],[3,0,0,0,0,2],[2,1,0,0,0,2]]\n"
        "indices=[0,1,2,0,2,3,4,5,6]\n";
    const std::string attributes =
        "texcoords=[[0,0],[1,1],[0,1],[1,0],[0,0],[1,0],[0,1]]\n"
        "lightmap_texcoords=[[0,0],[1,0],[1,1],[0,1],[0,0],[1,0],[0,1]]\n"
        "colors=[[0,0,0,1],[0.1,0,0,1],[0.2,0,0,1],[0.3,0,0,1],"
        "[0.4,0,0,1],[0.5,0,0,1],[0.6,0,0,1]]\n";
    const std::string parts = "[[submeshes]]\nfirst_index=0\nindex_count=6\nmaterial_slot=0\n"
                              "[[submeshes]]\nfirst_index=6\nindex_count=3\nmaterial_slot=1\n";
    const std::string authored = "tangents=[[0,1,0,-1],[0,1,0,-1],[0,1,0,-1],[0,1,0,-1],"
                                 "[0,1,0,-1],[0,1,0,-1],[0,1,0,-1]]\n";
    // The long, narrow triangle is nondegenerate in both position and UV.
    // Its last projected corner angle rounds to acos(1) in single precision.
    const std::string thin =
        "vertices=[[-0.034890,-0.036223,6.794890,-0.423151,-0.906054,0.003078],"
        "[-0.038723,-0.032390,6.794890,-0.863605,-0.504166,0.001483],"
        "[-0.034890,-0.036223,-6.794890,-0.578083,-0.815975,-0.002096]]\n"
        "indices=[0,1,2]\n"
        "texcoords=[[3.397445,-0.018111],[3.397445,-0.016195],"
        "[-3.397445,-0.018111]]\n";
    const auto write_asset =
        [&](std::string_view name, std::string_view guid, const std::string& content)
    {
        const auto path = fixture.root / "assets" / name;
        return write(path, content) && write(path.string() + ".meta", meta(guid, "mesh"));
    };
    if (!expect(write_asset("generated.mesh", generated_guid, geometry + attributes + parts) &&
                    write_asset("authored.mesh", authored_guid,
                                geometry + attributes + authored + parts) &&
                    write_asset("no-uv.mesh", no_uv_guid, geometry) &&
                    write_asset("thin.mesh", thin_guid, thin) && build(pipeline, fixture),
                "import generated, authored, UV-free and thin mesh tangent cases"))
    {
        return false;
    }
    ResourceCatalog catalog;
    const auto load_mesh = [&](std::string_view guid, schema::MeshDataT& output)
    {
        ByteBuffer payload;
        if (!ResourceSystem::read_catalog(fixture.root / "cache", catalog) ||
            !ResourceSystem::read_resource(catalog, {{"content", fixture.root / "cache"}}, guid,
                                           payload))
        {
            return false;
        }
        flatbuffers::Verifier verifier(reinterpret_cast<const std::uint8_t*>(payload.data()),
                                       payload.size());
        if (!schema::VerifyMeshDataBuffer(verifier))
        {
            return false;
        }
        schema::GetMeshData(payload.data())->UnPackTo(&output);
        return true;
    };
    schema::MeshDataT result;
    if (!expect(load_mesh(generated_guid, result) && result.vertices.size() > 7 &&
                    result.tangents.size() == result.vertices.size() &&
                    result.texcoords.size() == result.vertices.size() &&
                    result.lightmap_texcoords.size() == result.vertices.size() &&
                    result.colors.size() == result.vertices.size() && result.indices.size() == 9 &&
                    result.submeshes.size() == 2 && result.submeshes[1].first_index() == 6 &&
                    result.submeshes[1].material_slot() == 1,
                "MikkTSpace splits shared mirrored vertices and retains every mesh attribute"))
    {
        return false;
    }
    bool positive = false;
    bool negative = false;
    for (std::size_t i = 0; i < 6; ++i)
    {
        const auto& tangent = result.tangents[result.indices[i]];
        positive |= tangent.w() == 1;
        negative |= tangent.w() == -1;
    }
    for (std::size_t i = 0; i < result.vertices.size(); ++i)
    {
        const auto& vertex = result.vertices[i];
        std::size_t original = 0;
        while (original < 7 && (result.vertices[original].x() != vertex.x() ||
                                result.vertices[original].y() != vertex.y()))
        {
            ++original;
        }
        if (!expect(original < 7 && result.texcoords[i] == result.texcoords[original] &&
                        result.lightmap_texcoords[i] == result.lightmap_texcoords[original] &&
                        result.colors[i] == result.colors[original],
                    "split vertex attributes remain attached to their source vertex"))
        {
            return false;
        }
    }
    if (!expect(positive && negative && load_mesh(authored_guid, result) &&
                    result.vertices.size() == 7 && result.tangents[0].y() == 1 &&
                    result.tangents[0].w() == -1,
                "mirrored UVs retain both hands and authored tangents are preserved") ||
        !expect(load_mesh(no_uv_guid, result) && result.tangents.empty(),
                "mesh without UVs does not fabricate texture tangents"))
    {
        return false;
    }
    if (!expect(load_mesh(thin_guid, result) && result.tangents.size() == 3 &&
                    std::abs(result.tangents[2].z()) > 0.99 &&
                    std::isfinite(result.tangents[2].x()) &&
                    (result.tangents[2].w() == -1 || result.tangents[2].w() == 1),
                "thin triangle retains its UV-derived MikkTSpace tangent at the small corner"))
    {
        return false;
    }
    const auto stable_catalog = read(fixture.root / "cache/catalog.bin");
    return expect(write(fixture.root / "assets/generated.mesh",
                        "vertices=[[0,0,0,0,0,0]]\nindices=[0,0,0]\n") &&
                      !pipeline.build_assets(fixture.root) &&
                      read(fixture.root / "cache/catalog.bin") == stable_catalog,
                  "invalid mesh import leaves the previous published catalog intact");
}

} // namespace

int main()
{
    return mesh_tangent_tests() ? 0 : 1;
}
