# V8 构建副本覆盖

`windows-dynamic-crt.patch` 适用于 V8 `DEPS` 固定的 Chromium build 提交
`81330a6e45719ab8c0d22e5548afbedb5482b068`。

该上游配置在非 component 的 Windows 桌面构建中默认使用静态 CRT，而本项目的
`x64-windows` vcpkg 依赖和 CMake 目标使用动态 CRT。补丁仅将默认 CRT 配置替换为
上游已有的 `dynamic_crt`，按 Debug / Release 分别采用 `/MDd` / `/MD`。
这是嵌入方 ABI 配置，不代表已报告的上游缺陷。

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

`cmake/prepare_v8.cmake` 按各补丁所属 Git 根目录，在忽略的
`build/v8/source/v8/build/`、`build/v8/source/v8/` 和其中的
`third_party/partition_alloc/` 副本中分别应用补丁，
不会修改 `third_party/v8/`。重复准备时先仅撤销这些已知补丁，再同步固定依赖并重新
应用；重复构建检测并保留已应用状态。补丁不适用时明确报错，不重置其他本地改动。
更新 V8 固定提交时需核对全部补丁；上游提供等价 CRT 构建参数后移除 CRT 补丁。

固定 GN 的 `complete_static_lib` 将 Rust rlib 和 Clang 编译器运行时归档列为隐式依赖，
不会将其装入原始 `v8_monolith`。构建后 `cmake/package_v8.cmake` 从生成规则读取精确
依赖集合，保留原库，并扁平合并成供 CMake 导入的 `engine_v8.lib`（其他平台为
`libengine_v8.a`），保留 Temporal 功能。Windows 使用固定工具链的 `lld-link /lib`，
其他平台要求同一工具链中存在 `llvm-ar`，通过 MRI `ADDLIB` 展开成员；工具或归档缺失
时明确报错。GNU/macOS 打包分支尚未实机验证。
