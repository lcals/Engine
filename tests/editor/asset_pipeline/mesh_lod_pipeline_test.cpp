#include "editor/asset_pipeline/asset_pipeline_system.hpp"
#include "editor/asset_pipeline/mesh_lod_commands.hpp"
#include "editor/commands/editor_commands.hpp"
#include "runtime/engine_config.hpp"
#include "runtime/resource/resource_system.hpp"
#include "runtime/task/task_system.hpp"
#include "runtime/task/thread_system.hpp"

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>

namespace
{
namespace fs = std::filesystem;
using Json = nlohmann::json;

bool expect(bool value, const char* message)
{
    if (!value)
    {
        std::fprintf(stderr, "LOD import commands: %s\n", message);
    }
    return value;
}

bool write(const fs::path& path, const std::string& text)
{
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream << text;
    stream.close();
    return static_cast<bool>(stream);
}

struct Fixture
{
    fs::path root;
    bool initialize()
    {
        std::error_code error;
        const auto temporary = fs::temp_directory_path(error);
        if (error)
        {
            return false;
        }
        root = temporary /
               ("engine-lod-pipeline-" +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        for (const auto* directory : {"assets", "cache", "settings"})
        {
            fs::create_directories(root / directory, error);
            if (error)
            {
                return false;
            }
        }
        return write(root / "settings/editor-import.toml", "[common]\nimport_threads = 8\n");
    }
    ~Fixture()
    {
        std::error_code error;
        if (!root.empty())
        {
            fs::remove_all(root, error);
        }
    }
};

std::string read(const fs::path& path)
{
    std::string text;
    return engine::ResourceSystem::read_text(path, text) ? text : std::string{};
}

std::string grid()
{
    std::ostringstream text;
    text << "vertices=[";
    constexpr std::uint32_t width = 18;
    for (std::uint32_t y = 0; y <= width; ++y)
    {
        for (std::uint32_t x = 0; x <= width; ++x)
        {
            text << (x || y ? "," : "") << '[' << x << ',' << y << ",0,0,0,1]";
        }
    }
    text << "]\nindices=[";
    bool first = true;
    for (std::uint32_t y = 0; y < width; ++y)
    {
        for (std::uint32_t x = 0; x < width; ++x)
        {
            const auto vertex = y * (width + 1) + x;
            for (const auto index : {vertex, vertex + 1, vertex + width + 2, vertex,
                                     vertex + width + 2, vertex + width + 1})
            {
                text << (first ? "" : ",") << index;
                first = false;
            }
        }
    }
    text << "]\n";
    return text.str();
}

bool tests()
{
    using namespace engine;
    Fixture fixture;
    ThreadSystem threads;
    TaskSystem tasks(threads, {"Editor", 4, TaskPriority::low});
    editor::AssetPipelineSystem pipeline(tasks);
    editor::EditorCommands commands([]() { return std::uint64_t{0}; });
    if (!fixture.initialize() || !threads.initialize({}) || !tasks.initialize({}) ||
        !pipeline.initialize({}) ||
        !editor::register_mesh_lod_commands(commands, pipeline, fixture.root))
    {
        return false;
    }
    constexpr auto guid = "b0000000-0000-4000-8000-000000000091";
    const auto source = fixture.root / "assets/grid.mesh";
    const auto meta = fixture.root / "assets/grid.mesh.meta";
    if (!write(source, grid()) ||
        !write(meta,
               "guid=\"" + std::string(guid) +
                   "\"\nimporter=\"mesh\"\nmeta_version=1\nartist_note=\"keep this field\"\n") ||
        !pipeline.build_assets(fixture.root))
    {
        std::fprintf(stderr, "%s\n", pipeline.last_error().c_str());
        return false;
    }
    Json result;
    std::string error;
    const auto run = [&](const char* command, const Json& arguments)
    {
        error.clear();
        const bool success = commands.execute(command, arguments, result, error);
        if (!success)
        {
            std::fprintf(stderr, "%s: %s\n", command, error.c_str());
        }
        return success;
    };
    if (!expect(run("get_mesh_lod_import", {{"path", "grid.mesh"}}) && result["guid"] == guid &&
                    result["meshes"].size() == 1 && result["meshes"][0]["levels"].size() == 1,
                "source query returns stable identity and actual cached LOD0") ||
        !expect(run("set_mesh_lod_import", {{"path", "grid.mesh"}, {"enabled", true}}) &&
                    result["changed"] == true && result["imported"] == true &&
                    result["meshes"][0]["levels"].size() > 1 && result["guid"] == guid &&
                    read(meta).find("keep this field") != std::string::npos,
                "enabling saves meta, imports real levels and preserves GUID and unrelated fields"))
    {
        return false;
    }
    const auto stable_meta = read(meta);
    const auto stable_catalog = read(fixture.root / "cache/catalog.bin");
    if (!expect(run("set_mesh_lod_import", {{"path", "grid.mesh"}, {"enabled", true}}) &&
                    result["changed"] == false && result["imported"] == false &&
                    read(meta) == stable_meta &&
                    read(fixture.root / "cache/catalog.bin") == stable_catalog,
                "same values do not touch metadata or rebuild published resources"))
    {
        return false;
    }
    for (const Json& invalid : {Json{{"path", "../grid.mesh"}, {"enabled", true}},
                                Json{{"path", "grid.mesh"}, {"enabled", 1}},
                                Json{{"path", "grid.mesh"}, {"triangleRatios", {0.4, 0.6}}},
                                Json{{"path", "grid.mesh"}, {"maxRelativeError", 0}},
                                Json{{"path", "grid.mesh"}, {"unexpected", true}}})
    {
        error.clear();
        if (!expect(!commands.execute("set_mesh_lod_import", invalid, result, error) &&
                        !error.empty() && read(meta) == stable_meta &&
                        read(fixture.root / "cache/catalog.bin") == stable_catalog,
                    "bad paths and settings preserve source and published catalog"))
        {
            return false;
        }
    }
    return expect(run("set_mesh_lod_import", {{"path", "grid.mesh"}, {"enabled", false}}) &&
                      result["meshes"][0]["levels"].size() == 1 && result["guid"] == guid,
                  "disabling generation reimports only original topology with the same GUID");
}
} // namespace

int main()
{
    return tests() ? 0 : 1;
}
