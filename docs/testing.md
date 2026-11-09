# 测试与验证

只验证当前修改直接影响的行为。日常使用少量基础检查和现有模块用例；不因一次修复扩展全仓测试，不将性能采样当作每次提交的必经步骤。

## 按修改范围验证

| 修改 | 必要验证 |
| --- | --- |
| 文档、类图措辞 | 改动页的本地链接、围栏与 `git diff --check`；不构建引擎 |
| CMake、测试登记 | 对受影响配置重新生成，查看 `ctest -N` 和目标；不因此运行全部测试 |
| Schema 分层与生成 | 按层生成 C++ / TypeScript，检查目标依赖方向，选择对应导入及跨语言用例 |
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

## 镜头与角色模块验证

2026-11-09，macOS arm64 / Metal / QuickJS。使用继承根 `ninja-engine-debug`、`ninja-engine-release` 的本地预设，Ninja 与 ccache；`editor_app`、`engine_app`、`mcp_app`、`asset_build_app` 及受影响模块测试目标的 Debug / Release 构建通过。Debug 仅编译，未运行测试。

Release 的 13 项 CTest 全部通过（9.06 秒）：`engine.character_motor`、`engine.mcp_registry`、`engine.scene`、`engine.prefab`、`engine.plugin_selection`、`engine.architecture_boundaries`、`engine.architecture_rules`、`resource.codecs`、`engine.camera`、`engine.character_controller`、`engine.character_host`、`editor.character_controller`、`engine.control_script`。

相机用例覆盖目标失效、失败不消费触发器、参数原子修改、混合与四元数旋转、约束冲突、碰撞收缩和延迟恢复。角色用例覆盖多实例、固定步动作缓冲与到期、加减速、控制权独占、外力保留、蹲起净空、暂停／禁用／传送、观察增量与速率，以及实际姿态向镜头参数的映射。编辑器与脚本用例覆盖完整设置、撤销、瞬态输入、BigInt 身份、可编辑配置快照、旧字段拒绝和失效句柄。

关闭全部运行时／编辑器插件及脚本运行后，独立 Release 配置中的 `editor_app`、`engine_app` 编译链接通过；未创建 camera、character_controller、script 插件目标，运行宿主未链接相机和角色插件实现。插件选择用例同时检查依赖闭包与条件装配。

公开范例 TypeScript 构建及 `launcher_test.mjs` 的 8 项检查通过。实际编辑器窗口使用 `projects/showcase` 源资源导入缓存，逻辑尺寸 960×540、Metal 后缓冲 1920×1080；通过真实菜单按钮进入角色庭院，确认角色就绪、接地和镜头 ready。调用角色镜头接口切换第一／第三人称，截图确认第一人称隐藏自身身体、保留其他角色与自身阴影；随后恢复第三人称并正常关闭测试编辑器。项目配置、控制方式与复现入口见[角色与镜头范例](../projects/showcase/docs/character-control.md)。

此结果不包含实体手柄操作、移动平台场景验收、Windows／Android／iOS、V8 后端、旧版 macOS 发行验证或性能采样，不代表所有范例均已实窗验收。场景／预制体只接受版本 3，旧二进制需通过当前源描述重新导入和打包。

## 渲染性能基线

2026-11-08，Windows x64 / Release / DirectX 12 / QuickJS，RTX 4070 Laptop，驱动 `31.0.15.4601`；1280×720、MSAA 1、VSync 关闭、Tracy OFF。优化前源码为 `5070daf9`，实现和当时完整夹具见 `ec7d74d7`。

主场景为 4,096 个共享网格/材质的静态对象；120 稳定帧后采集 600 帧，交替执行三对。下表为 render 调用的 P50 / P95 / P99（ms），包含提交与等待，不是纯 CPU 耗时。

| 组 | 优化前 | 优化后 |
| --- | --- | --- |
| 1 | 199.535 / 211.465 / 227.562 | 19.921 / 21.162 / 23.710 |
| 2 | 198.367 / 210.198 / 223.172 | 20.139 / 22.801 / 24.569 |
| 3 | 193.828 / 205.472 / 213.961 | 19.885 / 21.668 / 24.924 |

主视图始终为 4,096 可见对象、4,096 实例、524,288 三角形，全设备 draw 为 9。GPU P50 为 2.4–3.2 ms。相对改善约 90%，但 render 约 20 ms 仍超过 60 FPS 的整帧预算；CPU 分段诊断见下节。

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

### CPU 分段诊断

基于 `cbb190b8` 加入内部 Tracy 分段，沿用上述 Windows/DX12 设备与 shared 场景。30 稳定帧后采集 120 帧，各运行一次未连接对照和 Tracy 0.14.1 on-demand 采集；输出及源码差异、二进制指纹位于忽略目录 `build/performance/render-cpu-profile/`。

未连接时 render P50 为 19.972 ms，连接时为 20.908 ms，相差约 0.94 ms（4.7%）；这是采集扰动检查，不是优化收益。开始/结束消息限定的窗口包含完整 120 次 Render.frame，以及 119 个完整 FrameMark 间隔。下表使用同一窗口内各区间总耗时除以 120，列出每帧平均值；子区间不再加到父区间上。

| 区间 | 每帧平均（ms） | 含义 |
| --- | ---: | --- |
| `Render.frame` | 21.183 | render 调用总计 |
| `Render.collectFrame` | 16.497 | ECS 对象与帧数据收集，约占总计 78% |
| `Render.meshState` | 6.537 | 上项内部：网格、形变状态、世界/法线矩阵 |
| `Render.meshFlagsAndLighting` | 7.514 | 上项内部：组件标志、实例化资格、光图与 Probe 查询 |
| `Render.meshMaterials` | 0.189 | 上项内部：材质绑定归组 |
| `Render.collectView` | 2.275 | 视图可见性、LOD 与绘制项收集 |
| `Render.buildBatches` | 1.532 | 各绘制通道的批次组织合计 |
| `Render.instanceData` | 0.122 | 实例缓冲分配和数据填充合计 |
| `Render.shadowSubmit` | 0.587 | 阴影提交路径 |
| `Render.frameSubmit` | 0.022 | bgfx::frame 调用，含可能等待 |

总 draw 仍为 9，连接采样的 GPU P50 为 1.509 ms。bgfx 的 `waitSubmit` 平均 19.643 ms 是后端等待提交线程推进；`waitRender` 平均 0.001 ms 是提交线程等待后端完成，两者不能颠倒。GPU 与 CPU 使用异步时钟，不直接相减。

修正前的热点集中在帧/视图数据收集，两项约占 89%。当时 `collect_frame` 已查询 ECS 列，仍通过 Node/Component 句柄逐项读取并重复校验。此诊断没有拆出各 getter、资源查找及矩阵计算的独立占比，不能将 16.497 ms 全部归于某一个函数。

上述连接运行在写出结果后退出超时，所属进程在 90 秒超时后结束；退出阻塞尚未定位。采集已包含完整计时窗口及结束标记，可用于上述 CPU 诊断，不作为正常退出或生命周期验收。未连接对照正常退出。

当前实现改为在稳定阶段直接读取网格 Data、ComponentState、NodeRuntime、WorldTransform 与姿态 Runtime；每个视图只借用一次相机隐藏名单，无 Lightmap 引用时跳过绑定检查。资源准备身份、版本校验和已有烘焙有效性规则保留，公开句柄契约不变，详见[收集与失效规则](runtime/class-diagrams/render-submission.md#收集更新与失效)。

该修改在同一设备、分辨率与 shared 场景执行一对 Tracy OFF 对比，均为 30 稳定帧 + 120 计时帧；不与上方连接采集或历史长窗口直接拼接：

| render 调用耗时（ms） | 修改前 | 修改后 |
| --- | ---: | ---: |
| P50 | 20.443 | 3.799 |
| P95 | 21.722 | 4.087 |
| P99 | 22.339 | 4.201 |

P50 减少约 81.4%。两次均正常退出，HDR 输出逐字节相同，全部逐帧/逐视图提交计数、GPU 资源数量与已记录字节数相同：4,096 可见对象及实例、524,288 三角形、全设备 9 draw。源码差异、二进制指纹及原始输出位于忽略目录 `build/performance/render-ecs/`。受影响目标 Debug/Release 构建通过，仅执行现有 Release `engine.render_submission` 一次并通过；没有新增测试程序或扩展性能场景。未重新采集优化后的 Tracy 分段，3.799 ms 是受控场景的 render 调用耗时，不代表游戏整帧或全场景性能。

## 验证范围与限制

- 源码英文字符串与外部 Unicode 夹具已完成 macOS arm64 的受影响 Debug / Release 构建；Debug 未运行测试。Release 的 GUID、日志、存储、资源读取、UI、脚本、MCP、资源管线、场景跨语言及建模几何共 18 项 CTest 通过；环境准备、Android 资源打包和桌面导出的 55 项脚本检查通过，私有项目 TypeScript 与设置检查通过。`modeling.core` 在二十面球体（`icosphere`）的三角形 18、顶点角 2 处因 MikkTSpace 返回零切线失败，前面的箱体和平面基本体检查通过；该失败保留记录，不能据此宣称全部建模功能通过。本次引擎实现只修改文案，未修改几何或切线算法。
- Schema 分层已完成 Windows Debug / Release 构建，Release 的 `modeling.pipeline`、`editor.destruction_pipeline` 和 `engine.scene_schema_cross_language` 通过，编辑器 TypeScript 通过严格类型检查。架构检查覆盖生成数据目标的所属层级与依赖方向。
- 建模参数、物理尺寸 UV、边界查询及可撤销重建已完成 Windows Debug / Release 构建；Release 的 `modeling.core`、`modeling.geometry`、`modeling.commands`、`modeling.pipeline`、`editor.scene_persistence` 和 `engine.mcp_registry` 通过。复用现有用例，未运行 Debug 测试、实窗或性能采样；不据此认定画面质量与其他平台已验收。示例源数据与资源包状态见 [Showcase](../projects/showcase/docs/index.md#验证)。
- 渲染提交优化已完成受影响 Debug / Release 构建，Release 的 `engine.render_foundation`、`engine.scene_hdr_capture`、`engine.window_render`、`engine.render_submission` 通过；这些结果只代表当时 Windows / DirectX 12 配置。
- 当前没有最新提交的全仓、全平台通过结论。其他模块与平台的旧执行日志保留在 Git 和本地 `build/`，不能直接当作当前版本的验收。
- iOS 完整宿主与交付尚未完成；Android 模拟器、独立依赖探针和桌面源码验证不代表实体手机的功能或性能。
- 该性能基线未覆盖 Shader/PSO 预热、大场景完整首帧及跨平台性能；CPU 分段结果仅适用于上述受控静态负载。

结果只记录必要信息：修改、配置、实际执行的目标/用例、结果、尚未验证的边界。更新本节或所属模块的现状，不再按每次会话追加一章。
