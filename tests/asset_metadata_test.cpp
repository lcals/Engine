#include "editor/asset_pipeline/asset_io.hpp"
#include "editor/asset_pipeline/asset_metadata.hpp"
#include "editor/asset_pipeline/asset_pipeline_system.hpp"
#include "runtime/engine_config.hpp"
#include "runtime/resource/resource_system.hpp"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

namespace
{
namespace fs = std::filesystem;
bool expect(bool condition, const char* description, const std::string& error)
{
    if (!condition)
    {
        std::fprintf(stderr, "%s: %s\n", description, error.c_str());
    }
    return condition;
}
struct Project
{
    fs::path root;
    ~Project()
    {
        std::error_code error;
        fs::remove_all(root, error);
    }
};
} // namespace

int main()
{
    using namespace engine;
    using namespace engine::asset_pipeline;
    std::error_code code;
    Project project{fs::temp_directory_path(code) /
                    ("engine-asset-meta-" +
                     std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))};
    for (const char* part : {"assets", "cache", "settings"})
    {
        fs::create_directories(project.root / part, code);
        if (code)
        {
            return 1;
        }
    }
    const auto source = project.root / "assets/a.txt";
    std::string error;
    std::string first;
    std::string second;
    if (!expect(write_text(source, "one", error), "create source", error) ||
        !expect(ensure_asset_guid(project.root, source, "source", first, error),
                "create source identity", error) ||
        !expect(ensure_asset_guid(project.root, source, "source", second, error) && first == second,
                "retain existing identity", error) ||
        !expect(!ensure_asset_guid(project.root, source, "mesh", second, error) && second.empty(),
                "reject importer mismatch", error))
    {
        return 1;
    }
    const auto duplicate = project.root / "assets/b.txt";
    std::string metadata;
    if (!write_text(duplicate, "two", error) ||
        !ResourceSystem::read_text(source.string() + ".meta", metadata) ||
        !write_text(duplicate.string() + ".meta", metadata, error) ||
        !expect(!ensure_asset_guid(project.root, duplicate, "source", second, error) &&
                    second.empty(),
                "reject duplicate GUID without repairing it", error))
    {
        return 1;
    }
    fs::remove(duplicate.string() + ".meta", code);
    if (!ensure_asset_guid(project.root, duplicate, "source", second, error) || first == second)
    {
        return 1;
    }
    AssetPipelineSystem pipeline;
    if (!expect(pipeline.initialize({}) && pipeline.build_assets(project.root),
                "import shared metadata", pipeline.last_error()))
    {
        return 1;
    }
    fs::remove(source.string() + ".meta", code);
    if (!expect(!ensure_asset_guid(project.root, source, "source", second, error) && second.empty(),
                "known source cannot silently receive a new identity", error))
    {
        return 1;
    }
    const auto outside = project.root / "outside.txt";
    if (!write_text(outside, "outside", error) ||
        !expect(!ensure_asset_guid(project.root, outside, "source", second, error),
                "reject source outside assets", error))
    {
        return 1;
    }
    pipeline.shutdown();
    return 0;
}
