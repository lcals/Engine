#pragma once

#include "runtime/resource/resource.hpp"

#include <string>

namespace engine::plugins::effects
{

// 官方运行数据解析及当前适配能力校验；不读取或加载外部依赖。
[[nodiscard]] bool validate_effect(ByteView bytes, std::string& error);

} // namespace engine::plugins::effects
