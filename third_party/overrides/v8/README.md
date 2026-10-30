# V8 构建副本覆盖

`windows-dynamic-crt.patch` 适用于 V8 `DEPS` 固定的 Chromium build 提交
`81330a6e45719ab8c0d22e5548afbedb5482b068`。

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
`13c7e3135187c1b0c7344e42529fbc27ba0e47f1`。此版本的
`src/wasm/wasm-code-manager.h` 仍调用 C++20 已弃用的 `std::atomic_load(shared_ptr*)`；
MSVC STL 14.44 的 STL4029 警告在 V8 的 `/WX` 下导致编译失败。补丁仅在 V8 内部
`toolchain` 配置的 Windows 且使用 MSVC STL 时定义该特定诊断的抑制宏；其余警告和
`/WX` 保持开启，不向引擎目标传播，也不改变原子访问实现。上游替换这些弃用调用后移除。

`windows-debug-quarantine.patch` 适用于 V8 `DEPS` 固定的 PartitionAlloc 提交
`db8446987dfff3cfc0c100b7d58e6a404ef639eb`。MSVC 的 Debug STL 在构造空 `vector`
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

本项目 Android 构建须同时保持 `use_custom_libcxx=false`、
`use_custom_libcxx_for_host=false`、`use_custom_libunwind=false`，使用同一 NDK 的
sysroot 和共享 C++ 运行库；`v8_monolithic_for_shared_library=true` 用于将 V8
静态库链接进 APK 的原生共享库。Mac 宿主仍需现有的 Mac SDK 兼容补丁。

`android-atomic-ref.patch` 应用于上述固定 V8 提交的根目录；
`android-simdutf-atomic-ref.patch` 应用于其 `DEPS` 固定的 simdutf 提交
`acd71a451c1bcb808b7c3a77e0242052909e381e`，根目录为 `third_party/simdutf/`。
NDK r27 的 libc++ 尚未提供 `std::atomic_ref`。两个补丁只在 Android 且标准库未
声明 `__cpp_lib_atomic_ref` 时，用 Clang `__atomic_*` 内建操作访问原有整数对象，
保持 V8 原子操作的返回值、成功/失败内存序和共享缓冲区语义，不改变 NDK 头文件
或 C++ 运行库，也不把普通对象转换为 `std::atomic` 对象。simdutf 的共享缓冲区
Base64 API 仍启用，原有算法、分块和对齐处理保持不变；字节和 64 位操作另检查
平台可无锁执行。其他平台继续使用原 `std::atomic_ref` 路径。上游支持该标准库
或项目整体升级到提供 `atomic_ref` 的 NDK 后复核并移除这两项兼容补丁。

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

`cmake/prepare_v8.cmake` 的构建副本位于
`ENGINE_V8_BUILD_ROOT/source/v8/`；桌面与 Android 使用各自的构建根目录。
补丁按所属 Git 根目录应用到该副本自身、其中的 `build/`、
`third_party/partition_alloc/` 或 `third_party/simdutf/`，不会修改
`third_party/v8/`。缓存命中时跳过已完成的准备工作；只有需要重新同步依赖时，才
撤销已知补丁、同步固定依赖并重新应用。Android 的 `DEPS` 工具链补丁在同步前
提前应用，以排除外部提供的 SDK/NDK 下载。重复构建检测并保留已应用状态。
补丁不适用时明确报错，不重置其他本地改动。
更新 V8 固定提交时需核对全部补丁；上游提供等价 CRT 构建参数后移除 CRT 补丁。

固定 GN 的 `complete_static_lib` 将 Rust rlib 和 Clang 编译器运行时归档列为隐式依赖，
不会将其装入原始 `v8_monolith`。构建后 `cmake/package_v8.cmake` 从生成规则读取精确
依赖集合，保留原库，并扁平合并成供 CMake 导入的 `engine_v8.lib`（其他平台为
`libengine_v8.a`），保留 Temporal 功能。Windows 使用固定工具链的 `lld-link /lib`，
其他平台要求同一工具链中存在 `llvm-ar`，通过 MRI `ADDLIB` 展开成员；工具或归档缺失
时明确报错。隐式归档路径按 Ninja 的转义规则解析，支持含空格、美元符号和冒号的
路径。各平台的实际构建和运行验证范围以根 `README.md` 为准。
