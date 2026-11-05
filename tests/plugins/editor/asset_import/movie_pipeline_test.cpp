#include "editor/asset_pipeline/asset_pipeline_system.hpp"
#include "plugins/editor/asset_import/movie_importer.hpp"
#include "runtime/engine_config.hpp"
#include "runtime/resource/resource_system.hpp"
#include "runtime/schema/movie_generated.h"
#include "runtime/task/task_system.hpp"
#include "runtime/task/thread_system.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <system_error>

namespace
{
namespace fs = std::filesystem;
using namespace engine;

bool expect(bool value, const char* message)
{
    if (!value)
    {
        std::fprintf(stderr, "movie pipeline: %s\n", message);
    }
    return value;
}
struct Fixture
{
    fs::path root;
    ~Fixture() noexcept
    {
        std::error_code error;
        fs::remove_all(root, error);
    }
};
} // namespace

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        return 1;
    }
    std::error_code error;
    Fixture fixture;
    fixture.root = fs::temp_directory_path(error) /
                   ("engine-movie-pipeline-" +
                    std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    for (const auto* name : {"assets/movies", "settings", "cache"})
    {
        fs::create_directories(fixture.root / name, error);
        if (error)
        {
            return 1;
        }
    }
    fs::copy_file(argv[1], fixture.root / "assets/movies/studio.mp4", error);
    if (error)
    {
        return 1;
    }
    {
        std::ofstream config(fixture.root / "settings/editor-import.toml");
        config << "[common]\nimport_threads = 8\n";
        if (!config)
        {
            return 1;
        }
    }
    ThreadSystem threads;
    TaskSystem tasks(threads, {"Editor", 4, TaskPriority::low});
    ResourceSystem resources;
    editor::AssetPipelineSystem pipeline(tasks);
    std::unique_ptr<editor::asset_pipeline::AssetImporter> importer =
        std::make_unique<plugins::asset_import::MovieImporter>();
    if (!threads.initialize({}) || !tasks.initialize({}) || !resources.initialize({}) ||
        !pipeline.initialize({}) || !pipeline.add_importer(importer) ||
        !pipeline.build_assets(fixture.root))
    {
        std::fprintf(stderr, "movie import failed: %s\n", pipeline.last_error().c_str());
        return 1;
    }
    const auto& entries = pipeline.catalog().entries;
    const auto movie = std::find_if(entries.begin(), entries.end(), [](const auto& entry)
                                    { return entry && entry->path == "movies/studio.mp4"; });
    const auto audio = std::find_if(entries.begin(), entries.end(), [](const auto& entry)
                                    { return entry && entry->path == "movies/studio.mp4/audio"; });
    if (!expect(movie != entries.end() && audio != entries.end(), "movie and audio published") ||
        !expect(
            (*movie)->kind == schema::AssetKind::movie &&
                (*audio)->kind == schema::AssetKind::audio && (*movie)->dependencies.size() == 1 &&
                (*movie)->dependencies.front() == (*audio)->guid && (*audio)->dependencies.empty(),
            "movie has exactly its real audio dependency"))
    {
        return 1;
    }
    const auto movie_guid = (*movie)->guid;
    const auto audio_guid = (*audio)->guid;
    ResourcePtr movie_resource;
    ResourcePtr audio_resource;
    if (!expect(
            resources.update_catalog(pipeline.catalog(), {{"content", fixture.root / "cache"}}) &&
                resources.load(movie_guid, movie_resource) && movie_resource &&
                resources.load(audio_guid, audio_resource) && audio_resource,
            "both resources decode through the catalog"))
    {
        return 1;
    }
    const auto* video = schema::GetMovieData(movie_resource->bytes().data())->video();
    if (!expect(video && video->width() == 1280 && video->height() == 720 &&
                    video->frame_count() == 150 && audio_resource->audio() &&
                    audio_resource->audio()->frames == 240000,
                "five-second movie and audio have matching duration") ||
        !expect(pipeline.build_assets(fixture.root) && pipeline.last_build().imported == 0 &&
                    pipeline.last_build().reused == 2,
                "unchanged movie uses cache"))
    {
        return 1;
    }
    GuidMap identities;
    const std::map<Guid, schema::AssetKind> kinds;
    const auto staging = fixture.root / "cache/cancelled";
    fs::create_directories(staging, error);
    if (error)
    {
        return 1;
    }
    const editor::asset_pipeline::ImportContext cancelled{
        identities, kinds,   fixture.root, fixture.root / "assets", staging,
        "macos",    nullptr, {},           [] { return true; }};
    editor::asset_pipeline::ImportAsset cancelled_asset;
    cancelled_asset.guid = movie_guid;
    cancelled_asset.source = fixture.root / "assets/movies/studio.mp4";
    cancelled_asset.path = "movies/studio.mp4";
    plugins::asset_import::MovieImporter direct_importer;
    std::string cancellation_error;
    if (!expect(!direct_importer.convert(cancelled_asset, cancelled, cancellation_error) &&
                    cancellation_error.find("cancelled") != std::string::npos &&
                    !fs::exists(staging / movie_guid),
                "cancelled conversion leaves no output"))
    {
        return 1;
    }
    const auto revision = pipeline.last_build().revision;
    {
        std::ofstream broken(fixture.root / "assets/movies/studio.mp4",
                             std::ios::binary | std::ios::trunc);
        broken << "invalid";
    }
    if (!expect(!pipeline.build_assets(fixture.root) && pipeline.last_build().revision == revision,
                "invalid source leaves the published version unchanged"))
    {
        return 1;
    }
    return 0;
}
