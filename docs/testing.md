# 测试与验证

只验证当前修改直接影响的行为。日常使用少量基础检查和现有模块用例；不因一次修复扩展全仓测试，不将性能采样当作每次提交的必经步骤。

## 按修改范围验证

| 修改 | 必要验证 |
| --- | --- |
| 文档、类图措辞 | 改动页的本地链接、围栏与 `git diff --check`；不构建引擎 |
| CMake、测试登记 | 对受影响配置重新生成，查看 `ctest -N` 和目标；不因此运行全部测试 |
| C++ 实现 | 受影响目标 Debug / Release 构建；仅运行覆盖该行为的 Release 用例 |
| 脚本或资源 | 对应项目的类型检查、导入或实际使用路径，复用已有入口 |
| 渲染行为 | 选择一个相关的现有 GPU 用例；只有改到窗口、设备或多效果生命周期时才运行 `engine.window_render` |
| 性能优化 | 一个代表场景、一对同条件前后数据；出现波动、失败或结论不足时才追加采样 |

一次修改开始时选定最小验证集合；通过后不因整理文档、准备提交而重复执行。新增用例必须能捕获现有测试缺少的真实契约或回归；优先补入所属模块现有测试，不为每个修改点再建测试程序。不重复验证第三方库本身，不维护临时场景的全排列矩阵。

## 执行方式

构建环境与平台预设见[构建指南](building.md)。普通构建只默认编译 `engine_framework_test`、`engine_guid_map_test`、`engine_system_test`；Release 测试预设默认也只选择这三项。其他测试仍保留独立目标，由调用方按模块显式选择，不随普通引擎构建全部编译。

```powershell
# 基础检查：已有构建不必重复构建。
cmake --build --preset windows-static-release --target engine_framework_test engine_guid_map_test engine_system_test
ctest --preset windows-static-release

# 模块检查示例：-R 替换预设的默认集合，不额外叠加基础检查。
cmake --build --preset windows-static-debug --target engine_render_submission_test
cmake --build --preset windows-static-release --target engine_render_submission_test
ctest --preset windows-static-release -R '^engine\.render_submission$'

# 只查看选中的用例，不执行。
ctest --preset windows-static-release -R '^engine\.render_submission$' -N
```

其他平台使用对应 Release 预设。Debug 只编译，已移除默认 Debug 测试预设；只有用户明确要求时才直接指定 Debug 构建目录运行测试。GPU 用例仍须配置 `ENGINE_ENABLE_GPU_TESTS=ON`，需要真实桌面与图形设备，不以 Noop/headless 结果替代画面验收。

全量构建/运行仅用于明确要求的完整验收，不是日常流程：

```powershell
cmake --build --preset windows-static-release
cmake --build --preset windows-static-release --target engine_tests
ctest --preset windows-static-release -R '.*'
```

CI 仍仅手动触发，默认执行基础集合；跨平台或完整功能验收须明确范围。不能把默认三项通过报告成全引擎通过。

### 测试目录

| 目录 | 归属 |
| --- | --- |
| `tests/runtime/<module>/` | 核心、资源、场景、渲染等运行时模块 |
| `tests/editor/<module>/` | 编辑器宿主、项目、命令、场景保存和资源管线 |
| `tests/plugins/runtime/<plugin>/` | 资源图、脚本及其他运行时插件 |
| `tests/plugins/editor/<plugin>/` | MCP、建模、导入、烘焙和构建同步 |
| `tests/build/` | 构建、插件选择、交付与架构规则 |
| `tests/support/`、`tests/fixtures/` | 共用夹具 |
| `projects/showcase/tests/` | 公开项目自身的行为 |

登记入口为 [tests/CMakeLists.txt](../tests/CMakeLists.txt)。不为测试修改正式接口可见性，不提交运行结果或生成资源。

## 手动集成与 GPU 验证

- `engine.render_submission`：剔除、实例化、材质/Probe 分组、LOD、瞬态缓冲不足和阴影像素回归。
- `engine.scene_hdr_capture` / `engine.scene_hdr_transparency` / `engine.hdr_extension_capture`：按实际修改选择对应捕获或透明链路。
- `engine.window_render`：窗口、GPU 资源、多效果与生命周期集成，耗时较长，不作为所有渲染修改的默认补充项。
- `tests/plugins/editor/scene_bake/` 的独立 Python GPU 脚本：仅在修改相应烘焙算法时选取，不全部执行。
- `engine.plugin_selection`、架构规则、平台打包、V8 缓存与工具链检查：仅在对应构建/装配修改时运行；V8 检查仅登记于 V8 配置。

长时间运行、截图、MCP 交互和基准采样由明确任务触发，不加入默认 CTest。真实 UI 操作不能用直接调用脚本回调代替，源码或宿主替身检查也不能宣称真机/像素通过。

## 公开范例

公开项目入口、场景、资源来源和操作说明统一在 [showcase 文档](../projects/showcase/docs/index.md)。引擎文档不再重复维护逐场景节点数量、操作长表和每次验收日志。

通过编辑器启动、MCP 控制和退出的操作见[编辑器](editor/editor.md)与[MCP](plugins/editor/mcp.md)。运行前核对项目、运行目录和编辑器会话；只关闭本次测试拥有的进程。脚本后端、资源目录、分辨率和设备必须在结果中说明。

## 渲染性能基线

2026-11-08，Windows x64 / Release / DirectX 12 / QuickJS，RTX 4070 Laptop，驱动 `31.0.15.4601`；1280×720、MSAA 1、VSync 关闭、Tracy OFF。优化前源码为 `5070daf9`，实现和当时完整夹具见 `ec7d74d7`。

主场景为 4,096 个共享网格/材质的静态对象；120 稳定帧后采集 600 帧，交替执行三对。下表为 render 调用的 P50 / P95 / P99（ms），包含提交与等待，不是纯 CPU 耗时。

| 组 | 优化前 | 优化后 |
| --- | --- | --- |
| 1 | 199.535 / 211.465 / 227.562 | 19.921 / 21.162 / 23.710 |
| 2 | 198.367 / 210.198 / 223.172 | 20.139 / 22.801 / 24.569 |
| 3 | 193.828 / 205.472 / 213.961 | 19.885 / 21.668 / 24.924 |

主视图始终为 4,096 可见对象、4,096 实例、524,288 三角形，全设备 draw 为 9。GPU P50 为 2.4–3.2 ms。相对改善约 90%，但 render 约 20 ms 仍超过 60 FPS 的整帧预算；尚未取得 CPU 收集、组批与等待的分阶段证据。

当时的辅助场景各执行一对，结果只作方向与正确性检查：

| 场景 | 对象 | render P50 前 → 后（ms） | 全设备 draw 前 → 后 |
| --- | ---: | --- | --- |
| probes | 4,096 | 472.935 → 21.443 | 12 → 12 |
| mixed | 1,024 | 86.025 → 5.825 | 214 → 214 |
| dynamic | 1,024 | 88.363 → 5.783 | 214 → 214 |
| shadow | 4,096 | 240.019 → 23.549 | 20,595 → 28 |
| views | 1,024 | 105.211 → 6.184 | 295 → 295 |

Probe 使用 120 + 600 帧，其余辅助场景使用 30 + 120 帧。全部 8 对 HDR 图像完全相同；逐帧主视图计数相同，7 对具有逐 view 数据的结果也相同。mixed 优化后最大帧 37.414 ms，未剔除慢帧。首次同步准备从 20.691–26.146 ms 增至 61.590–67.919 ms；阴影瞬态缓冲从 1.3125 MiB 增至 2.5684 MiB。稳态 GPU 资源存量未增加；未采集 CPU 缓存容量/RSS，也未测完整冷启动，不宣称整体内存或启动收益。

原始二进制、指纹、逐帧 CSV、图像和核对结果保留在忽略的 `build/performance/render-submission/`；基线失败结果不计入有效样本。完整历史数据可从上述提交与本地原始结果查阅，不在当前文档重复逐次日志。

当前基准只保留上述 shared 负载，不再维护临时六场景矩阵。默认 4,096 对象、30 稳定帧、120 计时帧；仅在性能任务中显式运行，输出目录必须为新目录：

```powershell
.\build\windows-static-release\tests\engine_render_submission_test.exe --benchmark --objects 4096 --settle-frames 30 --frames 120 --output build/performance/render-submission/sample
```

比较时两版使用相同参数，不能把新短窗口直接与历史长窗口拼成严格对比。图像回读和 CSV 写盘在计时窗口外；计时期间不并行构建或跑其他测试。细分 CPU/GPU 原因使用[既有性能工具](runtime/performance.md)，不另写一套采样框架。

## 验证范围与限制

- 渲染提交优化已完成受影响 Debug / Release 构建，Release 的 `engine.render_foundation`、`engine.scene_hdr_capture`、`engine.window_render`、`engine.render_submission` 通过；这些结果只代表当时 Windows / DirectX 12 配置。
- 当前没有最新提交的全仓、全平台通过结论。其他模块与平台的旧执行日志保留在 Git 和本地 `build/`，不能直接当作当前版本的验收。
- iOS 完整宿主与交付尚未完成；Android 模拟器、独立依赖探针和桌面源码验证不代表实体手机的功能或性能。
- 该性能基线未覆盖 Shader/PSO 预热；大场景完整首帧、CPU 热点分解与跨平台性能仍未验证。

结果只记录必要信息：修改、配置、实际执行的目标/用例、结果、尚未验证的边界。更新本节或所属模块的现状，不再按每次会话追加一章。
