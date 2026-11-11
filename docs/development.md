# 开发与维护

[首页](../README.md) · [构建、测试与交付](building.md) · [验证范围与限制](testing.md) · [协作约定](../AGENTS.md)

本页维护源码分层、第三方接入、CI 和格式约定。环境安装、根 Presets、依赖分组、交付目录及构建选项统一见[构建文档](building.md)。

## 源码与文档分层

`include/engine/` 与 `src/engine/` 使用一致的 `runtime/`、`editor/`、`plugins/runtime/`、`plugins/editor/` 目录。运行时基础系统及公共工具归 runtime；编辑器主类、项目、命令、任务及导入管线归 editor；资源依赖图、脚本执行、QuickJS/V8 适配及共享绑定归运行时插件；完整建模（含纯几何）、完整 MCP（含协议）、资源构建/同步归编辑器插件。平台入口位于 `platforms/<platform>/`。

目标传递 `include/engine/` 作为搜索根，自有头文件使用 `#include "runtime/engine.hpp"`、`#include "editor/project/project_system.hpp"` 等带层级路径；第三方 API 保留标准包含路径。UTF-8 路径转换由 [PathUtils](runtime/class-diagrams/path-utils.md) 复用，路径安全及规范化检查仍由调用者完成。运行时 TypeScript 声明统一位于 [include/engine.d.ts](../include/engine.d.ts)。

模块文档与独立类图分别归入 `docs/runtime/`、`docs/editor/`、`docs/plugins/runtime/`、`docs/plugins/editor/`；统一入口是[类图索引](class-diagrams/index.md)，跨层关系见[完整引用关系](class-diagrams/references.md)。新增类型或移动职责时同步更新所属图及引用链接。

## 目录

所有受版本管理的目录均使用小写；`CMakeLists.txt` 等标准文件名保持原样。

```text
.
├── .github/workflows/  # Windows、Linux、macOS 的构建与测试 CI
├── cmake/              # install/dependencies/plugins/core/editor/platforms 构建分工
├── docs/               # 总说明及 runtime/editor/plugins/{runtime,editor} 文档与类图
├── include/engine.d.ts  # 运行时虚拟 engine 模块的类型声明
├── include/engine/     # runtime/editor/plugins/{runtime,editor} 公开头文件
├── include/schemas/    # runtime/ 与 editor/ 的 FlatBuffers 跨语言数据模型
├── platforms/          # 桌面路径实现及 Android 入口、Gradle 与原生打包配置
├── projects/showcase/  # 共用设置、缓存，内含方块、公寓与动画庭院场景
├── references/         # 本地参考文件，内容不纳入 Git
├── scripts/            # 宿主安装、TS 构建、MCP 客户端与 Android 打包工具
├── src/engine/         # 与 include/engine 对应的基础与插件实现
├── tests/              # C++ 与命令行测试
└── third_party/        # Git 子模块、vcpkg overlay 与本地补丁
```

`references/` 用于本地放置参考文件，内容不纳入 Git；文档不记录其中的私有内容。构建目录、IDE 缓存和机器专属 preset 已由 `.gitignore` 排除。私有项目、付费素材和私有参考按[协作约定](../AGENTS.md)保持本地隔离；公开上游依赖保留固定版本及许可。

## 插件与 CMake 分工

CMake 按 `install/`（环境、缓存和运行数据安装）、`dependencies/`（查找及准备上游）、`plugins/`（插件声明与依赖闭包）、`core/`（运行时核心）、`editor/`（编辑器基础）、`platforms/`（宿主与平台目标）分工。根文件只组织配置与入口，不重新集中登记每个插件的源码。

每个插件在 `cmake/plugins/<runtime|editor>/<name>.cmake` 声明自己的目标、源码和依赖。`ENGINE_RUNTIME_PLUGINS` 与 `ENGINE_EDITOR_PLUGINS` 指定请求启用的插件，注册器解析依赖闭包；未选择且不在闭包中的插件不创建目标。宿主按实际目标装配，编辑器依赖不能由核心反向引入。插件类别与依赖关系见[插件说明](plugins/plugins.md)。这是构建期可选装配，不提供动态库热插拔。

平台专有的编译参数统一放在 `cmake/core/target_options.cmake`；只对本项目目标设置，不把警告参数传播给第三方依赖。公开的 C++20 要求通过目标依赖传递给使用方。显式选择编译器或生成器时使用新的构建目录，命令见[自定义工具链](building.md#自定义工具链)。

## 第三方依赖维护

依赖功能分组、共享安装与缓存策略见[依赖与缓存](building.md#依赖与缓存)。新增依赖必须进入实际使用目标的编译、链接和基本调用验证，不能仅登记或 `find_package` 成功。

新增 vcpkg 依赖时，将其放入 `vcpkg.json` 对应 feature，并在 `cmake/dependencies/` 中查找包，再链接到实际使用的 CMake 目标。依赖的 CMake target 名称以该包的 vcpkg usage 输出为准。例如：

```cmake
# cmake/dependencies/runtime.cmake
find_package(example CONFIG REQUIRED GLOBAL)

# cmake/core/systems.cmake：链接到实际使用此依赖的具体系统目标
target_link_libraries(engine_resource PRIVATE example::example)
```

仅实现内部使用的依赖选择 `PRIVATE`；公开头文件需要的依赖选择 `PUBLIC`。没有可用跨平台 vcpkg port 时，将上游库固定为 Git 子模块，源码放在 `third_party/`，构建接入放在 `cmake/dependencies/`。不要把构建产物放入 Git。

### 当前固定版本

2026-11-04 的稳定版本核查以各上游正式 release/tag 为准；V8 采用已发布 Chrome Stable 的精确 `DEPS`，没有正式发布的算法库采用固定上游提交。vcpkg baseline 为 [`8450b1326545e5e933c36d698c74df072ffca6d0`](https://github.com/microsoft/vcpkg/tree/8450b1326545e5e933c36d698c74df072ffca6d0)；下表说明直接依赖，完整传递依赖由该 baseline 与 [manifest](../vcpkg.json) 的 feature 闭包锁定。版本选择不等于平台验收，实际结果单独见[测试文档](testing.md)。

| 用途 | 固定依赖 | 来源与维护 |
| --- | --- | --- |
| 基础格式与 JSON | fmt 12.2.0、nlohmann-json 3.12.0 | baseline；保留该版的 port 修复。 |
| 异步日志 | Quill 13.1.0 | baseline；LogSystem 通过官方 Sink 扩展点保留无异常文件/平台输出契约。 |
| 平台窗口与输入 | SDL 3.4.20 | overlay；含同版 Android Java glue 与 Windows 文件共享修复。 |
| 渲染及图像工具 | bgfx.cmake 1.167.9605-606（API 167） | overlay；bgfx/bx/bimg/shaderc 来自同一正式归档。 |
| Shader 交叉编译 | glslang 16.4.0、SPIRV-Cross/Headers/Tools 1.4.363.0 | baseline；旧 PointSize 查询兼容 overlay 已删除。 |
| ECS 与任务 | Flecs 4.2.0、oneTBB 2023.2.0 | overlay；TBB 具体 task 类型销毁补丁已由上游覆盖。 |
| 物理与导航 | Jolt 5.6.0、Recast Navigation 1.6.0 | baseline。 |
| UI 与编辑器界面 | RmlUi 6.3、Dear ImGui 1.93.0 docking | baseline；仅启用实际需要的绑定。 |
| 字体离线解析 | FreeType 2.15.0 | baseline；运行包仍消费生成的字体资源。 |
| 纹理与浮点图像 | Basis Universal 2.50、TinyEXR 3.3.0 | overlay；保留 BasisU 移动端库构建及 stdio 输出适配。 |
| 资源跨语言模型 | FlatBuffers 25.12.19 维护发布 `v25.12.19-2026-02-06-03fffb2` | overlay；原生库与 host `flatc` 同版。 |
| 文件监听与目录数据库 | efsw 1.7.3、SQLite 3.54.1 | efsw overlay，SQLite baseline。 |
| 网格处理 | meshoptimizer 1.3、MikkTSpace `3e895b49` | Mikk 保留有来源的极小夹角数值补丁。 |
| 建模 | Manifold 3.5.4、earcut.hpp 3.2.4、Clipper2 2.0.1 | baseline。 |
| 性能采集 | Tracy 0.14.1 | baseline；仍按构建选项启用。 |
| 项目设置 | toml++ 3.4.0 | baseline；TOML 仍仅由编辑器读取。 |
| QuickJS 后端 | quickjs-ng 0.18.0 | overlay；保留无副作用模块预检契约。 |
| 可选 USD 导入 | OpenUSD 26.11.1 | overlay；迁移静态插件与安装目录适配，删除上游已覆盖的 OIIO 3 补丁。 |
| TS 工具及 JS 模型 | TypeScript 7.0.2、npm flatbuffers 25.9.23 | 项目 package-lock；npm 与原生 FlatBuffers 发布渠道独立。 |
| 动画 | ozz-animation 0.17.0，`744eb9d9` | Git 子模块；覆盖只应用到构建副本。 |
| V8 后端 | 15.7.112.23，`440c1c46` | Chrome Stable 157.0.8103.38 的 shipping `DEPS`；固定其完整工具链。 |
| 特效与破坏 | Effekseer 1808，`b5700082`；Blast 5.1.0，`636e7eb4` | 当前正式稳定版，保留必要平台补丁。 |
| FBX 源文件解析 | ufbx 0.23.1，`26a482ae` | 当前正式稳定版，Git 子模块。 |
| 无正式发布的固定算法库 | xatlas `f700c779`、efkbgfx `7d89c3c3`、Transvoxel `51a494f0`、lightmapper `4fd3bf4e` | 核查时均等于上游 HEAD；Git 仍固定精确提交，不在构建时追踪分支。 |
| Android 工具链 | Gradle 9.8.2、AGP 9.5.0、NDK r30、Build Tools 37.0.0 | [平台说明](runtime/android.md)记录官方来源和 wrapper 哈希；min/target API 策略保持。 |

部分版本没有改变，是因为该稳定渠道尚无更新：BasisU 2.50、bgfx API 167、nlohmann-json 3.12.0、Recast 1.6.0、Tracy 0.14.1、Manifold 3.5.4、earcut.hpp 3.2.4、toml++ 3.4.0 和传递依赖 Boost 1.92.0、zstd 1.5.7 已为此次核查的最新稳定版。MikkTSpace 无 tag/release 且上游 HEAD 未变。npm flatbuffers 25.9.23 仍为 npm `latest`，不把未发布的原生源码 package 版本冒充 npm 发布。

overlay 的具体归档 SHA512、来源许可与补丁去留见 [overlay 说明](../third_party/overlay-ports/README.md)和 [vcpkg 许可](../third_party/notices/vcpkg.md)。引入时固定的渲染范例或论文来源保留原始提交；升级运行依赖不改写算法来源，也不自动接入新上游示例。

### 上游依赖的本地补丁

有上游问题时，补丁保存在仓库中，不直接修改依赖的检出目录或 vcpkg 安装目录。

- vcpkg 包：在 `third_party/overlay-ports/<port>/` 放置该 port 的完整定义及补丁，并在 `portfile.cmake` 中用 `PATCHES` 应用。根目录 `vcpkg-configuration.json` 已启用此目录。覆盖文件变化会使下一次 CMake 构建先重新配置，vcpkg 按新的 port 内容重建安装包。具体约定见 [overlay-ports/README.md](../third_party/overlay-ports/README.md)。
- ozz-animation：在 `third_party/overrides/ozz-animation/` 放置与上游源码相对路径一致的替换文件。CMake 从固定 Git 提交准备源码副本；输入变化或副本缺失时才重新准备，未改变内容保留时间戳，不修改 Git 子模块。具体约定见 [overrides/README.md](../third_party/overrides/README.md)。
- QuickJS：固定 quickjs-ng 0.18.0 port 位于 `third_party/overlay-ports/quickjs-ng/`，模块图预检扩展以补丁应用到构建副本；选择 V8 或关闭脚本时不安装该 feature。
- V8：`cmake/dependencies/prepare_v8.cmake` 在忽略的构建目录准备固定版本的 gclient 工作副本，并在 GN 生成前应用 `third_party/overrides/v8/` 中的补丁；上游子模块只读。各平台兼容补丁的适用版本与移除条件见 [V8 覆盖说明](../third_party/overrides/v8/README.md)。

新增补丁时记录其上游问题和移除条件；上游修复后删除本地覆盖并重新验证。

## CI

CI 仅通过 `workflow_dispatch` 手动触发。在 GitHub 仓库的 Actions 页面选择 `ci`，点击 Run workflow 并选择分支；提交、推送、创建或更新 PR、合并分支均不会自动运行 CI。助手只有在用户明确要求运行 CI 时才可手动触发。

工作流配置 Linux GCC、Linux Clang、Windows MSVC 和 macOS AppleClang 的 Debug / Release 构建，仅 Release 作业运行测试，并在 Debug 作业解析全部已选依赖的 vcpkg feature。配置支持范围不等于运行结果；在 Actions 页面查看每次实际结果，本地验证记录见[验证范围与限制](testing.md)。

## 测试约定

`tests/` 按所属模块归档，构建规则检查归 `tests/build/`，共用夹具归 `tests/support/`，项目行为测试留在项目内。普通构建和 Release 测试预设只包含 framework、guid_map、system 三项基础检查；其他测试显式指定目标和 `-R`，全量构建仅通过 `engine_tests`。选择方法见[测试文档](testing.md)。

只验证当前改动直接影响的行为，已通过的检查不因提交或文档整理重复执行。优先复用现有用例，不为每个修改点增加程序、脚本或基准场景；只有缺少真实契约或回归覆盖时才补充。Debug 仅编译受影响目标，实际测试用 Release。不为测试扩大正式接口，也不以基础三项通过冒充全引擎通过。

## 文档维护

文档以职责、契约、使用方法和限制为主，使用面向维护者的陈述，不记录对话、批准过程或“本轮/下一步”工作汇报。正式说明描述现有能力；尚未实现但需要保留的设计明确标注范围，不混作使用说明。

同一接口只在所属类图定义，使用指南链接该契约；索引仅负责导航。重复页面合并，已完成的迁移计划、旧接口说明和阶段报告删除，历史过程由 Git 保存。固定来源、版权许可、仍影响使用的限制不得随过程记录一起删除。

验证结果只保留必要的配置、结论、证据位置和未覆盖范围，集中更新测试文档；项目专属结果归项目文档。已有结论不因整理文档再次运行测试，不持续追加会话章节。

## 代码格式维护

格式整理覆盖仓库自有代码、测试、工具、平台入口，以及 third_party 中的自有 CMake 接入与 triplet。上游 Git 子模块、overlay portfile、移植的上游覆盖文件、补丁内容和构建生成物保持各自格式，不纳入此范围。语言规则由根 `.clang-format`、`.editorconfig`、`.cmake-format.json` 和 `pyproject.toml` 统一维护；C++ 使用 Allman 大括号、4 空格缩进及 100 列，并为控制语句保留大括号。

| 范围                                                                                      | 检查方式                                                                     |
| ----------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------- |
| `include/engine/`、`src/`、`tests/` 中的 C++ 与 `platforms/` 中自有 C++ / Java            | 将文件列表传给 `clang-format --dry-run --Werror`                             |
| `scripts/`、`tests/` 中的 Python                                                          | `python3 -m black --check scripts tests`                                     |
| 根 CMakeLists、`cmake/`、`tests/`、Android native_bundle、自有 third_party 接入与 triplet | 将这些自有 CMake 文件传给 `cmake-format --check`，排除 overlay、上游及生成物 |
| TypeScript / JavaScript                                                                   | 使用下方 Prettier 命令                                                       |
| 运行时和示例项目 Shader                                                                   | 按 `.editorconfig` 与现有 Shader 排版维护，并执行对应 Shader 编译            |
| `include/schemas/` 中的 FlatBuffers schema                                                | 4 空格、UTF-8、LF；通过 flatc 生成验证语法                                   |
| Android Gradle、Shell 与 PowerShell                                                       | 4 空格、UTF-8、LF、无行尾空白；保留各语言语法及现有行为                      |

```sh
prettier --check --tab-width 4 --print-width 100 include/engine.d.ts "projects/showcase/assets/scripts/**/*.ts" scripts/build_scripts.mjs tests/runtime/scene/scene_schema_bridge.ts
```

Black 26.10.1、cmakelang 0.6.13 与 Prettier 用于格式维护，不是引擎构建依赖；可在独立工具环境安装。格式检查不能替代命名、模块职责、返回值错误处理及资源释放顺序的代码审查，C++ 修改仍需 Debug / Release 构建和受影响的 Release CTest。

项目采用 [MIT License](../LICENSE)。
