#include "editor/asset_pipeline/asset_pipeline_system.hpp"
#include "plugins/editor/json_import/json_importer.hpp"
#include "runtime/engine_config.hpp"
#include "runtime/resource/resource_system.hpp"
#include "runtime/task/task_system.hpp"
#include "runtime/task/thread_system.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>

namespace
{

namespace fs = std::filesystem;
using namespace engine;

constexpr std::size_t maximum_json_size = 4U * 1024U * 1024U;

bool expect(bool value, std::string_view message)
{
    if (!value)
    {
        std::fprintf(stderr, "JSON importer: %.*s\n", static_cast<int>(message.size()),
                     message.data());
    }
    return value;
}

bool write(const fs::path& path, std::string_view text)
{
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write(text.data(), static_cast<std::streamsize>(text.size()));
    stream.close();
    return static_cast<bool>(stream);
}

std::string read(const fs::path& path)
{
    std::string output;
    return ResourceSystem::read_text(path, output) ? output : std::string{};
}

std::string payload(const Resource& resource)
{
    const auto bytes = resource.bytes();
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

struct Fixture
{
    fs::path root;

    bool initialize()
    {
        std::error_code error;
        root = fs::temp_directory_path(error);
        if (error)
        {
            return false;
        }
        root /= "engine-json-importer-" +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        for (const auto* directory : {"assets", "settings", "cache", "stage"})
        {
            fs::create_directories(root / directory, error);
            if (error)
            {
                return false;
            }
        }
        return true;
    }

    ~Fixture() noexcept
    {
        if (!root.empty())
        {
            std::error_code error;
            fs::remove_all(root, error);
        }
    }
};

bool conversion_tests(const Fixture& fixture)
{
    plugins::json_import::JsonImporter importer;
    GuidMap identities;
    const std::map<Guid, schema::AssetKind> kinds;
    const editor::asset_pipeline::ImportContext context{
        identities, kinds, fixture.root, fixture.root / "assets", fixture.root / "stage", "macos",
        nullptr, {}};
    editor::asset_pipeline::ImportAsset asset;
    asset.guid = "43fbdc92-4911-42ba-b8db-88d952cb31f6";
    asset.path = "config.json";
    asset.source = fixture.root / "assets/config.json";
    asset.kind = schema::AssetKind::json;
    const auto output = context.output / asset.guid;
    std::string error;
    std::string fingerprint;
    asset.build_dependencies.push_back(asset.guid);
    asset.runtime_dependencies.push_back(asset.guid);
    if (!expect(importer.id() == "json" && importer.kind() == schema::AssetKind::json &&
                    importer.matches("config.json") && importer.matches("CONFIG.JSON") &&
                    !importer.matches("config.json.meta") && !importer.matches("config.txt"),
                "JSON format is independently identified") ||
        !expect(importer.inspect_batch(context, fingerprint, error) && !fingerprint.empty() &&
                    error.empty(),
                "JSON parser and validation rules participate in the batch fingerprint") ||
        !expect(importer.scan_dependencies(asset, context, error) &&
                    asset.build_dependencies.empty() && asset.runtime_dependencies.empty(),
                "JSON strings and metadata do not create automatic resource dependencies"))
    {
        return false;
    }
    for (const auto text : std::array<std::string_view, 8>{
             "{\n  \"map\": \"levels/missing.scene\", \"label\": \"场景\"\n}\n",
             "[1, true, null, {\"weapon\": \"weapons/missing.mesh\"}]", "\"hello\"", "42",
             "-12.5e+2", "true", "false", "null"})
    {
        if (!expect(write(asset.source, text) && importer.convert(asset, context, error) &&
                        error.empty() && read(output) == text,
                    "valid JSON values preserve exact UTF-8 bytes, whitespace and member order"))
        {
            return false;
        }
    }
    constexpr std::string_view previous = "previous staged bytes";
    for (const auto& text : std::array<std::string, 13>{
             "", " ", "{", "{} {}", "{} trailing", "[1,]", "{\"a\":1,}",
             "{/* comment */\"a\":1}", "// comment\n{}", "\xef\xbb\xbf{}",
             "\"\xff\"", "\"\xc0\xaf\"", std::string("{}\0garbage", 10)})
    {
        if (!expect(write(output, previous) && write(asset.source, text) &&
                        !importer.convert(asset, context, error) && !error.empty() &&
                        read(output) == previous,
                    "invalid syntax, BOM and invalid UTF-8 never overwrite staged output"))
        {
            return false;
        }
    }
    const auto boundary = "\"" + std::string(maximum_json_size - 2, 'x') + "\"";
    if (!expect(write(asset.source, boundary) && importer.convert(asset, context, error) &&
                    read(output) == boundary,
                "a valid JSON resource of exactly 4 MiB is accepted") ||
        !expect(write(output, previous) && write(asset.source, boundary + " ") &&
                    !importer.convert(asset, context, error) && !error.empty() &&
                    read(output) == previous,
                "the size limit counts every UTF-8 byte including trailing whitespace"))
    {
        return false;
    }
    return true;
}

bool pipeline_tests(const Fixture& fixture)
{
    constexpr std::string_view first =
        "{\n  \"map\": \"levels/missing.scene\", \"weapon\": \"weapons/missing.mesh\"\n}\n";
    const auto source = fixture.root / "assets/config.json";
    ThreadSystem threads;
    TaskSystem tasks(threads, {"Editor", 4, TaskPriority::low});
    ResourceSystem resources;
    editor::AssetPipelineSystem pipeline(tasks);
    std::unique_ptr<editor::asset_pipeline::AssetImporter> importer =
        std::make_unique<plugins::json_import::JsonImporter>();
    if (!expect(threads.initialize({}) && tasks.initialize({}) && resources.initialize({}) &&
                    pipeline.initialize({}) && pipeline.add_importer(importer) && !importer &&
                    write(source, first) && pipeline.build_assets(fixture.root),
                pipeline.last_error()))
    {
        return false;
    }
    const auto& entries = pipeline.catalog().entries;
    const auto found = std::find_if(entries.begin(), entries.end(), [](const auto& entry)
                                    { return entry && entry->path == "config.json"; });
    if (!expect(found != entries.end() && (*found)->kind == schema::AssetKind::json &&
                    (*found)->dependencies.empty() && (*found)->module_path.empty(),
                "pipeline publishes an independent JSON entry without script or resource links"))
    {
        return false;
    }
    const auto guid = (*found)->guid;
    const auto metadata = read(fixture.root / "assets/config.json.meta");
    ResourcePtr original;
    schema::ResourceHeaderDataT header;
    if (!expect(metadata.find(guid) != std::string::npos &&
                    metadata.find("importer = \"json\"") != std::string::npos &&
                    resources.update_catalog(pipeline.catalog(),
                                             {{"content", fixture.root / "cache"}}) &&
                    resources.read_header(guid, header) && header.kind == schema::AssetKind::json &&
                    resources.load(guid, original) && original && payload(*original) == first,
                "standard GUID metadata, catalog, envelope and binary codec retain JSON bytes") ||
        !expect(pipeline.build_assets(fixture.root) && pipeline.last_build().imported == 0 &&
                    pipeline.last_build().reused == 1,
                "unchanged JSON uses the existing incremental cache"))
    {
        return false;
    }
    const auto changed = std::string(first) + "\n";
    if (!expect(write(source, changed) && pipeline.build_assets(fixture.root) &&
                    pipeline.last_build().imported == 1 && pipeline.last_build().reused == 0 &&
                    payload(*original) == first,
                "a byte edit reimports JSON while existing shared references remain immutable"))
    {
        return false;
    }
    const auto revision = pipeline.last_build().revision;
    const auto catalog = read(fixture.root / "cache/catalog.bin");
    const auto generated = read(fixture.root / "cache/gen" / guid);
    if (!expect(write(source, "{\"invalid\":") && !pipeline.build_assets(fixture.root) &&
                    !pipeline.last_error().empty() && pipeline.last_build().revision == revision &&
                    read(fixture.root / "cache/catalog.bin") == catalog &&
                    read(fixture.root / "cache/gen" / guid) == generated,
                "invalid JSON preserves the published catalog, resource and revision") ||
        !expect(write(source, changed) && pipeline.build_assets(fixture.root) &&
                    pipeline.last_build().reused == 1 &&
                    resources.update_catalog(pipeline.catalog(),
                                             {{"content", fixture.root / "cache"}}),
                "restoring valid JSON recovers the same resource identity and cache"))
    {
        return false;
    }
    ResourcePtr current;
    return expect(resources.load(guid, current) && current && payload(*current) == changed &&
                      payload(*original) == first,
                  "new catalog reads the new bytes while the old resource stays valid");
}

} // namespace

int main()
{
    Fixture fixture;
    return expect(fixture.initialize(), "create isolated JSON project") &&
                   conversion_tests(fixture) && pipeline_tests(fixture)
               ? 0
               : 1;
}
