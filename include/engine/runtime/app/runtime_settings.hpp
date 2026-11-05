#pragma once

#include "runtime/app/runtime_parallel_config.hpp"
#include "runtime/engine_config.hpp"

#include <filesystem>
#include <string>
#include <string_view>

namespace engine
{

struct RuntimeSettings
{
    EngineConfig engine;
    RuntimeParallelConfig parallel;
    std::string game_language;
    std::string startup_movie;
};

// 读取 game/engine/parallel 配置；startup_movie 可缺省，其他字段缺失或非法时保持 output 不变。
[[nodiscard]] bool decode_runtime_settings(std::string_view text, RuntimeSettings& output,
                                           std::string& error);
[[nodiscard]] bool load_runtime_settings(const std::filesystem::path& path, RuntimeSettings& output,
                                         std::string& error);

} // namespace engine
