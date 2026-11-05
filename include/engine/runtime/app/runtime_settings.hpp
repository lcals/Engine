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
};

// 读取完整 game/engine/parallel 配置；缺失或非法字段失败，不补默认值且保持 output 不变。
[[nodiscard]] bool decode_runtime_settings(std::string_view text, RuntimeSettings& output,
                                           std::string& error);
[[nodiscard]] bool load_runtime_settings(const std::filesystem::path& path, RuntimeSettings& output,
                                         std::string& error);

} // namespace engine
