#include "runtime/app/runtime_settings.hpp"

#include "runtime/resource/resource_system.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <utility>

namespace engine
{
namespace
{

using Json = nlohmann::json;
constexpr std::uint64_t maximum_bytes = 4ULL * 1024 * 1024 * 1024;

bool keys(const Json& value, std::initializer_list<std::string_view> allowed, std::string& error)
{
    if (!value.is_object())
    {
        error = "Runtime configuration sections must be objects";
        return false;
    }
    for (const auto& [name, item] : value.items())
    {
        if (std::find(allowed.begin(), allowed.end(), name) == allowed.end())
        {
            error = "Unknown runtime configuration field: " + std::string(name);
            return false;
        }
    }
    return true;
}

template <typename T>
bool integer(const Json& value, const char* key, T& output, std::uint64_t minimum,
             std::uint64_t maximum, std::string& error)
{
    const auto found = value.find(key);
    if (found == value.end())
    {
        return true;
    }
    if (!found->is_number_unsigned() || found->get<std::uint64_t>() < minimum ||
        found->get<std::uint64_t>() > maximum)
    {
        error = std::string("Invalid parallel integer: ") + key;
        return false;
    }
    output = static_cast<T>(found->get<std::uint64_t>());
    return true;
}

bool seconds(const Json& value, const char* key, double& output, std::string& error)
{
    const auto found = value.find(key);
    if (found == value.end())
    {
        return true;
    }
    if (!found->is_number())
    {
        error = std::string("Invalid parallel time budget: ") + key;
        return false;
    }
    const auto number = found->get<double>();
    if (!std::isfinite(number) || number <= 0.0 || number > 1.0)
    {
        error = std::string("Parallel time budget must be in (0, 1] seconds: ") + key;
        return false;
    }
    output = number;
    return true;
}

template <typename T>
bool priority(const Json& value, const char* key, T& output, std::string& error)
{
    const auto found = value.find(key);
    if (found == value.end())
    {
        return true;
    }
    if (found->is_string())
    {
        const auto name = found->get<std::string>();
        if (name == "low")
        {
            output = T::low;
            return true;
        }
        if (name == "normal")
        {
            output = T::normal;
            return true;
        }
        if (name == "high")
        {
            output = T::high;
            return true;
        }
    }
    error = std::string("Parallel priority must be low, normal or high: ") + key;
    return false;
}

bool domain(const Json& value, TaskConfig& output, std::string& error)
{
    return keys(value, {"max_concurrency", "priority", "max_pending"}, error) &&
           integer(value, "max_concurrency", output.max_concurrency, 0, 64, error) &&
           priority(value, "priority", output.priority, error) &&
           integer(value, "max_pending", output.max_pending, 1, 65536, error);
}

bool apply_parallel(const Json& value, RuntimeParallelConfig& output, std::string& error)
{
    if (!keys(value,
              {"threads", "runtime", "background", "loading", "physics", "animation", "navigation",
               "resources", "animation_sampling", "scene_restore", "render",
               "navigation_tile_threshold", "completions_per_frame", "completion_budget_seconds"},
              error))
    {
        return false;
    }
    if (const auto found = value.find("threads"); found != value.end())
    {
        if (!keys(*found, {"worker_limit", "worker_priority"}, error) ||
            !integer(*found, "worker_limit", output.threads.worker_limit, 0, 64, error) ||
            !priority(*found, "worker_priority", output.threads.worker_priority, error))
        {
            return false;
        }
    }
    const std::pair<const char*, TaskConfig*> domains[] = {
        {"runtime", &output.runtime},     {"background", &output.background},
        {"loading", &output.loading},     {"physics", &output.physics},
        {"animation", &output.animation}, {"navigation", &output.navigation}};
    for (const auto& [name, config] : domains)
    {
        if (const auto found = value.find(name);
            found != value.end() && !domain(*found, *config, error))
        {
            return false;
        }
    }
    if (const auto found = value.find("resources"); found != value.end())
    {
        auto& config = output.resources;
        if (!keys(*found, {"max_in_flight", "max_in_flight_bytes", "completion_budget"}, error) ||
            !integer(*found, "max_in_flight", config.max_in_flight, 1, 1024, error) ||
            !integer(*found, "max_in_flight_bytes", config.max_in_flight_bytes, 1, maximum_bytes,
                     error) ||
            !integer(*found, "completion_budget", config.completion_budget, 1, 65536, error))
        {
            return false;
        }
    }
    if (const auto found = value.find("animation_sampling"); found != value.end())
    {
        auto& config = output.animation_sampling;
        if (!keys(*found, {"min_parallel_count", "batch_size"}, error) ||
            !integer(*found, "min_parallel_count", config.min_parallel_count, 1, 1000000, error) ||
            !integer(*found, "batch_size", config.batch_size, 1, 1000000, error))
        {
            return false;
        }
    }
    if (const auto found = value.find("scene_restore"); found != value.end())
    {
        auto& config = output.scene_restore;
        if (!keys(*found, {"nodes_per_step", "max_seconds"}, error) ||
            !integer(*found, "nodes_per_step", config.nodes_per_step, 1, 1000000, error) ||
            !seconds(*found, "max_seconds", config.max_seconds, error))
        {
            return false;
        }
    }
    if (const auto found = value.find("render"); found != value.end())
    {
        auto& config = output.render;
        if (!keys(*found,
                  {"texture_batch_size", "texture_staging_bytes", "cluster_tile_size",
                   "cluster_depth_slices", "maximum_lights", "maximum_cluster_lights",
                   "shadow_atlas_size", "local_shadow_tile_size", "maximum_local_shadow_faces",
                   "maximum_planar_reflections"},
                  error) ||
            !integer(*found, "texture_batch_size", config.texture_batch_size, 1, 64, error) ||
            !integer(*found, "texture_staging_bytes", config.texture_staging_bytes, 1,
                     maximum_bytes, error) ||
            !integer(*found, "cluster_tile_size", config.cluster_tile_size, 16, 256, error) ||
            !integer(*found, "cluster_depth_slices", config.cluster_depth_slices, 1, 64, error) ||
            !integer(*found, "maximum_lights", config.maximum_lights, 1, 4096, error) ||
            !integer(*found, "maximum_cluster_lights", config.maximum_cluster_lights, 1, 256,
                     error) ||
            !integer(*found, "shadow_atlas_size", config.shadow_atlas_size, 512, 8192, error) ||
            !integer(*found, "local_shadow_tile_size", config.local_shadow_tile_size, 64, 2048,
                     error) ||
            !integer(*found, "maximum_local_shadow_faces", config.maximum_local_shadow_faces, 0, 24,
                     error) ||
            !integer(*found, "maximum_planar_reflections", config.maximum_planar_reflections, 0, 2,
                     error))
        {
            return false;
        }
        if ((config.shadow_atlas_size & (config.shadow_atlas_size - 1)) != 0 ||
            (config.local_shadow_tile_size & (config.local_shadow_tile_size - 1)) != 0 ||
            config.local_shadow_tile_size > config.shadow_atlas_size)
        {
            error = "Shadow dimensions must be powers of two and tiles must fit the atlas";
            return false;
        }
    }
    return integer(value, "navigation_tile_threshold", output.navigation_tile_threshold, 1, 1024,
                   error) &&
           integer(value, "completions_per_frame", output.completions_per_frame, 1, 65536, error) &&
           seconds(value, "completion_budget_seconds", output.completion_budget_seconds, error);
}

bool apply_engine(const Json& value, EngineConfig& output, std::string& error)
{
    if (!keys(value, {"window_title", "width", "height", "target_frame_rate"}, error) ||
        !integer(value, "width", output.width, 1, std::numeric_limits<int>::max(), error) ||
        !integer(value, "height", output.height, 1, std::numeric_limits<int>::max(), error))
    {
        return false;
    }
    if (const auto title = value.find("window_title"); title != value.end())
    {
        if (!title->is_string())
        {
            error = "Runtime window_title must be a string";
            return false;
        }
        const auto& text = title->get_ref<const std::string&>();
        if (text.empty() || text.size() > 256 || text.find('\0') != std::string::npos)
        {
            error = "Runtime window_title must contain 1..256 bytes without NUL";
            return false;
        }
        output.window_title = text;
    }
    if (const auto rate = value.find("target_frame_rate"); rate != value.end())
    {
        if (!rate->is_number_integer() ||
            (rate->is_number_unsigned() &&
             rate->get<std::uint64_t>() >
                 static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max())))
        {
            error = "Runtime target_frame_rate must be -1 or a positive int32";
            return false;
        }
        const auto number = rate->get<std::int64_t>();
        if (number != -1 && (number < 1 || number > std::numeric_limits<std::int32_t>::max()))
        {
            error = "Runtime target_frame_rate must be -1 or a positive int32";
            return false;
        }
        output.target_frame_rate = static_cast<std::int32_t>(number);
    }
    return true;
}

bool apply(const Json& value, RuntimeSettings& output, std::string& error)
{
    if (!keys(value, {"engine", "parallel"}, error))
    {
        return false;
    }
    if (const auto engine = value.find("engine");
        engine != value.end() && !apply_engine(*engine, output.engine, error))
    {
        return false;
    }
    if (const auto parallel = value.find("parallel");
        parallel != value.end() && !apply_parallel(*parallel, output.parallel, error))
    {
        return false;
    }
    return true;
}

} // namespace

bool decode_runtime_settings(std::string_view text, std::string_view profile,
                             RuntimeSettings& output, std::string& error)
{
    error.clear();
    if (profile != "desktop" && profile != "mobile")
    {
        error = "Runtime profile must be desktop or mobile";
        return false;
    }
    if (text.size() > 65536)
    {
        error = "Runtime settings exceed 64 KiB";
        return false;
    }
    const auto document = Json::parse(text, nullptr, false);
    if (!keys(document, {"version", "defaults", "profiles", "overrides", "game_language"}, error))
    {
        return false;
    }
    if (!document.contains("version") || !document["version"].is_number_unsigned() ||
        document["version"].get<std::uint64_t>() != 1)
    {
        error = "Runtime settings require version 1";
        return false;
    }
    auto candidate = output;
    if (const auto language = document.find("game_language"); language != document.end())
    {
        if (!language->is_string())
        {
            error = "Runtime game_language must be a string";
            return false;
        }
        candidate.game_language = language->get_ref<const std::string&>();
    }
    if (const auto defaults = document.find("defaults");
        defaults != document.end() && !apply(*defaults, candidate, error))
    {
        return false;
    }
    if (const auto profiles = document.find("profiles"); profiles != document.end())
    {
        if (!keys(*profiles, {"desktop", "mobile"}, error))
        {
            return false;
        }
        const auto defaults = candidate;
        for (const auto& [name, item] : profiles->items())
        {
            auto validated = defaults;
            if (!apply(item, validated, error))
            {
                error = name + ": " + error;
                return false;
            }
            if (name == profile)
            {
                candidate = std::move(validated);
            }
        }
    }
    if (const auto overrides = document.find("overrides");
        overrides != document.end() && !apply(*overrides, candidate, error))
    {
        return false;
    }
    output = std::move(candidate);
    return true;
}

bool load_runtime_settings(const std::filesystem::path& path, std::string_view profile,
                           RuntimeSettings& output, std::string& error)
{
    std::string text;
    if (!ResourceSystem::read_text(path, text))
    {
        error = "Cannot read runtime settings";
        return false;
    }
    return decode_runtime_settings(text, profile, output, error);
}

} // namespace engine
