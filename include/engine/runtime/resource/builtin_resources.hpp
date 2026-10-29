#pragma once

namespace engine
{

// 本阶段用代码注册默认定义；后续资源加载仍使用同一个注册入口。
[[nodiscard]] bool register_builtin_render_resources();

} // namespace engine
