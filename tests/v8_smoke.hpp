#pragma once

namespace engine::tests
{

using V8SmokeLog = void (*)(const char* message);

// 每个进程只允许调用一次。icu_data_file 非空时覆盖程序旁的默认 ICU 数据路径。
// 所有参数均为借用，仅在调用期间使用；log 不允许为空。
[[nodiscard]] bool run_v8_smoke(const char* executable_path, const char* icu_data_file,
                                V8SmokeLog log);

} // namespace engine::tests
