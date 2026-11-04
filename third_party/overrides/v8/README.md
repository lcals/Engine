# V8 构建副本覆盖

当前固定 V8 **15.7.112.23**（`440c1c463e2e528c309af5067952e7b11631507f`），
来自 Chrome Stable **157.0.8103.38** 的 `DEPS`，不是每日开发分支。
2026-11-04 核对的 Chrome-for-Testing 最新稳定清单时间为
`2026-11-03T22:51:17.271Z`；版本来源：
[稳定清单](https://googlechromelabs.github.io/chrome-for-testing/last-known-good-versions.json)、
[Chromium 固定 DEPS](https://github.com/chromium/chromium/blob/157.0.8103.38/DEPS)、
[V8 固定提交](https://github.com/v8/v8/commit/440c1c463e2e528c309af5067952e7b11631507f)。

本版 `DEPS` 的兼容补丁基线如下；补丁只应用于忽略的构建副本，不修改子模块：

| 源码 | 固定提交 |
| --- | --- |
| Chromium build | `aa268127b8cc571ebc60f200d052a09d27231843` |
| PartitionAlloc | `1b65ca87736e9edb51911e15915573bf4cda7245` |
| depot_tools | `a03dfbf7f2d1cefa2795d81d59ca4f16c8616a8f` |

`system-libcxx-hardening-build.patch` 与 `system-libcxx-hardening-v8.patch`
适用于上表固定的 Chromium build 与 V8 提交。V8 15.7 的 sandbox 新增
`use_safe_libcxx` 前置约束，而该变量只表示上游私有 libc++，不能代表宿主 SDK / NDK。
嵌入适配保持它原本的含义，另设默认关闭的 `engine_verified_system_libcxx`；
只有准备探针通过后才启用。V8 的 sandbox 断言保留，并明确接受这一受检能力。

受检路径仍使用厂商 libc++ 的官方
`_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_EXTENSIVE`；
不替换容器、分配器、运行库或标准库 ABI。每个 V8 C++ 翻译单元强制包含守卫头，
要求 macOS libc++ **220106** 或 Android NDK r30 libc++ **210000**，并拒绝缺少
EXTENSIVE 模式、模式被关闭或其他平台。头文件版本变化须重新验证，不能静默接受。
标准库对不同硬化模式使用隐藏 inline 函数的 ODR ABI tag，保留厂商导出 ABI；
[上游硬化说明](https://libcxx.llvm.org/Hardening.html)解释了这些模式的标准语义。

准备流程使用固定 V8 Clang 编译工具探针，合法访问必须成功，vector / span / string
越界必须真实触发 trap；Android 目标另编译同一守卫和探针。探针源码、准备脚本、
兼容补丁、实际守卫以及 SDK / NDK 的关键头文件都进入缓存身份。
2026-11-04 在 macOS SDK 27.2 和现有 Android 34 ARM64 模拟器实测：
合法访问返回 0，三类越界分别触发 SIGTRAP；普通 / EXTENSIVE 翻译单元之间的
vector、string、shared_ptr 增容、引用和销毁互操作通过。
这只验证已使用的 libc++ 配置，不代表 Windows MSVC STL 或 Linux libstdc++ 等价。
当前 V8 准备流程在这两类宿主上明确拒绝受检路径，保持 sandbox，不自动关闭它。
Android 的该构建路径目前也要求 macOS 宿主；默认 QuickJS 不受此限制。

`cppgc-configuration-order.patch` 适用于上述固定 V8 提交。上游
`src/heap/cppgc-internal/gc-info.cc` 在读取 `v8config.h` 前就判断
`CPPGC_ENABLE_OBJECT_SECTION_GCINFO`；使用官方生成配置头时，这会先进入错误分支，
随后声明与实现条件不一致，出现 `EnsureGCInfoIndexTrait` 未声明的编译错误。
补丁只将官方配置头移到该条件之前，保留 cppgc 与对象节注册方式。
2026-11-04 核对 V8 15.7.112.25 及上游 main，仍有相同顺序；
上游修正配置头读取顺序后移除此补丁。

`windows-dynamic-crt.patch` 适用于 V8 `DEPS` 固定的 Chromium build 提交
`aa268127b8cc571ebc60f200d052a09d27231843`。

该上游配置在非 component 的 Windows 桌面构建中默认使用静态 CRT，而本项目的
`x64-windows` vcpkg 依赖和 CMake 目标使用动态 CRT。补丁仅将默认 CRT 配置替换为
上游已有的 `dynamic_crt`，按 Debug / Release 分别采用 `/MDd` / `/MD`。
这是嵌入方 ABI 配置，不代表已报告的上游缺陷。

`windows-static-crt.patch` 在同一 Chromium build 提交中增加默认关闭的
`engine_use_static_crt` GN 参数。启用后使用上游已有的 `static_crt` 配置，
C++ 与 Rust 同时采用 `/MTd` / `/MT` 对应的 CRT；保留原动态 CRT 配置供旧产物复用。
CMake 根据 `CMAKE_MSVC_RUNTIME_LIBRARY` 选择，静态 CRT 使用独立的
`out/engine.mt-debug`、`out/engine.mt-release` 与生成头文件目录，不覆盖动态 CRT 缓存。

`windows-shared-ptr-atomic-warning.patch` 适用于 V8 固定提交
`440c1c463e2e528c309af5067952e7b11631507f`。此版本的
`src/wasm/wasm-code-manager.h` 仍调用 C++20 已弃用的 `std::atomic_load(shared_ptr*)`；
MSVC STL 14.44 的 STL4029 警告在 V8 的 `/WX` 下导致编译失败。补丁仅在 V8 内部
`toolchain` 配置的 Windows 且使用 MSVC STL 时定义该特定诊断的抑制宏；其余警告和
`/WX` 保持开启，不向引擎目标传播，也不改变原子访问实现。上游替换这些弃用调用后移除。

`windows-debug-quarantine.patch` 适用于 V8 `DEPS` 固定的 PartitionAlloc 提交
`1b65ca87736e9edb51911e15915573bf4cda7245`。MSVC 的 Debug STL 在构造空 `vector`
时也会分配迭代器代理；隔离队列使用的 `InternalAllocator` 因此会在初始化自己的
`PartitionRoot` 时递归进入同一个静态初始化锁，导致 `mksnapshot` 停住。补丁将队列
延迟到启用隔离时构造，未构造时查询与清理按空队列处理，保留 Debug 迭代器检查和
PartitionAlloc。上游支持此配置下无分配的队列初始化后移除；尚未向上游提交问题。

`android-macos-host.patch` 和 `android-ndk-runtime.patch` 适用于上述固定的 Chromium
build 提交，应用根目录均为构建副本内的 `build/`：

- 前者允许 macOS 宿主生成 Android 原生构建，并让 Mac 的 `mksnapshot` 等宿主工具
  使用系统链接器。Android 目标继续使用 LLD；这属于本项目对上游最佳努力路径的
  适配，不代表 Chromium 官方支持在 macOS 上构建完整 Android 客户端。
- 后者新增可选 GN 参数 `android_clang_resource_dir`，指向所选 NDK 的
  `toolchains/llvm/prebuilt/<host>/lib/clang/<version>` 目录。Android 的显式
  compiler-rt 依赖与链接器驱动都使用这个目录；固定 Clang 编译器及其编译头文件
  保持不变。参数为空时保留上游行为，非 Android 工具链也不受影响。

本项目 Android 固定 **NDK 30.0.16248370（r30）**，与 APK 使用相同的 NDK。
compiler-rt 的 resource 目录由该 NDK 的 `clang -print-resource-dir` 查询，
不再硬编码 Clang 主版本号。Android 构建须同时保持 `use_custom_libcxx=false`、
`use_custom_libcxx_for_host=false`、`use_custom_libunwind=false`，使用同一 NDK 的
sysroot 和 C++ 运行库；最终由 APK 的 `c++_static` 合入 `libmain.so`。
`v8_monolithic_for_shared_library=true` 用于将 V8
静态库链接进 APK 的原生共享库。Mac 宿主保留 Apple 链接器的兼容补丁。

固定 V8 工具链现为 Clang 24（`llvmorg-24-init-7747-g62397f8b`，修订 67）。
macOS 最低目标使用上游 GN 的 `mac_deployment_target` 与
`mac_min_system_version` 同步配置为 **13.3**，显式设置更高版本时跟随宿主配置。
V8 新版 Debug 诊断使用 `std::format`；系统 libc++ 的浮点 `to_chars` 从 macOS 13.3
起可用，上游默认 13.0 会被 SDK 的可用性检查拒绝。保留可用性检查及官方格式实现，
不通过宏取消诊断或自行替换标准库；Android 的 macOS 宿主工具同样遵守该下限。
在 macOS SDK 27 上，不带任何自有强制头、以 C++20 和 `-Werror` 编译数学头及实际
`os_signpost_event_emit` 调用通过，且编译器已支持 `stack_protector_ignore`；
因此删除 `macos-system-math.patch` 和 `macos-signpost-attribute.patch`。
`macos-system-lto-library.patch` 仍保留：同版 Clang 直接调用系统链接器并启用
`-fatal_warnings` 链接最小程序时，仍因下载包缺少 `libLTO.dylib` 失败。

NDK r30 已提供 C++20 `std::atomic_ref`，因此删除旧的
`android-atomic-ref.patch` 和 `android-simdutf-atomic-ref.patch`。
V8 与 simdutf 直接编译上游标准实现；不再为旧版 libc++ 扩大自有原子操作兼容层。

`android-external-toolchain.patch` 应用于上述固定 V8 提交的根目录 `DEPS`，须在
`gclient sync` 前应用。它新增默认开启的 `checkout_android_toolchain`，仅控制
Android SDK 和 NDK 的 CIPD 包；`.gclient` 保持 `target_os = ["android"]`，同时在
solution 的 `custom_vars` 设置 `"checkout_android_toolchain": False`，即可使用项目
外部的 SDK/NDK，并保留 Android 原生源码依赖。固定 gclient 的 CIPD 路径不会通过
`custom_deps` 的路径级 `None` 禁用，不能依赖该方式排除这些二进制包。

仅构建原生 V8 时，还可在 `custom_deps` 中排除只用于 Android 测试工具的 Git 依赖
`v8/third_party/android_platform`、`v8/third_party/colorama/src`、
`v8/third_party/requests`。`android_platform` 的目标引用受
`build_with_chromium` 控制，另两项只是测试运行时的数据依赖。必须保留
`v8/third_party/catapult`：即使关闭 Java 模板，`build/android/BUILD.gn` 仍会加载
其 `tracing:convert_chart_json` 目标。也必须保留
`v8/third_party/cpu_features/src` 的固定提交；Android zlib 的 `ndk_compat` 目标会
实际编译它。完整测试工具链需要重新启用对应依赖。

`cmake/dependencies/prepare_v8.cmake` 的构建副本位于
`ENGINE_V8_BUILD_ROOT/source/v8/`；桌面与 Android 使用各自的构建根目录。
补丁按所属 Git 根目录应用到该副本自身、其中的 `build/`、
`third_party/partition_alloc/`，不会修改
`third_party/v8/`。缓存命中时跳过已完成的准备工作；只有需要重新同步依赖时，才
撤销已知补丁、同步固定依赖并重新应用。Android 的 `DEPS` 工具链补丁在同步前
提前应用，以排除外部提供的 SDK/NDK 下载。重复构建检测并保留已应用状态。
补丁不适用时明确报错，不重置其他本地改动。
更新 V8 固定提交时需核对全部补丁；上游提供等价 CRT 构建参数后移除 CRT 补丁。

固定 GN 的 `complete_static_lib` 将 Rust rlib 和 Clang 编译器运行时归档列为隐式依赖，
不会将其装入原始 `v8_monolith`。构建后 `cmake/dependencies/package_v8.cmake` 从生成规则读取精确
依赖集合，保留原库，并扁平合并成供 CMake 导入的 `engine_v8.lib`（其他平台为
`libengine_v8.a`），保留 Temporal 功能。Windows 使用固定工具链的 `lld-link /lib`，
其他平台要求同一工具链中存在 `llvm-ar`，通过 MRI `ADDLIB` 展开成员；工具或归档缺失
时明确报错。隐式归档路径按 Ninja 的转义规则解析，支持含空格、美元符号和冒号的
路径。各平台的实际构建和运行验证范围以 [测试记录](../../../docs/testing.md) 为准。

2026-11-04 对上述固定 V8、Chromium build 和 PartitionAlloc 快照逐项及连续检查
全部补丁的 `git apply --check`。该检查只说明补丁与源码匹配；macOS、Windows、
Android 的编译和运行结果仍分别按测试记录标注，不把补丁匹配当作平台验收。
