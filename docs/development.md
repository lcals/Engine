# 开发与第三方依赖

## 跨平台构建

根目录 `CMakePresets.json` 是 configure、build、test 的共同入口，推荐 `ninja-debug` / `ninja-release` workflow 将三步串联，环境安装与缓存说明统一见根 [README](../README.md#构建与测试)。原 `debug` / `release` 保留平台默认生成器。单配置生成器使用 `CMAKE_BUILD_TYPE`，多配置生成器由 build / test preset 的 `configuration` 选择同名配置。

平台专有的编译参数统一放在 `cmake/project_options.cmake`；只对本项目目标设置，不把警告参数传播给第三方依赖。公开的 C++20 要求通过目标依赖传递给使用方。

如需显式选择编译器或生成器，在一个新的构建目录配置，避免复用不同工具链的 CMake 缓存，并将 vcpkg 工具链一并传入：

```sh
cmake -S . -B build/custom -G Ninja -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE=Debug -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
cmake --build build/custom
ctest --test-dir build/custom --output-on-failure --no-tests=error
```

此示例需要本机已有 Ninja 和 Clang；使用 MSVC + Ninja 时，从 Visual Studio Developer PowerShell 执行并改用 `cl`。机器专属设置可以写入未纳入版本管理的 `CMakeUserPresets.json`。

GitHub CI 验证 Linux GCC、Linux Clang、Windows MSVC 和 macOS AppleClang 的 Debug / Release 构建与测试，并在 Debug 作业解析全部规划依赖的 vcpkg feature；可以在仓库 Actions 页面查看每次实际结果。

## 第三方依赖

根目录 `vcpkg.json` 按用途划分可选 feature；`builtin-baseline` 和 `overrides` 固定 vcpkg 版本。默认 `debug` / `release` 构建最小运行时及其系统依赖；`editor-deps` 增加编辑器与资源管线，`engine-debug` / `engine-release` 再增加 V8。各系统通过真实 API 调用参与编译和链接，依赖集成测试实例化所有启用的系统；具体功能仍分阶段启用。

| 分组 | vcpkg feature | 依赖 |
| --- | --- | --- |
| 游戏运行时 | `runtime` | SDL3、bgfx、flecs、Jolt Physics、Recast Navigation、RmlUi、FlatBuffers、Basis Universal |
| 桌面编辑器 | `editor` | 项目系统所用 toml++、Dear ImGui（docking 与 SDL3 绑定） |
| 编辑器与离线资源管线 | `asset-pipeline` | OpenUSD、Taskflow、SQLite、efsw、FlatBuffers、toml++、Basis Universal、meshoptimizer、bgfx 编译工具（shaderc、texturec、geometryc） |

运行时还通过固定提交的 Git 子模块引入 ozz-animation 和 V8。ozz-animation 作为 CMake 子项目接入，关闭工具、示例与上游测试；V8 使用独立 GN 工作副本，以 `ENGINE_ENABLE_V8` 显式启用准备、编译和 `engine::v8` 导入目标，命令与本机验证范围统一见根 [README](../README.md#v8-依赖)。固定的 vcpkg baseline 中，V8 port 不支持 macOS 与 Linux，因此不能作为跨平台运行时依赖。

桌面依赖 Preset 面向 Windows、macOS、Linux；Android 使用 `arm64-android-engine` overlay triplet 与独立安装目录。固定 baseline 的 `basisu` port 原本排除 Android / ARM，本仓库 overlay 增加 macOS ARM64 和 Android ARM64 支持；Android 关闭宿主工具但保留库。SDL3 在 Android 使用共享库并安装同版 Java 入口，其他运行时依赖使用 PIC 静态库。维护与验证说明见根 [README](../README.md#本地验证记录)。OpenUSD 仅在桌面资源管线中使用，不进入游戏运行时。
Basis Universal 当前通过同一 port 提供编码与转码能力；实现运行时目标时需只链接所需的转码部分，并核对最终产物大小。

需要验证依赖时先运行 `git submodule update --init`，再按需使用 `runtime-deps`、`asset-pipeline-deps` 或 `editor-deps` 的 configure / build / test Preset。编辑器 Preset 同时启用三个 feature；构建及 CTest 会验证相应系统与真实依赖的编译、链接和基本调用。

新增 vcpkg 依赖时，将其放入 `vcpkg.json` 对应 feature，并在 `third_party/` 中查找包，再链接到实际使用的 CMake 目标。依赖的 CMake target 名称以该包的 vcpkg usage 输出为准。例如：

```cmake
# third_party/CMakeLists.txt
find_package(example CONFIG REQUIRED GLOBAL)

# 根 CMakeLists.txt
target_link_libraries(engine_core PRIVATE example::example)
```

仅实现内部使用的依赖选择 `PRIVATE`；公开头文件需要的依赖选择 `PUBLIC`。没有可用跨平台 vcpkg port 时，将上游库固定为 Git 子模块，在 `third_party/` 接入。不要把构建产物放入 Git。

### 上游依赖的本地补丁

有上游问题时，补丁保存在仓库中，不直接修改依赖的检出目录或 vcpkg 安装目录。

- vcpkg 包：在 `third_party/overlay-ports/<port>/` 放置该 port 的完整定义及补丁，并在 `portfile.cmake` 中用 `PATCHES` 应用。根目录 `vcpkg-configuration.json` 已启用此目录。覆盖文件变化会使下一次 CMake 构建先重新配置，vcpkg 按新的 port 内容重建安装包。具体约定见 [overlay-ports/README.md](../third_party/overlay-ports/README.md)。
- ozz-animation：在 `third_party/overrides/ozz-animation/` 放置与上游源码相对路径一致的替换文件。CMake 从固定 Git 提交准备源码副本；输入变化或副本缺失时才重新准备，未改变内容保留时间戳，不修改 Git 子模块。具体约定见 [overrides/README.md](../third_party/overrides/README.md)。
- V8：`cmake/prepare_v8.cmake` 在忽略的构建目录准备固定版本的 gclient 工作副本，并在 GN 生成前应用 `third_party/overrides/v8/` 中的补丁；原子模块只读。Windows 兼容补丁的适用版本与移除条件见 [V8 覆盖说明](../third_party/overrides/v8/README.md)。

新增补丁时记录其上游问题和移除条件；上游修复后删除本地覆盖并重新验证。

## 测试与格式

`tests/` 使用 CTest 运行库单元测试和 CLI 测试。测试使用普通失败返回值，在 Release 中同样生效；新增业务通过正式接口验证。

`.editorconfig` 统一文本编码与换行，`.clang-format` 提供 C++ 格式配置。构建目录、IDE 缓存和机器专属 preset 已由 `.gitignore` 排除。
