#include "resource_fixture.hpp"

#include "runtime/app/runtime_settings.hpp"

#include <cstdio>
#include <fstream>
#include <string>

namespace
{

bool expect(bool success, const char* message)
{
    if (!success)
    {
        std::fprintf(stderr, "runtime settings: %s\n", message);
    }
    return success;
}

} // namespace

int main(int argc, char* argv[])
{
    test_support::ResourceFixture directory;
    const auto path = directory.root / "runtime.json";
    const std::string text = R"json({
        "version": 1,
        "game_language": "zh-CN",
        "defaults": {
            "engine": {"width":1440,"target_frame_rate":45},
            "parallel": {"threads":{"worker_limit":6},"resources":{"max_in_flight":4}}
        },
        "profiles": {
            "desktop": {"parallel":{"loading":{"max_concurrency":4}}},
            "mobile": {"engine":{"target_frame_rate":30},"parallel":{"threads":{"worker_limit":2}}}
        },
        "overrides": {"parallel":{"resources":{"max_in_flight":3},"render":{"texture_batch_size":2}}}
    })json";
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file << text;
    file.close();
    if (directory.root.empty() || !file)
    {
        return 1;
    }
    engine::RuntimeSettings settings;
    settings.engine.window_title = "Engine Editor";
    std::string error;
    if (!expect(settings.game_language == "en" &&
                    engine::load_runtime_settings(path, "desktop", settings, error) && error.empty() &&
                    settings.game_language == "zh-CN" &&
                    settings.engine.window_title == "Engine Editor" &&
                    settings.engine.width == 1440 && settings.engine.target_frame_rate == 45 &&
                    settings.parallel.threads.worker_limit == 6 &&
                    settings.parallel.loading.max_concurrency == 4 &&
                    settings.parallel.resources.max_in_flight == 3 &&
                    settings.parallel.render.texture_batch_size == 2,
                "aggregate engine and parallel settings override caller defaults in order"))
    {
        return 1;
    }
    settings = {};
    if (!expect(engine::decode_runtime_settings(text, "mobile", settings, error) &&
                    settings.parallel.threads.worker_limit == 2 &&
                    settings.parallel.loading.max_concurrency == 2 &&
                    settings.engine.target_frame_rate == 30,
                "host explicitly selects mobile and retains fields absent from its layer"))
    {
        return 1;
    }
    const char* invalid[] = {
        "version = 1",
        R"({"version":2})",
        R"({"version":true})",
        R"({"version":1,"game_language":42})",
        R"({"version":1,"defaults":{"thread":{}}})",
        R"({"version":1,"defaults":{"parallel":{"threads":{"worker_limit":65}}}})",
        R"({"version":1,"defaults":{"parallel":{"threads":{"worker_limit":-1}}}})",
        R"({"version":1,"defaults":{"parallel":{"threads":{"worker_limit":2.5}}}})",
        R"({"version":1,"defaults":{"parallel":{"runtime":{"priority":"urgent"}}}})",
        R"({"version":1,"defaults":{"parallel":{"resources":{"max_in_flight":0}}}})",
        R"({"version":1,"defaults":{"parallel":{"scene_restore":{"max_seconds":0}}}})",
        R"({"version":1,"defaults":{"parallel":{"completion_budget_seconds":2}}})",
        R"({"version":1,"defaults":{"parallel":{"navigation_tile_threshold":1025}}})",
        R"({"version":1,"defaults":{"parallel":{"render":{"texture_batch_size":65}}}})",
        R"({"version":1,"defaults":{"parallel":{"render":{"cluster_tile_size":15}}}})",
        R"({"version":1,"defaults":{"parallel":{"render":{"cluster_depth_slices":65}}}})",
        R"({"version":1,"defaults":{"parallel":{"render":{"maximum_lights":4097}}}})",
        R"({"version":1,"defaults":{"parallel":{"render":{"maximum_cluster_lights":0}}}})",
        R"({"version":1,"defaults":{"parallel":{"render":{"shadow_atlas_size":3000}}}})",
        R"({"version":1,"defaults":{"parallel":{"render":{"local_shadow_tile_size":768}}}})",
        R"({"version":1,"defaults":{"parallel":{"render":{"shadow_atlas_size":512,"local_shadow_tile_size":1024}}}})",
        R"({"version":1,"defaults":{"parallel":{"render":{"maximum_local_shadow_faces":25}}}})",
        R"({"version":1,"defaults":{"parallel":{"render":{"maximum_planar_reflections":3}}}})",
        R"({"version":1,"defaults":{"engine":{"width":0}}})",
        R"({"version":1,"defaults":{"engine":{"window_title":""}}})",
        R"({"version":1,"defaults":{"engine":{"target_frame_rate":0}}})",
        R"({"version":1,"defaults":{"engine":{"target_frame_rate":18446744073709551615}}})",
        R"({"version":1,"profiles":{"other":{}}})",
        R"({"version":1,"profiles":{"mobile":{"parallel":{"physics":{"max_concurrency":99}}}}})",
        R"({"version":1,"profiles":{"mobile":{"parallel":{"navigation_tile_threshold":1025}}}})",
        R"({"version":1,"profiles":[]})"};
    for (const auto value : invalid)
    {
        if (!expect(!engine::decode_runtime_settings(value, "desktop", settings, error) &&
                        !error.empty() && settings.parallel.threads.worker_limit == 2 &&
                        settings.engine.target_frame_rate == 30 && settings.game_language == "zh-CN",
                    "invalid source, unselected profile and ranges reject without partial output"))
        {
            return 1;
        }
    }
    if (!expect(
            engine::decode_runtime_settings(
                R"({"version":1,"defaults":{"parallel":{"navigation_tile_threshold":1024,"render":{"texture_batch_size":64}},"engine":{"target_frame_rate":-1}}})",
                "desktop", settings, error) &&
                settings.parallel.navigation_tile_threshold == 1024 &&
                settings.parallel.render.texture_batch_size == 64 &&
                settings.engine.target_frame_rate == -1,
            "upper tile/texture bounds and unlimited frame rate are accepted") ||
        !expect(!engine::decode_runtime_settings(text, "automatic", settings, error),
                "the parser never guesses the platform profile") ||
        !expect(
            !engine::load_runtime_settings(directory.root / "missing", "mobile", settings, error),
            "an explicitly supplied missing file reports failure"))
    {
        return 1;
    }
    settings.engine.window_title = "Host default";
    if (!expect(
            engine::decode_runtime_settings(
                R"({"version":1,"defaults":{"parallel":{"render":{"maximum_lights":512,"maximum_cluster_lights":96,"shadow_atlas_size":8192,"local_shadow_tile_size":1024}}},"profiles":{"mobile":{"parallel":{"render":{"maximum_lights":128,"shadow_atlas_size":4096,"local_shadow_tile_size":256,"maximum_local_shadow_faces":0,"maximum_planar_reflections":0}}}}})",
                "mobile", settings, error) &&
                settings.parallel.render.maximum_lights == 128 &&
                settings.parallel.render.maximum_cluster_lights == 96 &&
                settings.parallel.render.shadow_atlas_size == 4096 &&
                settings.parallel.render.local_shadow_tile_size == 256 &&
                settings.parallel.render.maximum_local_shadow_faces == 0 &&
                settings.parallel.render.maximum_planar_reflections == 0,
            "mobile rendering overrides retain absent defaults and can disable optional captures"))
    {
        return 1;
    }
    if (!expect(engine::decode_runtime_settings(R"({"version":1})", "desktop", settings, error) &&
                    settings.engine.window_title == "Host default",
                "an empty versioned file preserves explicit caller defaults"))
    {
        return 1;
    }
    // 可直接核对打包器生成的真实 JSON；常规 CTest 不依赖私有项目或导出目录。
    for (int index = 1; index < argc; ++index)
    {
        for (const auto profile : {"desktop", "mobile"})
        {
            settings = {};
            if (!engine::load_runtime_settings(argv[index], profile, settings, error))
            {
                std::fprintf(stderr, "%s: %s\n", argv[index], error.c_str());
                return 1;
            }
        }
    }
    return 0;
}
