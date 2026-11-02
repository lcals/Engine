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
};

// 按调用方默认值、defaults、指定 profile、overrides 覆盖；失败保持 output 不变。
// profile 只能由宿主明确给出 desktop/mobile，配置解析器不识别项目或平台。
[[nodiscard]] bool decode_runtime_settings(std::string_view text, std::string_view profile,
                                           RuntimeSettings& output, std::string& error);
[[nodiscard]] bool load_runtime_settings(const std::filesystem::path& path,
                                         std::string_view profile, RuntimeSettings& output,
                                         std::string& error);

} // namespace engine
