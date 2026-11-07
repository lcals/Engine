#include "editor/asset_pipeline/asset_pipeline_system.hpp"
#include "editor/asset_pipeline/import_tool.hpp"
#include "plugins/editor/asset_import/movie_importer.hpp"
#include "resource_fixture.hpp"
#include "runtime/utils/path_utils.hpp"
#if ENGINE_TEST_MOVIE_BUILD
#include "plugins/editor/asset_build/asset_build_system.hpp"
#endif
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
bool write(const fs::path& path, std::string_view content)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << content;
    output.close();
    return static_cast<bool>(output);
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
    if (!threads.initialize({}) || !tasks.initialize({}) ||
        !(test_support::register_resource_codecs(resources) && resources.initialize({})) ||
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
    std::unique_ptr<Resource> movie_resource;
    std::unique_ptr<Resource> audio_resource;
    if (!expect(
            resources.update_catalog(pipeline.catalog(), {{"content", fixture.root / "cache"}}) &&
                resources.read(movie_guid, movie_resource) && movie_resource &&
                resources.read(audio_guid, audio_resource) && audio_resource,
            "both resources decode through the catalog"))
    {
        return 1;
    }
    const auto* video =
        engine::resource_detail::data<MovieResource>(movie_resource.get())->video.get();
    if (!expect(video && video->width == 1280 && video->height == 720 &&
                    video->frame_count == 150 &&
                    engine::resource_detail::data<engine::AudioResource>(audio_resource.get()) &&
                    engine::resource_detail::data<engine::AudioResource>(audio_resource.get())
                            ->frames == 240000,
                "five-second movie and audio have matching duration") ||
        !expect(pipeline.build_assets(fixture.root) && pipeline.last_build().imported == 0 &&
                    pipeline.last_build().reused == 2,
                "unchanged movie uses cache"))
    {
        return 1;
    }
#if ENGINE_TEST_MOVIE_BUILD
    plugins::asset_build::AssetBuildSystem build(pipeline);
    const auto build_config = fixture.root / "settings/editor-resource-build.toml";
    const auto game_config = fixture.root / "settings/runtime-game.toml";
    const std::string platform = pipeline.catalog().target_platform;
    const std::string game = "[common]\nstartup_movie = \"movies/missing.mp4\"\n[platform." +
                             platform + "]\nstartup_movie = \"movies/studio.mp4\"\n";
    if (!build.initialize({}) ||
        !write(build_config, "[common]\n[[common.items]]\nname = \"main\"\n"
                             "storage = \"files\"\nroots = []\n") ||
        !write(game_config, game) || !build.build(fixture.root))
    {
        std::fprintf(stderr, "movie package failed: %s\n", build.last_error().c_str());
        return 1;
    }
    ResourceCatalog packaged;
    const auto destination = build.last_build().output_path;
    if (!expect(ResourceSystem::read_catalog(destination, packaged) && packaged.find(movie_guid) &&
                    packaged.find(audio_guid) &&
                    packaged.find(movie_guid)->location->path == "gen/" + movie_guid &&
                    packaged.find(audio_guid)->location->path == "gen/" + audio_guid,
                "platform startup movie and its audio are included despite empty explicit roots"))
    {
        return 1;
    }
    const std::string packaged_revision(packaged.revision());
    if (!write(game_config, "[common]\nstartup_movie = \"movies/studio.mp4/audio\"\n") ||
        !expect(!build.build(fixture.root) && ResourceSystem::read_catalog(destination, packaged) &&
                    packaged.revision() == packaged_revision,
                "wrong-kind startup resource fails without replacing the published package") ||
        !write(game_config, "[common]\nstartup_movie = false\n") ||
        !expect(!build.build(fixture.root), "invalid startup setting is rejected") ||
        !write(game_config, game) ||
        !write(build_config, "[common]\n[[common.items]]\nname = \"main\"\n"
                             "storage = \"package\"\nroots = [\"movies/studio.mp4\"]\n") ||
        !expect(build.build(fixture.root) && ResourceSystem::read_catalog(destination, packaged) &&
                    packaged.find(movie_guid)->location->path.starts_with("packs/main-") &&
                    packaged.find(audio_guid)->location->path.starts_with("packs/main-"),
                "automatic startup collection preserves explicit package storage"))
    {
        return 1;
    }
    // Write a valid platform override which explicitly disables the common startup movie.
    if (!write(game_config, "[common]\nstartup_movie = \"movies/studio.mp4\"\n[platform." +
                                platform + "]\nstartup_movie = \"\"\n") ||
        !write(build_config, "[common]\n[[common.items]]\nname = \"main\"\n"
                             "storage = \"files\"\nroots = []\n") ||
        !expect(build.build(fixture.root) && ResourceSystem::read_catalog(destination, packaged) &&
                    !packaged.find(movie_guid) && !packaged.find(audio_guid),
                "empty platform override disables automatic startup collection"))
    {
        return 1;
    }
#endif
    std::filesystem::path ffmpeg;
    std::string tool_error;
    std::string tool_output;
    const auto anamorphic = fixture.root / "cache/anamorphic.mp4";
    if (!editor::asset_pipeline::find_import_tool("ffmpeg", ffmpeg, tool_error) ||
        editor::asset_pipeline::run_import_tool({PathUtils::to_utf8(ffmpeg), "-v", "error", "-y",
                                                 "-i", argv[1], "-map", "0", "-c", "copy",
                                                 "-aspect", "32:9", PathUtils::to_utf8(anamorphic)},
                                                tool_output, tool_error, {}, 65536) !=
            editor::asset_pipeline::ImportToolStatus::success)
    {
        std::fprintf(stderr, "anamorphic fixture failed: %s %s\n", tool_error.c_str(),
                     tool_output.c_str());
        return 1;
    }
    fs::copy_file(anamorphic, fixture.root / "assets/movies/studio.mp4",
                  fs::copy_options::overwrite_existing, error);
    movie_resource.reset();
    audio_resource.reset();
    if (error || !pipeline.build_assets(fixture.root) ||
        !resources.update_catalog(pipeline.catalog(), {{"content", fixture.root / "cache"}}) ||
        !(movie_resource.reset(), resources.read(movie_guid, movie_resource)) ||
        !(audio_resource.reset(), resources.read(audio_guid, audio_resource)))
    {
        std::fprintf(stderr, "anamorphic import failed: %s\n", pipeline.last_error().c_str());
        return 1;
    }
    video = engine::resource_detail::data<MovieResource>(movie_resource.get())->video.get();
    if (!expect(video && video->width == 1920 && video->height == 540 &&
                    engine::resource_detail::data<engine::AudioResource>(audio_resource.get())
                            ->frames == static_cast<std::uint64_t>(video->frame_count) * 1600,
                "non-square source pixels preserve 32:9 display aspect and matching audio"))
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
