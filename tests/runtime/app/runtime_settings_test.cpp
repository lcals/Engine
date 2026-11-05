#include "resource_fixture.hpp"

#include "runtime/app/runtime_settings.hpp"
#include "runtime/resource/resource_system.hpp"

#include <nlohmann/json.hpp>

#include <cstdio>
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
    using Json = nlohmann::json;
    std::string text;
    std::string error;
    engine::RuntimeSettings settings;
    if (argc < 2 || !engine::ResourceSystem::read_text(argv[1], text))
    {
        std::fprintf(stderr, "runtime settings: a complete fixture JSON path is required\n");
        return 1;
    }
    const auto source = Json::parse(text, nullptr, false);
    if (!expect(source.is_object() && engine::load_runtime_settings(argv[1], settings, error) &&
                    error.empty() && settings.game_language == "zh-CN" &&
                    settings.startup_movie.empty() &&
                    settings.engine.window_title == "Runtime settings fixture" &&
                    settings.engine.width == 1440 && settings.engine.height == 900 &&
                    settings.engine.target_frame_rate == 30 &&
                    settings.parallel.threads.worker_limit == 2 &&
                    settings.parallel.loading.max_concurrency == 4 &&
                    settings.parallel.resources.max_in_flight == 3 &&
                    settings.parallel.render.texture_batch_size == 2,
                "complete grouped settings supply the game, engine and runtime budgets"))
    {
        return 1;
    }
    const auto rejects = [&](const std::string& candidate)
    {
        return expect(!engine::decode_runtime_settings(candidate, settings, error) &&
                          !error.empty() && settings.game_language == "zh-CN" &&
                          settings.engine.width == 1440 &&
                          settings.engine.target_frame_rate == 30 &&
                          settings.parallel.threads.worker_limit == 2,
                      "invalid or incomplete settings fail without publishing partial values");
    };
    for (const auto invalid : {"not JSON", "[]", "{}", "{\"game\":{}}"})
    {
        if (!rejects(invalid))
        {
            return 1;
        }
    }
    // 每个补丁应用到同一完整夹具，确保失败来自指定字段而非其他缺失参数。
    const char* invalid_patches[] = {
        R"({"version":1})",
        R"({"defaults":{}})",
        R"({"profiles":{"desktop":{}}})",
        R"({"overrides":{}})",
        R"({"game_language":"en"})",
        R"({"game":null})",
        R"({"engine":null})",
        R"({"parallel":null})",
        R"({"game":{"game_language":null}})",
        R"({"engine":{"height":null}})",
        R"({"parallel":{"runtime":{"max_pending":null}}})",
        R"({"parallel":{"render":{"maximum_lights":null}}})",
        R"({"parallel":{"completion_budget_seconds":null}})",
        R"({"game":{"game_language":42}})",
        R"({"game":{"startup_movie":42}})",
        R"({"game":{"quality":"high"}})",
        R"({"game":[]})",
        R"({"parallel":{"threads":{"worker_limit":65}}})",
        R"({"parallel":{"threads":{"worker_limit":-1}}})",
        R"({"parallel":{"threads":{"worker_limit":2.5}}})",
        R"({"parallel":{"runtime":{"priority":"urgent"}}})",
        R"({"parallel":{"resources":{"max_in_flight":0}}})",
        R"({"parallel":{"scene_restore":{"max_seconds":0}}})",
        R"({"parallel":{"completion_budget_seconds":2}})",
        R"({"parallel":{"navigation_tile_threshold":1025}})",
        R"({"parallel":{"render":{"texture_batch_size":65}}})",
        R"({"parallel":{"render":{"cluster_tile_size":15}}})",
        R"({"parallel":{"render":{"cluster_depth_slices":65}}})",
        R"({"parallel":{"render":{"maximum_lights":4097}}})",
        R"({"parallel":{"render":{"maximum_cluster_lights":0}}})",
        R"({"parallel":{"render":{"shadow_atlas_size":3000}}})",
        R"({"parallel":{"render":{"local_shadow_tile_size":768}}})",
        R"({"parallel":{"render":{"shadow_atlas_size":512,"local_shadow_tile_size":1024}}})",
        R"({"parallel":{"render":{"maximum_local_shadow_faces":25}}})",
        R"({"parallel":{"render":{"maximum_planar_reflections":3}}})",
        R"({"engine":{"width":0}})",
        R"({"engine":{"window_title":""}})",
        R"({"engine":{"target_frame_rate":0}})",
        R"({"engine":{"target_frame_rate":18446744073709551615}})",
        R"({"game":{"game_language":"changed"},"engine":{"width":800},"parallel":{"physics":{"max_concurrency":99}}})"};
    for (const auto patch : invalid_patches)
    {
        auto candidate = source;
        const auto parsed = Json::parse(patch, nullptr, false);
        if (!parsed.is_object())
        {
            return 1;
        }
        candidate.merge_patch(parsed);
        if (!rejects(candidate.dump()))
        {
            return 1;
        }
    }
    auto candidate = source;
    candidate["game"]["startup_movie"] = "movies/intro.movie";
    if (!expect(engine::decode_runtime_settings(candidate.dump(), settings, error) &&
                    settings.startup_movie == "movies/intro.movie" &&
                    engine::decode_runtime_settings(source.dump(), settings, error) &&
                    settings.startup_movie.empty(),
                "optional startup movie is copied and missing value clears previous selection"))
    {
        return 1;
    }
    candidate.merge_patch(Json::parse(
        R"({"engine":{"target_frame_rate":-1},"parallel":{"navigation_tile_threshold":1024,"render":{"texture_batch_size":64,"maximum_local_shadow_faces":0,"maximum_planar_reflections":0}}})",
        nullptr, false));
    if (!expect(engine::decode_runtime_settings(candidate.dump(), settings, error) &&
                    settings.engine.target_frame_rate == -1 &&
                    settings.parallel.navigation_tile_threshold == 1024 &&
                    settings.parallel.render.texture_batch_size == 64 &&
                    settings.parallel.render.maximum_local_shadow_faces == 0 &&
                    settings.parallel.render.maximum_planar_reflections == 0,
                "explicit upper bounds and disabled optional captures are accepted"))
    {
        return 1;
    }
    test_support::ResourceFixture directory;
    if (!expect(!engine::load_runtime_settings(directory.root / "missing", settings, error),
                "an explicitly supplied missing file reports failure"))
    {
        return 1;
    }
    // 附加参数可直接核对打包器生成的真实 JSON。
    for (int index = 2; index < argc; ++index)
    {
        if (!engine::load_runtime_settings(argv[index], settings, error))
        {
            std::fprintf(stderr, "%s: %s\n", argv[index], error.c_str());
            return 1;
        }
    }
    return 0;
}
