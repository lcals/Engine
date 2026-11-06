# 测试、公开范例与验证记录

本文集中说明公开测试项目的操作、可重复执行的检查及已有证据。所有命令从仓库根目录执行；环境安装、依赖准备及平台预设见[构建指南](building.md)，模块契约见[架构索引](architecture.md)与[类图索引](class-diagrams/index.md)。

验证记录只对注明的日期、平台、后端和范围负责。定向测试通过不表示全仓测试通过，历史实窗结果也不代表后续修改已跨平台验收。功能检查与性能验收分开；性能指标和采样口径见[性能文档](runtime/performance.md)。CI 仅手动触发。

## 执行方式

### 测试目录

引擎测试按被测模块归档；跨模块用例放在主测职责所属目录，目标名、CTest 名称及构建输出目录保持不变。

| 目录 | 内容 |
| --- | --- |
| `tests/runtime/<module>/` | 核心、资源、场景、渲染、物理、导航等运行时模块 |
| `tests/editor/<module>/` | 编辑器宿主、项目、命令、场景保存及资源管线 |
| `tests/plugins/runtime/<plugin>/` | 脚本、资源图、角色、群体、地形、破坏和特效插件 |
| `tests/plugins/editor/<plugin>/` | MCP、建模、各类导入、烘焙、UI 和构建同步插件 |
| `tests/build/` | 架构规则、依赖与插件选择、安装环境和平台交付检查 |
| `tests/support/`、`tests/fixtures/` | 共用代码夹具及源资源夹具 |
| `projects/showcase/tests/` | 公开范例的项目行为与真实 MCP 交互 |

`tests/CMakeLists.txt` 统一登记测试；共享头只作为测试目标的私有包含目录。Python 测试支持从仓库根直接执行，模块间复用通过明确导入连接。

### 构建、CTest 与脚本检查

准备好构建指南要求的宿主工具及依赖后，完整引擎使用根目录 Presets：

```sh
npm ci --prefix projects/showcase/settings/typescript
cmake --workflow --preset ninja-engine-debug
cmake --workflow --preset ninja-engine-release
```

Debug workflow 只做脚本准备和构建，Release workflow 另运行测试。已准备过且依赖未变时，分别使用对应预设配置、构建，测试执行 `ctest --preset ninja-engine-release --output-on-failure`。自 2026-11-03 起按用户要求，Debug 只确认编译成功，功能测试和实窗验收统一使用 Release；历史 Debug 验证记录保留，不再作为后续要求。不因普通回归清除共享依赖或缓存；测试集合随所选插件变化，不能用更小配置的结果代替完整配置。

showcase 的独立 TS 类型检查入口为：

```sh
node projects/showcase/settings/typescript/node_modules/typescript/bin/tsc \
  --project projects/showcase/settings/typescript/tsconfig.json --noEmit
```

`engine.scene_schema_cross_language` 在 ECS 迁移后已通过 Debug／Release 实跑；测试直接导入所需场景生成类型。全 SDK barrel 暴露的输入枚举生成问题仍见[已知失败与限制](#已知失败与限制)，未据此宣称输入生成器已修复。

### 架构契约修复验证（2026-11-05，Windows x64）

本轮同步[类图](class-diagrams/index.md)与实现：17 种组件的 Runtime 及材质槽状态移入内部头，公开句柄不再提供可变运行缓存入口；WorldSystem 通过生产者的只读验证函数消费通用变换驱动。活动相机移除后清除选择，保存拒绝被临时标记过滤的活动相机。任务完成委托按域轮转，部署按完整版本选择；编辑器目录提交先准备租约和登记，同步使用进程退出自动释放的 OS 锁。ProjectSystem 不再为依赖验证执行无业务用途的 TOML 操作。Animator 原生与 TS 公开方法分别统一到各自类图。

使用根 `windows-static-debug` / `windows-static-release` Preset、默认 QuickJS 后端与已启用的完整插件集合，保留已有依赖和编译缓存。Debug 受影响的四个正式宿主目标及其库构建通过；Release 完整构建通过，定向 CTest **41/41 通过**（157.46 秒），未运行 Debug 测试。覆盖范围包括：

- `engine.scene`、`engine.ecs_scene`、`engine.component_registry`、`engine.animation_rigging_events`：相机删除/重建与正式资源往返、临时相机保存拒绝、公开 Runtime 访问编译期约束、类型无关驱动及挂点即时失效。
- `engine.runtime_host`、`plugins.runtime.resource_graph`：单完成额度下的域间公平性、坏持久清单/缺实体/错误平台选择完整基线、完整外部版本独立启动、坏热重载保留活动 Run，以及加载器切换失效。
- `engine.editor_environment`、`engine.asset_sync`、`engine.project`：登记 I/O 失败保留资源代次、核心拒绝后清理候选、同步跨进程互斥及强制终止后的直接重试。
- 其他定向项覆盖资源编解码、任务、物理、音频、动画/脚本、预制体、字体/UI、角色、导航群体、地形、破坏与特效；`engine.window_render` 和 `engine.font_ui_render` 实际使用 Windows DX12，架构边界与插件选择检查通过。

核心回归可从以下命令重复执行；41 项定向运行的完整输出保存在忽略目录 `build/architecture-fix-ctest-release.log`，配置与构建日志为同目录的 `architecture-fix-*.log`。

```powershell
cmake --build --preset windows-static-debug --parallel 2 --target engine_app editor_app asset_build_app mcp_app
ctest --preset windows-static-release --output-on-failure -R '^(engine\.(runtime_host|scene|ecs_scene|component_registry|animation_rigging_events|editor_environment|asset_sync|project)|plugins\.runtime\.resource_graph)$'
```

本轮额外核对 Animator 的 25 个原生公开方法名、33 个 TS 成员与所属类图一致；改动文档的本地链接及 `git diff --check` 通过。结论限于 Windows x64 静态构建及上述 Release 用例，不代表其他平台设备已验收，也不替代全仓 CTest。

### 项目配置整理验证（2026-11-05，Windows x64）

项目设置统一为 `runtime-<模块>.toml`、`editor-<模块>.toml`，使用 `common` 与运行平台表；TypeScript 工程独立放在 `settings/typescript/`。所有当前支持的项目运行参数、编辑器帧率与导入并发必须显式配置，缺文件、缺字段、错误类型及非法范围均失败；配置链路不提供参数默认值。契约与示例见[编辑器配置](editor/configuration.md)和[运行预算](runtime/parallelism.md)。

本轮使用 `windows-static-debug/release` 根 Preset，保留依赖和增量缓存。Release 相关 CTest **46/46 通过**（140.90 秒），覆盖运行/编辑器配置、脚本、TS 工具定位、资源导入/打包、场景、动画制作以及真实 DX12 窗口、字体 UI 和角色宿主；官方 TypeScript `--noEmit` 通过。五目标平台生成的 JSON 均经原生完整字段校验，公共字段与目标平台覆盖生效，未携带其他平台或源配置分层。这是 Windows 上的配置生成校验，不代表其他平台设备验收。

Debug 受影响代码编译完成，五个宿主目标链接构建通过；Debug 编辑器与运行包已导出，仅做构建与产物核对。Release 编辑器与运行包已重新导出，分别实际启动并正常结束 8 帧；运行包进入菜单场景，输出 `settings/runtime.json` 为完整 Windows 参数。日志保存在忽略目录 `build/windows-static-release/config-style-*.log`。

### 真实窗口与 GPU 测试

普通 CTest 不要求图形桌面；窗口测试需显式开启 `ENGINE_ENABLE_GPU_TESTS`，并在有真实图形会话的机器串行运行。完整引擎示例：

```sh
cmake --preset ninja-engine-release -DENGINE_ENABLE_GPU_TESTS=ON
cmake --build --preset ninja-engine-release
ctest --preset ninja-engine-release --output-on-failure
```

窗口测试串行运行，避免多个实例抢占窗口。若只验证基础窗口，可使用最小 Release 预设：

```sh
cmake --preset ninja-release -DENGINE_ENABLE_GPU_TESTS=ON
cmake --build --preset ninja-release
ctest --preset ninja-release
```

- `engine.window_render`：真实后端、场景绘制、效果开关、窗口生命周期与 GPU 资源回收。
- `engine.font_ui_render`：多字体/Shader/图片、跨文档焦点、3D 文字、缩放与反复启停。
- `engine.input`：平台窗口、输入映射、设备配对与触摸路由。
- `editor.project_lifecycle`、`editor.mcp_commands`：真实编辑器、TS 编译、MCP 启停/重载、场景编辑、批量回滚、撤销与会话隔离。

这些测试仅在相应插件与 GPU 开关启用时登记；协议、文件传输和一般命令测试不要求 GPU。`engine.architecture_boundaries` 检查源码包含方向、实际目标链接方向及循环，`engine.architecture_rules` 检查规则的正反例。

### 启动和停止公开项目

`projects/showcase/` 共用一套设置、资源目录和缓存。完整桌面预设默认装配菜单所需插件；显式裁剪插件时需按对应模块文档补齐运行时与编辑器清单。

```sh
build/ninja-engine-release/editor_app \
  --project projects/showcase --runtime-dir build/showcase-runtime
```

编辑器空白待机后，在另一终端启动项目：

```sh
python3 scripts/mcp_client.py --server build/ninja-engine-release/mcp_app \
  --project projects/showcase --runtime-dir build/showcase-runtime start
```

Windows 使用对应 `.exe`。选择菜单场景后，先聚焦引擎窗口再操作；`Back to scenes` 返回菜单。使用同一条 MCP 命令的 `stop` 停止项目，检查场景与相关资源释放、窗口恢复空白。项目目录和运行目录必须与编辑器实例一致。

默认 `assets/scripts/game.ts` 导出 `modules/menu.js`，无需逐场景改入口。修改脚本源文件后，通过 MCP 的 `stop_project → sync_assets → start_project` 更新缓存，或关闭后重新打开编辑器；只停止再启动不会重新导入源码。生成资源、截图、日志和 `node_modules/` 不提交。

### 截图与 MCP 回执

截图必须来自实际渲染器。只有收到 `complete` 并检查 PNG 文件有效后，才把截图作为完成证据。命令若返回 `activation: pending`，按原 `session/requestId` 查询 `get_editor_receipt`，不要重复提交原操作。复用客户端见 [editor_mcp_session.py](../scripts/editor_mcp_session.py)。

## 公开范例

### 功能用例与常驻 UI

菜单按功能组织 14 项用例，稳定 ID、标题、目标、场景和 Gameplay 入口统一定义在项目 [showcase_cases.ts](../projects/showcase/assets/scripts/models/showcase_cases.ts)。逐项操作、预期结果、自动覆盖和待补测试见[范例功能覆盖](editor/showcase-coverage.md)；不再在菜单与文档各维护一套场景注册。

项目行为测试放在 `projects/showcase/tests/`，核心算法、组件和协议测试继续归 `tests/`。测试须标明使用脚本替身还是实际引擎；脚本状态机检查不能代替 GPU、Jolt、RmlUi 和真实窗口的验收。MCP 工具覆盖见[模块清单](plugins/editor/mcp-coverage.md)。

公开项目专属的资产制作工具放在 `projects/showcase/tools/`，包括公寓、动画庭院、破坏联动和特效生成器；通用 MCP 客户端、构建和检查工具仍放根 `scripts/`。移动工具不重新生成资产或改变资源 GUID。

菜单 UI 根通过 `Node.SetPersistent(true)` 创建、绑定一次，切关复用原文档与 Controller，停止或热重载时完整清理。`UI and scene lifetime` 可检查换关、取消、加载失败、无活动关卡时输入和显式销毁；输入框内容应随原文档保留。生命周期契约见[常驻节点](runtime/class-diagrams/persistent-nodes.md)。

### ECS 大量对象

`ECS many objects` 默认建立 10,000 个真实 ECS 实体，可选 1,000／50,000。逐实体检查组件表迁移后的句柄、层级隐藏／激活、换父级、删除重建和整组回收，随后返回常驻菜单。操作细节见[用例说明](../projects/showcase/docs/ecs-many-objects.md)。

```sh
python3 projects/showcase/tests/ecs_many_objects_test.py \
  --server build/ninja-engine-release/mcp_app \
  --runtime-dir build/showcase-runtime --count 10000
```

连接已启动并导入完成的 showcase 编辑器；脚本通过正常 UI 操作，只读当前 Gameplay 状态，并独立核对原生场景实体总数。回执位于 `projects/showcase/cache/ecs-many-objects/`，不关闭编辑器，不登记为普通 CTest，不输出性能分数。

2026-11-03，ECS 迁移完成 macOS Debug／Release 全目标构建，Release 分批累计 117 项不同 CTest 全部通过：基础及模块回归、原生 10,000 实体、只读／延迟阶段保护、场景跨语言往返、预制体、编辑撤销、脚本、窗口渲染、字体／UI、物理／音频 MCP 和建模 MCP。脚本测试宿主补齐显式可选组件登记，建模测试夹具显式解除预制体关联后分别复验通过；没有放松生产接口保护。showcase 与本地私有项目 TS 类型检查通过，私有项目未纳入 Git。详细本地日志为 `build/ecs-final-release-ctest.log`、`build/ecs-script-effect-release-test.log`、`build/ecs-final-release-window-test.log`；前一份日志保留首次失败，最终结论合并后续对应复验，不等同于单次全仓运行。Debug 按用户新要求只作为编译验收，不再补跑测试；未触发 CI，未验证其他平台。

2026-11-03，macOS Metal Release 实窗通过 10,000 对象完整流程：创建时增加组件不破坏原 Transform 句柄、移动父级、独立隐藏／激活、换父级、删除重建、整组回收至 1,000、返回菜单和重新进入 10,000。每步逐实体检查，原生场景总数始终为用例数量加 5 个固定节点；删除重建及整组回收拒绝了 27 个旧句柄。二次进入的状态恢复为初始值。结果、逐条 MCP 回执及真实截图位于本地 `projects/showcase/cache/ecs-many-objects/20261103T131005Z-6451c5d9/`。

Debug 曾在原 180 秒等待上限内创建 9,856 个对象而超时，随后确认已创建 10,000 并保存实际画面；完整交互结果以本次 Release 记录为准。功能脚本等待上限调整为 600 秒，TS 用例创建每帧 128、检查每帧 1,024；没有采集或承诺帧率。

额外 50,000 档位在原定 600 秒等待上限内完成创建 38,912 个对象，仍处于创建阶段，因此本轮没有完成 50,000 实体逐项验收。未延长或重复运行；随后正常返回菜单，原生 `get_scene.totalNodes` 回到 0，再经 `close_editor` 正常关闭专用编辑器。失败及清理回执保留于 `projects/showcase/cache/ecs-many-objects/20261103T131706Z-sizes-0ecd6366/`。10,000 的完整交互及切换至 1,000 后的回收检查通过，不外推为 50,000 或其他平台已通过。

### PBR 材质球对照

当前实现、全部参数组复拍、逐球指标和未覆盖范围统一见 [PBR 当前结果](../projects/showcase/docs/pbr-comparison/current-results.md)。旧阶段报告保留原数值，不能代替最新结果。

2026-11-04 整体复核后，在 macOS Metal 完成受影响目标 Debug/Release 编译；Release 渲染基础、实际窗口、FBX 管线、FBX 切线、场景默认材质共 5 项不同测试通过。新增覆盖法线终止线平滑（仅直接漫反射）、无扰动法线、掠射、双面与纯金属，关闭 IBL 时 SSR 保留/移除 LUT 引用的整图最大像素差为 0。FBX 补齐非法浮点、输出复用与失败保留旧完整数据检查；新 Mesh、新槽和 FBX 缺省映射均验证默认 PBR。首次四项结果在 `build/material-comparison/final-release-tests.log`，默认入口补充两项在 `build/material-comparison/default-pbr-release-tests.log`，其中 FBX 管线重复一次，共五项不同测试；没有运行 Debug 测试。

本次原参数、原生 1280×720 的 12 组引擎复拍完成，其中 11 组共 66 球与 Blender 默认参考按同一显示变换比较；法线强度 0.5/1 的 MAE 为 0.288/0.469（显示 RGB 0–255），旧标准直接光阶段为 2.855/6.705。未修改曝光、法线强度或关闭 Blender 默认校正来缩小误差。公寓与庭院 11 份实体材质和生成器改用 PBR，正式导入及两场景实窗预览完成；UI/调试专用 Shader 保留。收据和单文件详细对照入口见上述当前结果。

六个金属度和粗糙度组合的正式资源、固定机位、截图方法以及与 Blender 对比的已知限制见 [PBR 材质球对照](../projects/showcase/docs/pbr-comparison.md)。该场景通过 MCP 预览，不依赖玩法启动。

以下为同日较早的仅背光保护阶段记录（当前已补充平滑遮蔽）：在 macOS Metal 增加法线贴图直接光半球回归：同一倾斜法线贴图及相同材质下，方向光分别在平滑基础法线的正面和背面；Release 实窗中心像素红通道为 160 / 0（8 位显示值）。从双面材质的背面观察并交换来光方向时，朝向观察侧及相反侧的结果同样为 160 / 0。该测试验证两侧表面直接光的可见性，不覆盖间接光、软化终止线或所有几何形状。Debug／Release 编译通过；Release `engine.render_foundation` 和 `engine.window_render` 共 2 项通过。

### 现代公寓

[apartment.scene](../projects/showcase/assets/scenes/apartment.scene) 保存客厅、卧室、厨房与餐区，包含 322 个可编辑模型、相机及 5 个灯光节点。家具、摆件、倒角、变换、材质和灯光由 [create_modern_apartment.py](../projects/showcase/tools/create_modern_apartment.py) 通过正式 MCP 工具制作。

正常运行时从菜单选择 `Authored scene and local lights`。如需脚本切镜头与截图，可使用已构建的 Release 编辑器，在进入公寓后运行：

```sh
build/ninja-engine-release/editor_app --project projects/showcase \
  --runtime-dir build/apartment-runtime --width 1600 --height 1000
```

另开终端用相同目录启动项目并在菜单进入公寓，再调用：

```sh
python3 scripts/mcp_client.py --server build/ninja-engine-release/mcp_app \
  --project projects/showcase --runtime-dir build/apartment-runtime start
python3 projects/showcase/tools/create_modern_apartment.py --view-only --configure-view \
  --view wide --capture screenshots/apartment-wide.png
python3 projects/showcase/tools/create_modern_apartment.py --view-only \
  --view living --capture screenshots/apartment-living.png
python3 projects/showcase/tools/create_modern_apartment.py --view-only \
  --view kitchen --capture screenshots/apartment-kitchen.png
```

脚本默认连接 `build/ninja-engine-release/mcp_app` 和 `build/apartment-runtime`；其他构建或运行目录须显式指定 `--server`、`--runtime-dir`。`--start` 只负责启动项目，不会代替菜单选择。`--view bedroom` 切卧室，`--dry-run` 只打印构图清单；`--save-only` 保存当前场景。不带 `--view-only` 会复用或创建模型并重设示例布局。

截图在项目 `cache/screenshots/`，MCP 回执默认在 `build/apartment-mcp-receipts.jsonl`。场景参数已经落盘；`--configure-view` 修改组件后可通过 `save_scene` 保存。该例采用几何造型、颜色材质、实时 PBR、方向光阴影与局部点光，不包含烘焙光照或 IBL。

### 动画庭院与第一／第三人称

进入 `Character, animation and camera` 后，WASD 移动，Space 跳跃，按住鼠标右键转向，Tab 切换视角。第一人称仅在自身镜头隐藏身体和头部，身体阴影与旁观镜头中的角色应保留；第三人称镜头通过球体检测避开墙体。`Back to scenes` 返回菜单。

庭院有铺装、立面、台阶、廊架、长椅、绿植及带物理的 Player/PAB 蒙皮角色。环境由 MCP 创建并保存：231 个节点、225 个环境 Mesh、47,004 个环境三角形、31 个静态碰撞体；三棵树共 840 片薄叶，每树合并为一个 Mesh。人物在场景加载后实例化，避免保存后重复生成。贴图来源和处理见[庭院资源说明](../projects/showcase/docs/courtyard-source.md)。

范例默认使用 InPlace，位置由物理或导航推进。导航、物理交互及地形范例按速度调节动作倍率；庭院仍固定循环 walk，用于检查蒙皮，不是动画状态图。该场景尚未提供触屏摇杆。样例源法线反向，使用 FBX `.meta` 的 `normals="generate"` 导入，未修改源 FBX。

2026-11-04 在 macOS 完成 FBX 切线导入修复的 Debug／Release 构建和 Release `editor.fbx_bind_pose`、`editor.fbx_pipeline` 测试。使用固定版本 ufbx 自带的混合 UV 手性 FBX 验证源切线方向及正负手性，并验证重生成法线时不沿用旧切线。源切线或副切线缺失、无效或法线重生成时，FBX 带 UV 的网格会使用上游 MikkTSpace 离线逐角生成并按需拆点；Release 测试覆盖镜像 UV、颜色和蒙皮复制、子网格、退化 UV 与无 UV。其他来源的网格仍使用各自现有路径；这些测试不能代替实际模型画面的全面验收。

动画导入、动作切换、坐标适配，以及新增重定向／基础 IK 与尚未实现的 RootMotion 范围见[动画模块](runtime/animation.md)；角色制作的后续方向见[制作流程](editor/character-production.md)。这些规划不属于本场景已经通过的验证。

### 射击靶场

`Ray and sphere hit queries` 打开[独立靶场](../projects/showcase/assets/scenes/shooting-range.scene)，包含近靶、远靶和蓝墙后的遮挡靶。WASD 移动，按住鼠标右键观察，左键或 `Fire` 单发射击，Space 跳跃，Tab 切视角。

- `Ray` / `Sphere 0.12 m` 切换查询方式；墙体应阻挡射击。
- 命中显示落点、移除靶面并更新计分；R 或 `Reset targets` 恢复三靶与计分。
- 全部命中后可重置再玩；`Back to scenes` 返回菜单。

这是从相机近裁剪面发出的即时命中范例，尚无枪口弹道、弹药、敌人 AI 或伤害系统。触屏沿用移动／观察区域和 `Fire` 按钮，未在移动设备实机验证本场景。查询身份、过滤和失效语义见[物理模块](runtime/physics.md)。

### 多角色导航

`Crowd and terrain navigation` 提供 16／32／64 个角色，四组交叉移动或两组通过 2.4 米窄门；可反转目标、停止、继续、重置和切观察镜头。每次进入等待地形就绪后构建共享导航面，切换角色数量不重复烘焙。参数和按钮见[范例说明](../projects/showcase/docs/navigation.md)。

挖坑、切断门洞地面和恢复地形后，观察地形版本、导航版本及本次重建瓦片数；断路时代理应停住，恢复连通后沿原目标重新排队。停止群体再修改地形，代理应保持 idle；返回菜单再进入应创建新场景和代理。

此例只提交静态地形；移动残骸不会自动进入导航，脚下地面消失后也没有与物理下落的控制权交接。局部重建仍同步发布，局部避让不保证双向窄门限时无拥堵。核心和插件契约见[导航模块](runtime/navigation.md)。

### 物理交互

`Physics queries and contact events` 打开[物理交互范例](../projects/showcase/docs/physics-interactions.md)：两条路线使用 CharacterMotor，三条使用 NavigationAgent 与零速度运动学 sensor 胶囊，移动由原生系统推进。

选择路线后沿蓝线执行 Raycast 或半径 0.12 米的 SphereCast，观察对象类型、实例身份、距离和命中点。球形／轴对齐盒范围查询将对象标绿，橙色 Trigger 统计 enter/stay/exit。按钮提供暂停、继续、反转路线和清空统计；返回菜单释放角色及订阅。

该场景观察统一物理身份、范围查询和接触生命周期，不含伤害或战斗规则。脚本回归模拟宿主边界，Jolt 与 Detour 行为由原生测试和实窗分别验证。

### 特效实验室

`Particle and model effects` 显示两个公开上游例子：Laser 粒子和带 UV 动画的 Arrow 模型，均通过普通节点的特效组件使用。可检查同时显示、独立暂停／恢复／停止、批量创建、删除撤销和重做重建后的播放，再返回菜单及停止项目检查释放。展台关闭方向光阴影，方便观察特效本身。

历史 Metal 实窗分别批量创建了 32 个粒子特效和 32 个模型特效；这是功能检查，不是容量结论。支持的官方效果类型与尚未接入的扭曲、软粒子等能力见[特效类图](plugins/runtime/class-diagrams/effects.md)。

### 地形实验场

`Terrain editing and restoration` 打开 `scenes/terrain.scene`：64×64 米、32 米高的自制地形。可挖隧道、爆炸挖坑、填充、保存／加载、重置，切换俯瞰／洞口观察及第三人称行走。角色行走仍需键鼠，触屏只操作实验按钮。

编辑器状态文件位于 `cache/terrain-states/*.terrainstate`，脚本状态位于 App 持久目录的 `terrain/<slot>.bin`；保存状态不覆盖源 `.terrain` 或导入缓存。塌方产生的动态土石不能写入旧静态体积存档，执行保存／加载或可撤销体积编辑前应先 Reset。笔刷、设置、重置和加载可检查编辑器撤销／重做；保存文件本身不属于场景撤销。

桌面和 Android 的资源清单均登记了该场景，登记不等于 Android 场景运行已经验收。阶段范围见[地形与破坏方案](plugins/runtime/terrain-destruction.md)，技术和验证边界见[研究记录](plugins/runtime/terrain-destruction-feasibility.md)。

### 建筑破坏实验场

`Fracture bonds and support` 提供切片塔、Voronoi 墙、分段柱等实心试件。通过 UI 施加固定球形损伤、重置和切观察镜头；在编辑器预览中可检查损伤的撤销／重做。停止项目后应清空场景及生成节点。

输入仍为参数盒体，单资源最多 256 个碎块，不是任意复杂 FBX 建筑预破碎工具。断开锚点不保证立即落地，其他接触仍可能支撑碎块；实际位移由重力、碰撞和冲量共同决定。编辑快照也不是独立游戏存档槽。

macOS 阶段验收和 Android 探针的区别见[步骤 2 验证记录](plugins/runtime/terrain-destruction-feasibility.md#步骤-2-当前实现与验证范围)。

### 综合破坏与火箭场

`Destruction system integration` 组合地形、Blast、Jolt、Effekseer、音频和第三人称角色。步骤 4 增加同步应力、动态土石、静态导航瓦片更新及密集火箭场。按钮、参数与资源来源见[项目说明](../projects/showcase/docs/destruction-integration.md)。

- 塔／墙／柱／梁分别预破碎为 256／128／64／72 块，共 520 个资产片段，不等于同时活跃刚体数；活动土石预算为 96。
- 火箭固定 64 个槽，可请求 2／6／12 发每秒，进入场景默认 6。60 发齐射可停止，停火后在途弹体继续飞行；每帧最多一发，不补发卡顿积压。球扫实际命中位置驱动爆破。
- 命中依次驱动建筑损伤／挖土、土石提取、地基刷新、烟尘／音频及导航脏区更新。导航只使用显式提交的静态地形；修改后隐藏旧路线并重建相关瓦片。
- `Weak beam / normal` 切换梁的强度；填土或恢复强度不会自动接回已断连接。`Reset all` 停止炮击，清理弹体、表现、土石，恢复地形和四个结构，重建导航并复位角色。

实窗验证过两档连射、停火、重置、短走和返回清理；功能通过不表示达到请求射速。面板的 `Date.now()` 指标是墙钟帧间隔的指数平均和最大值，包含限帧等待及截图开销，不是 CPU/GPU 工作耗时或 P95。每轮连射清统计，每约一秒及最终完成时输出 `Rocket range sample` JSON，包含实际发射率、命中、blocked、碎块／土石和导航状态。

目前无综合世界存档、移动残骸导航、完整建筑力学或手机性能结论。异步笔刷和损伤接入不表示其他同步事务或连续炮击吞吐已达标。阶段证据见[步骤 4](plugins/runtime/terrain-destruction-feasibility.md#步骤-4-应力土石导航与火箭场)。

## 独立 GPU 照明验证

公开脚本 [scene_lighting_gpu_test.py](../tests/plugins/editor/scene_bake/scene_lighting_gpu_test.py) 要求 Python 3.11+，只使用自造几何及内置材质。默认仅准备资源，`--run` 才启动编辑器并验证一／二次反弹、探针、稳定 GUID、取消和重开：

```sh
cmake --build --preset ninja-engine-debug --target editor_app mcp_app
python3 tests/plugins/editor/scene_bake/scene_lighting_gpu_test.py --run \
  --binary-dir build/ninja-engine-debug \
  --work-dir build/scene-lighting-gpu-test
```

`--binary-dir` 应与实际 Preset 输出一致。生成项目、截图、MCP 回执和 `report.json` 留在工作目录下的 `project/cache/verification/`；默认目录位于忽略的 `build/`。脚本只关闭自己启动的编辑器进程。它验证小型场景的功能，不替代大场景容量或跨平台验证。烘焙公开参数与资源契约见[照明烘焙类图](plugins/editor/class-diagrams/scene-lighting-bake.md)。

## 有日期的验证记录

### 2026-11-06：影片类图契约与正确性修复，Windows x64 / DirectX 12 / QuickJS

先同步[影片类图入口](class-diagrams/index.md#影片播放)、播放器、音频、宿主启动与导入／构建契约，再修改实现；保持现有公开方法，不新增架构包装层。影片 MCP、独立预览与异步导入批次仍属后续工作，不能由本次运行链路通过推断已交付。

| 修复范围 | 本次证据 |
| --- | --- |
| Windows 影片 Shader | 使用兼容 HLSL 的向量常量构造；影片 Shader 参与实际编译，宿主测试通过真实 DX12 提交影片帧。 |
| 解码追帧与取消 | 90 帧／30 fps 夹具在 1.5 秒停顿后少量宿主更新内追到第 45 帧；持续 5 Hz 更新不累积落后，最终显示第 89 帧；停止、释放及旧句柄隔离回归通过。 |
| 音频时间与状态 | 不再重复扣除 SDL 队列；playback 为只读快照，由 tick／控制发布状态。场景清理保留共享设备，完整 clear 关闭；无世界播放、暂停恢复与自然排空通过。 |
| 媒体时间及启动装配 | 静音影片收到完整 1.25 秒增量；同请求首帧成功提交前不初始化世界，准备期间保持同一资源目录与影片。可执行入口在起播前后台选择完整发布版本，无有效版本才读取完整内置目录。 |
| 资源构建 | 显式空 roots 仍收集平台配置的片头与音轨；已选包存储不改变；空平台覆盖可关闭片头；错误类型／非影片入口拒绝且保留旧发布目录。 |
| 像素宽高比 | 对 SAR=2:1、显示比例 32:9 的源生成 1920×540 方形像素影片，音轨时长匹配；更新导入指纹使旧缓存重新生成。 |

使用根预设完成受影响目标的 Debug／Release 编译和链接；未执行 Debug 测试。Release 定向 CTest 的 7 项均通过：`engine.audio`、`engine.script_runtime`、`editor.asset_build`、`resource.movie_player`、`resource.codecs`、`editor.movie_pipeline`、`engine.runtime_host`。最终完整定向运行 7/7，通过用时 14.69 秒。另通过架构规则 10 项、桌面导出脚本 16 项和 28 个修改 C++ 文件的 clang-format 检查。

复跑入口：

```powershell
cmake --build --preset windows-static-debug --target engine_movie_player_test engine_audio_test engine_resource_codecs_test engine_movie_pipeline_test engine_runtime_host_test engine_script_runtime_test engine_asset_build_test engine_app engine_editor engine_project_app
cmake --build --preset windows-static-release --target engine_movie_player_test engine_audio_test engine_resource_codecs_test engine_movie_pipeline_test engine_runtime_host_test engine_script_runtime_test engine_asset_build_test engine_app engine_editor engine_project_app
ctest --preset windows-static-release --output-on-failure -R '^(resource\.(movie_player|codecs)|engine\.(audio|runtime_host|script_runtime)|editor\.(movie_pipeline|asset_build))$'
python tests/build/architecture_rules_test.py
python tests/build/desktop_export_test.py
```

公开 showcase 经 `engine_project_export` 导出后运行 `engine-showcase.exe --frames 420`，DirectX 12 正常初始化，影片开始后约 5 秒输出 `Packaged gameplay started`，420 帧正常退出，退出码 0。导出包含 1370 项资源、2 个资源文件。本机日志位于忽略目录 `build/review-drafts/movie-fix-*.log`，不提交构建产物。

真实 GPU 测试需配置 `ENGINE_ENABLE_GPU_TESTS=ON`；本次已开启。音频自动测试使用 SDL dummy，验证队列、状态及生命周期，不代表扬声器端精确音画同步验收；macOS／Android／iOS 及硬件 SIMD 性能未在本机复验。


### 2026-11-04：V8 15.7 与升级后完整依赖，macOS ARM64 / Metal

固定 V8 15.7.112.23 的正式工具链完成 macOS 13.3 部署目标下的实际重编：Debug 库 1,670 项、Release 库 1,655 项均成功；只执行 Release 上游 `Hello, World!`。保留 sandbox，使用已验证的厂商 libc++ EXTENSIVE 硬化，不以关闭安全检查或可用性诊断通过编译。准备探针验证合法访问和 vector / span / string 越界 trap，所有 V8 C++ 编译均强制版本 / 硬化模式守卫。随后根 Presets `local-ninja-v8-release` / `local-ninja-v8-debug` 全目标构建成功，包含本轮 Quill / SDL 日志桥；Debug 只编译。

V8 后端完整 Release CTest **126/126 通过**，包括真实 JavaScript 执行、默认关闭能力检查、项目脚本、热重载与回滚、TS7 编译、UI MVC、导航 / 破坏绑定、GameplaySession、MCP、资源管线和真实 Metal 渲染回归。输出为 `build/dependencies-upgrade/ctest-v8-release-final.log`，引擎构建为同目录 `build-v8-integration-release-final.log` / `build-v8-integration-debug-final.log`；库构建为 `v8-build-release-macos-13.3.log` / `v8-build-debug-macos-13.3.log`。最后的原生后端编译回执为 `build-native-release-final.log` / `build-native-debug-final.log`，格式收尾后日志及 FBX / 动画六项 Release 回归也全部通过，见 `ctest-native-final-check.log`。

新版 V8 的 Windows MSVC STL 与 Linux libstdc++ 尚未完成满足 sandbox 前提的嵌入适配，准备阶段明确拒绝，默认 QuickJS 不受该限制；macOS 宿主上的 Android 目前只验证独立标准库硬化探针，没有完成新版 V8 完整引擎 APK / 真机验收。既有 vcpkg 静态库的 macOS 27.0 部署目标警告仍存在，见下一节；本机结果不代表完整包兼容 macOS 13.3。接口与移植边界见[脚本文档](plugins/runtime/scripting.md)。

### 2026-11-04：Quill 日志统一接入，macOS ARM64 / Metal / QuickJS

Quill 13.1.0 通过正式 vcpkg port 接入现有 `LogSystem`，异步队列、后台格式化和刷新使用上游实现。私有官方 Sink 扩展点保留无异常文件 I/O、4 MiB 三代轮换和 SDL 平台输出；现有窗口、渲染、UI 及第三方的 `SDL_Log*` 经 SDL 官方输出回调进入同一日志文件，保留平台 category、priority 和筛选，不重复平台输出，也不占用 MCP 标准输出。公开接口和日志路径不变，详见[日志类图](runtime/class-diagrams/log-system.md)。

`local-ninja-debug` / `local-ninja-release` 完整构建与日志桥增量构建成功，Debug 未运行测试。Release `engine.app_log` 通过，覆盖真实 SDL 日志与原生日志混合并发、错误返回前文件可读、显式刷新、中文路径、轮换、打开失败、轮换失败后持续返回失败、重开恢复及 SDL callback / userdata 恢复。随后完整 Release CTest **125/125 通过**；输出为 `build/dependencies-upgrade/ctest-quill-release-final.log`，日志定向回归为同目录 `ctest-quill-log.log`，日志桥构建为 `build-quill-bridge-release.log` 和 `build-quill-bridge-debug.log`。

本次运行使用当前 macOS 宿主。链接器仍报告共享依赖缓存中部分静态对象按 macOS 27.0 构建，高于引擎 / V8 的 13.3 部署目标；未屏蔽该警告，未将本机验收表述为完整发行包通过 macOS 13.3。较旧系统发行前须统一依赖部署目标并在相应系统验收。本节不代表其他平台的 Quill 或完整引擎已实测。

### 2026-11-04：全量依赖升级，macOS ARM64 / Metal / QuickJS

依赖固定版本、稳定渠道和保留版本的原因见[依赖版本表](development.md#当前固定版本)。本轮更新 vcpkg baseline、补齐正式发布较新的 overlays，升级 ozz-animation、TypeScript 及 Android 构建工具；未选择的脚本后端仍不进入当前配置。官方归档保留原始 SHA512，下载与二进制缓存继续复用。以下为 Quill 接入前的阶段记录，接入后的完整回归见上一节。

`local-ninja-debug`、`local-ninja-release` 完整构建及最终增量构建成功，Debug 未运行测试。Release 完整 CTest **125/125 通过**，覆盖真实 Metal 窗口、HDR/PBR、透明、反射、字体/UI、ECS、物理与角色、导航、地形/破坏、动画、FBX/USD、资源管线、TS 编译、MCP、预制体和架构规则。完整输出为 `build/dependencies-upgrade/ctest-release-final.log`，最终构建回执为同目录的 `build-release-final-check.log` 和 `build-debug-final-check.log`。

兼容迁移包括 Flecs 4.2 的真实 world / stage / readonly 契约、Jolt 5.6 的角色接触结构及法向冲量数组、efsw 1.7.3 的回调签名、QuickJS 0.18 的函数捕获参数和显式 import-attribute 检查。新增角色与刚体、角色与角色接触法线回归，分别检查 begin / stay 的 A-to-B 方向；原有场景所有权、候选恢复与阶段隔离断言保留。首次测试发现的 PBR 共用夹具漏文件和 MCP 测试替身竞态已修复；没有放宽生产超时、跳过断言或降低正式 Shader。

TS7 使用官方原生编译器及同版 sync / AST API。公开范例成功输出 39 个模块，输入快照与批编译的 215 项指纹、依赖图一致；跨语言测试通过真实 FlatBuffers C++ / TS / C++ 往返。独立临时脚本回归保留 default / namespace 导出、静态 require、共享状态与非法导入拒绝检查。资源指纹同步覆盖本次 TinyEXR / stb 解码更新；真实临时 shaderc 内容变化、版本号及修改时间不变时，只重建 Shader，GUID、源数据与输出内容保持，下一轮全部命中缓存，正式工具文件未改变。

升级后的 Release 编辑器通过正常管线重新导入公开范例，并在原参数、原机位、原生 1280×720 下完成 12 组 PBR 复拍；编辑器退出码为 0。12 张 PNG 与本轮升级前 `1793773436458021000` 的原图逐字节一致，画面没有因依赖升级发生变化。回执、原图、清单及校验结果见 `build/material-comparison/current/1793778263179463000/`。

Android 工具链单独以 Gradle 9.8.2、AGP 9.5.0、NDK r30、Build Tools 37.0.0 完成最小 Release APK 构建、16 KiB 对齐和 v2 签名检查，28 项资源打包 Python 测试通过。该证据为工具链与打包检查，不能代替完整引擎 Android APK 或真机验收；本轮未验证 Windows、Linux 或 iOS 引擎构建。

### 2026-11-04：bgfx API 167，macOS ARM64 / Metal

当前功能分支把 bgfx overlay 固定到 bgfx.cmake `1.167.9605-606`，运行库 API 为 `167`，宿主 `shaderc --version` 为 `1.19.167`。bgfx、bx、bimg 及工具随同一发布归档安装；SPIRV-Cross 保持 baseline 的 `1.4.309.0`，以 `#1` overlay 回补新 Shader 工具要求的上游 PointSize 查询。固定提交、补丁去留和工具范围见 [overlay 说明](../third_party/overlay-ports/README.md)。

`local-ninja-debug` 和 `local-ninja-release` 整体构建成功，Debug 未运行测试。Release 的渲染基础、实窗、平面反射、HDR 场景／透明／扩展捕获、直接光浮点对照、字体/UI、纹理管线、资源构建、工具、宿主 Shader API 匹配、特效管线与运行、照明数学及文件回归共 16 项全部通过；两项架构检查也通过。窗口回归继续验证真实像素尺寸、VSync、MSAA 与各向异性切换。工具回归按当前后端编译并检查 `VSH` 二进制版本 `14`；配置回归验证原生和带 sysroot 的交叉目标都拒绝不匹配的宿主工具。

本次兼容修复包括交换链初始化／重设、纹理区域复制与异步回读、截图格式参数、精确 MSAA 能力位和 Effekseer C99 调用。新版 bimg 对二维／立方体 KTX1 的 depth 语义为 `0`；引擎写入器和读取校验同步，非法 `pixelDepth=1` 仍拒绝，并新增二维与立方体回归。

通过 Release 编辑器 MCP 重新导入公开 showcase，并采集 12 组 1280×720 原生 PBR 截图，编辑器退出码为 0。全部截图的 PNG SHA256 与升级前 `1793770721339431000` 那次一致，11 组 Blender 逐球误差不变；本次回执、原图及自包含报告位于 `build/material-comparison/current/1793773436458021000/`。

另用高对比线性 HDR 全景图和 cubemap 各完成一次真实 IBL 烘焙，均完成 97 项任务、发布三项 KTX 及相邻 meta、正常增量导入并刷新场景，编辑器均正常退出。全景图回执在 `build/ibl-high-contrast-hdr/1793773514741883000/project/cache/verification/1793773515071122000/`，cubemap 回执在 `build/ibl-high-contrast-hdr/1793773375678584000/project/cache/verification/1793773375819501000/`。这些记录覆盖本机 Metal；其他平台只完成共享接口适配，未宣称已构建或运行通过。

### 2026-11-03：预制体与公开范例迁移，macOS ARM64 / QuickJS

Debug／Release 全目标构建通过，两配置各通过 31 项定向 CTest：预制体字段合成、资源管线、运行时实例、编辑命令、场景保存、Model/FBX/USD、资源闭包、脚本实例化、架构边界和 5 项公开项目行为测试。覆盖同值/0/false 的明确覆盖、模板连续更新的稳定身份、引用重映射、失败回滚、材质恢复与撤销、缺失或临时实例的保存约束。另有 53 项 Python 资源打包/导出/架构规则检查通过；没有运行性能测试或 CI。

公开范例 10 个场景改为引用直接预制体，动态人物使用 FBX 关联预制体。迁移核对保留全部 851 个原节点的数据，静态环境增加 9 个恒等变换包装根。TS 类型检查通过；脚本替身测试不能代替真实引擎运行。制作、覆盖和首版限制见[预制体流程](editor/prefabs.md)。

本地构建与定向测试证据为 `build/prefab-build-*-final.log`、`build/prefab-ctest-*.log`；本轮未重跑既有跨语言 schema 失败项，不宣称全仓或其他平台通过。

公开范例全量导入计时：`projects/showcase/assets` 共 2,641 个文件，逻辑大小 81,356,707 字节（77.59 MiB，含源资源和 meta）。Release 导入工具使用 8 线程，2026-11-03 19:39:45.744 开始，19:41:44.079 完成，约 118.34 秒；回执为 `imported=1326 reused=0 removed=0 threads=8`。耗时按导入日志创建至完成回执写入统计，覆盖扫描、转换与发布，不包含 C++ 构建及打包。当时还有另一个项目并行导入，文件系统缓存也已被读取；这是实际全量资源重导耗时，不是独占机器或冷磁盘基准。日志为 `build/prefab-import-showcase-release.log`。

Release / Metal 真实编辑器已打开迁移后的方块、公寓、庭院场景，分别为 3、332、233 个节点。通过 MCP 创建两个角色预制体，验证其中一个的位移和 `enabled=false` 不影响另一个，明确覆盖查询、恢复模板位置、撤销/重做一致；关闭重开恢复源场景，项目菜单正常启停，最后 `close_editor` 退出码 0。回执和报告位于 `build/prefab-live/`。首轮验证遇到加载期间状态超过 MCP 的 5 秒存活窗口，客户端等待状态重新发布后完成；未将此扩展为其他模块重构。

公开范例按正常 `--package --target macos` 构建运行资源，产出 1,240 项资源、2 个包，完整运行依赖校验通过；其中保留脚本动态创建的角色预制体，静态场景模板已展开，不额外打入模板及来源 Model。打包回执为 `build/prefab-package-showcase.log`。

### 2026-11-03：架构收敛与节点状态，macOS ARM64 / Metal / QuickJS

Debug／Release 全目标增量构建通过。两配置各通过 50 项定向 CTest，覆盖组件身份、父子激活与可见状态、场景持久化、物理、音频、动画、导航、地形与破坏、脚本、编辑器撤销、资源文件发布、MCP 传输及真实窗口。该轮另执行的 `engine.scene_schema_cross_language` 曾因既有 flatc 输入枚举 TS import 路径错误失败（`TS2307`）；后续 ECS 迁移让场景测试直接导入所需类型，Debug／Release 已复验通过。输入生成器的剩余边界见[已知失败与限制](#已知失败与限制)；不宣称全仓测试通过。公开范例 TS 类型检查通过。

状态回归包括父级恢复不覆盖子级局部标记、组件重建后旧脚本句柄失效、隐藏仍保留碰撞、UI 隐藏／停用保留文档及订阅、回调内停用后停止剩余事件投递，以及暂停期间生成碎片同步显示但不发布异步破坏候选。渲染实窗检查覆盖父级隐藏的实际像素变化；项目启停、MCP 编辑和延迟关闭仍通过真实编辑器验证。

架构收敛覆盖统一组件身份、唯一脚本预检启动路径、组件直接持有渲染设置、统一编辑命令通道、无状态 MCP 登记函数、资源元数据撤销快照及源文件／meta 发布共用函数。首轮新增测试的重复持锁已修复；MCP 边界测试通过同一会话检查所有非法 ID，保留原 60 秒上限及全部断言。重复持锁、超时两项问题及最后补充的状态用例均已定向复验通过。

本地证据为 `build/architecture-build-*.log`、`build/architecture-test-debug*.log`、`build/architecture-test-release*.log`、`build/architecture-typescript.log`。没有执行性能测试或 CI，未新增其他平台实测结论。

### 2026-11-03：Basis Universal 2.50 与测试目录整理，macOS ARM64

BasisU 2.50 的 Debug／Release 库已实际构建安装，两配置引擎全目标构建通过。各通过 15 项定向 CTest：贴图与资源导入、HDR 半浮点转码和预算、资源编解码、UI 导入、特效导入与运行、光照资源、渲染基础、脚本编译、架构规则及桌面／Android 资源打包；另各通过 `engine.window_render` 和 `engine.font_ui_render` 两项真实 Metal 测试。

贴图回归覆盖 PNG/JPEG、TGA/QOI、HDR/EXR、完整 mip、增量缓存、无效设置不发布，以及保留 KTX cube／LUT。HDR 检查大于 1 的亮度、65216 上限、透明通道和设备能力；不把有损 HDR 编码当作任意浮点数据存储。默认导入并发为 8，并验证项目配置仍能覆盖它。

114 个测试与辅助文件按模块迁移，CTest 名称及目标名保持不变；18 个 Python 文件的新位置编译／导入通过。迁移回归发现资源目录解析器漏接受 `light_probe=17`，修复后 Android 资源打包 28 项、桌面导出 16 项 Python 检查均通过。证据保存于忽略目录 `build/basisu-upgrade/`。

上述为定向功能回归，不是全仓测试或性能验收；没有新增 Windows、Android、iOS 实机结论。移动端关闭 BasisU CLI 的配置已保留。

### 2026-11-03：渲染升级，macOS Metal

Debug／Release 均完成 `engine_app`、`editor_app` 及受影响测试目标构建，每种配置通过 17 项相关 CTest，覆盖场景与资源编解码、脚本绑定、预算配置、烘焙数学／发布、反射数学、真实渲染、字体和特效。公开范例 TS 类型检查通过；该轮额外跨语言 schema 测试曾失败，后续 ECS 场景回归已复验通过，输入 SDK 生成器仍有限制，见[已知失败与限制](#已知失败与限制)。这些结果不是全仓 CTest 全过声明。

独立开关画面对照检测到的有效变化像素如下，数字用于证明对应效果实际改变画面，不是质量评分或性能数据：

| 对照项 | 有效变化像素 |
| --- | ---: |
| 平面倒影 | 9,774 |
| 折射率变化 | 9,738 |
| SSR | 9,550 |
| 点光阴影 | 1,601 |
| 聚光阴影 | 3,826 |

检查还覆盖蒙皮、窗口缩放、取消捕获、无活动相机捕获和释放后 GPU 回到待机。HDR 关闭时，3D 字体、普通与预乘透明无光照材质在改变曝光和色调映射后截图逐字节一致。未实测其他平台。

### 2026-11-03：Lightmap 与探针端到端，macOS Metal / Debug

自造小场景的真实 MCP 一／二次反弹均完成：两张 16×16 Lightmap 各有 256 个有效 texel，8/8 探针有效，五项输出重烘焙保持 GUID。白地面红色间接光均值从 0.17691 增至 0.22558，动态物体背光面接收到红墙反弹。取消前后 assets 文件哈希一致，关闭重开后场景正常恢复。

CSM 接收面深度修正后重新打开已有烘焙场景截图，确认大面积斜面自阴影条纹消失；此次截图没有重烘焙，低分辨率接触边缘仍可见阶梯。未验证大场景、Release 完整烘焙或其他平台；两配置的定向测试不能替代这些检查。

### 2026-11-02：oneTBB 任务模块，macOS ARM64

Debug（Tracy OFF）与 Release（ON）各 7 项定向测试通过；运行时、编辑器、资源构建和 MCP 宿主编译链接，两配置编辑器及 Release 独立运行时空白 Metal 实窗正常启停。独立探针验证 OS QoS 设置、精确恢复及失败路径；Tracy 实际采集及离线分析读取 360 次任务成功。关闭 runtime 依赖后，核心配置不查找或链接 TBB。

未重跑全部插件测试。Windows／Linux／Android 未构建运行；iOS 只做优先级适配源码交叉语法编译。上游不正式支持静态 oneTBB，移动包与静态链接仍须定向验证。

### 2026-11-01：资源路径、输入、KV 与射击回归

| 平台／后端 | 已记录的结果 | 未覆盖范围 |
| --- | --- | --- |
| Windows x64 / DX12 / QuickJS | 资源路径升级的 Debug／Release 构建及运行包导出；Release 91/91 CTest，Debug 完成前 14 项。catalog-only 修复后检查受影响构建、首次／重复同步及运行包启动。输入两配置各 5 项，KV／脚本各 2 项定向回归，SDK 与 showcase 类型检查通过 | 后续改动未做完整 Debug CTest，未逐一验证所有示例；手柄为 SDL 虚拟设备，触摸为注入，符号链接用例受权限限制 |
| Android ARM64 / Vulkan / QuickJS | 两配置 APK 构建及静态校验：唯一 `libmain.so`、catalog 与资源实体、目标 Shader、签名及 ZIP／ELF 16 KiB 对齐；catalog-only 同步定向检查 | 路径升级后 APK 未安装到设备；新增输入／KV 无 Android 运行结果 |
| macOS ARM64 / Metal / QuickJS | 受影响 Debug／Release 增量构建；输入、KV、脚本、UI、角色及资源管线／打包各 8 项 CTest，另通过桌面／Android 打包脚本、架构边界和 showcase 导航回归。射击链路两配置各 6 项，Metal 实窗验证菜单往返、移动、射线／球扫命中、墙体遮挡、计分与重置 | 该轮未重跑全套 CTest、应用导出或其他平台构建；手柄／触摸为虚拟或注入，射击无其他平台实机结论 |

### 2026-10-31：IBL 烘焙，macOS Metal / QuickJS Release

验证了 128 分辨率、256 采样的完整 GPU 烘焙、半浮点结果与 GUID 缓存、开启 IBL 后自动应用、取消重烘焙不改变已发布文件。构建与相关 CTest 使用 Release；当次未运行 Debug、Windows 或 Android。发布后导入与预览重建沿用同步流程，大场景刷新期间仍可能暂停响应。

## 历史模块验收

以下条目在原记录中没有统一日期，按其原有范围保留，不把它们视为 2026-11-03 全部重跑。

| 范围 | 已有证据 | 边界 |
| --- | --- | --- |
| macOS 多角色与可破坏地形导航 | Debug／Release 共 10 项导航、代理、脚本、编解码及装配定向回归；TS 和编排检查通过。Metal 实窗观察 16／64 角色、反转、启停及地形变化 | 未做其他平台、完整战斗联调或容量压测；同步重建与避让无目标设备性能结论 |
| macOS ARM64 / QuickJS 物理交互 | Debug 5 项、Release 7 项查询／角色／范围／接触／脚本／运动学组合定向 CTest；订阅与 Trigger 类型切换修复后两配置再回归。实窗验证查询、范围、触发统计和菜单清理 | 无其他平台或性能验收；Trigger 类型切换最终修复只做定向测试，未在窗口切换属性 |
| macOS InPlace 动画导入 | 两配置 FBX 管线、轴向与动画定向测试，TS／编排回归通过。walk 每周期前进位移由约 1.4253 米降到浮点误差；未选轴、旋转、缩放和其他骨骼不变。同步、Metal 启动和停止后零节点通过 | 无其他平台实窗结果；未实现 RootMotion 驱动与运行时模式切换 |
| macOS / Metal / QuickJS 特效 | 两配置构建 `editor_app`、`mcp_app`，通过 `editor.effects_pipeline`、`engine.effects_runtime`、`engine.window_render`、`engine.character_host`；插件关闭时无 SDK 目标，18 个 Metal／18 个 SPIR-V Shader 离线编译 | 两配置定向回归不是全仓测试；无 Windows／Android 真机结果 |
| macOS ARM64 地形 | 两配置定向回归与 Metal 实窗覆盖跨块隧道、挖填、撤销／重做、ETRS 保存恢复、角色落地／行走、预览启停和清理；关闭后场景／节点归零 | Android 独立探针不能替代正式插件场景验收；触屏只操作实验按钮 |
| macOS ARM64 建筑破坏 | 两配置覆盖运行时图／物理、Authoring、导入、编辑事务、MCP 注册及禁用。实窗覆盖预览损伤／撤销重做、各试件损伤／重置、镜头及返回清理 | Android 只有独立 Graph 和步骤 4 标量 ExtStress 探针；完整 APK 场景、地基联动及移动端性能未验收 |
| macOS 综合破坏步骤 4 | Debug／Release 累计各 8 项定向测试；最终资源实窗两档连射、停火、重置、短走及返回清理 | 属于同步功能验收，不表示达到请求射速；Android ARM64 仅独立 Graph／ExtStress 探针 |
| Android ARM64 / API 34 软件 Vulkan 模拟器 | 资源路径升级前验证过菜单／场景往返、角色／动画、音频状态、前后台恢复、完整资源同步和有限帧退出 | 不代表当前包、实体设备性能、扬声器、真实触屏或 16 KiB 页设备已验证 |
| Linux / iOS | 保留平台配置和统一后端约束 | 无本分支构建及端到端运行结果 |

导航实窗初建 40 个瓦片，挖坑局部更新 9 个，断沟 18 个，恢复此前两处修改 24 个；这些计数只证明局部更新范围。挖坑后 16 角色全部到达；门洞断沟后 8 个无路、8 个因当前位置无法投影而 invalid。恢复后全部重新移动，观察结束时 14 个到达、2 个仍 moving，未声称限时全员通过。停止群体再修改／恢复地形仍保持 idle。

物理交互场景含 2 个 Motor 和 3 个导航角色，返回菜单剩 2 个菜单节点，停止后为 0。特效实窗确认两个公开例子同屏、各自暂停／恢复／停止，分别创建 32 个粒子／模型效果，撤销删除、重做重建后继续播放，菜单往返正常，停止后场景和节点归零。

特效模型回归还验证真实网格上传、粒子同屏和删除后 GPU 资源释放。efkbgfx／Effekseer 1808 的模型工厂、UV2 顶点布局及 40 项模型实例常量兼容修正是这些结果的前提；补丁仅应用构建副本，布局偏移和大小有编译期检查。

### 2026-11-01 单线程性能历史边界

旧单线程版本在 Mac 的可信采样中，单次完整地形碰撞更新最大 0.468999 ms；Jolt 单步均值 1.135225 ms、P95 1.976267 ms、最大 3.280668 ms，整个物理模块仍未达到 0.5 ms。它不代表当前并行版本；不能把不同命中量换算为等工作量加速比，也不能外推其他平台。完整口径见[历史单线程基线](runtime/performance.md#历史单线程基线)。

## 已知失败与限制

- **输入 SDK 生成边界（2026-11-03）**：flatc 25.9.23 为既有 `InputControlState` 生成的 TS 枚举 import 路径不正确，整个 SDK barrel 会暴露 `TS2307`；输入生成器本轮未修改。`engine.scene_schema_cross_language` 已直接导入所需场景生成类型，保留完整场景跨语言往返断言，并在 Debug／Release 实际通过；这个结果不表示整个输入 SDK 问题已修复。
- **Android 验收边界**：APK 为本机调试签名；单 `libmain.so` 包的 ZIP 与 ELF LOAD 16 KiB 对齐检查通过，但未在 16 KiB 页设备运行。adb 同步只支持 Debug 包，正式发布签名和内容更新渠道未实现；FNV-1a 仅作完整性检查。软件 Vulkan 不代表真机性能，纹理压缩格式矩阵仍待实机验证。
- **图片验证器差异**：固定 bimg 的部分带 alpha 图片往返因 `m_hasAlpha` 元数据不一致导致 `texturec --validate` 失败。已测样本像素一致，元数据兼容问题未修补。
- **渲染限制**：SSR 无屏幕外信息或时序降噪；透明按子网格排序，不解决交叉面的逐像素排序及多层折射。HDR 关闭时单通道权重只近似区分受光照／无光照贡献，加色与多层混合不能精确分离曝光。静态 GI 不随破坏实时重算，探针单点插值不能自动消除薄墙漏光；没有动态 GI 或自动局部反射立方探针。
- **物理与音频边界**：约束关节尚未提供；动态刚体不支持父节点，球要求等比缩放，胶囊横向缩放须一致。音频无压缩／流式长音频、混响和音频总线；自动测试使用 SDL dummy，不能替代扬声器听觉验证。
- **资源与角色边界**：FBX 未转换源材质、贴图、混合形状或双四元数蒙皮；USD 仅支持自包含静态范围。已接入共享 TS 普通状态机、显式 profile 运行重定向、基础 Aim／TwoBone IK、骨骼挂点和事件；其功能证据见本页最新动画验收节。分层遮罩／加法混合、RootMotion 驱动、接触脚锁和全身 IK 尚未实现；控制器不附带触屏 UI。
- **工具与导航边界**：完整编辑器界面、移动残骸自动导航及导航代理与 Jolt 动态运动融合未完成。建模未达到 ProBuilder／UModeler 完整等价：选边倒角限闭合凸体，扫掠无闭环路径，建模 UV 仅一个通道；具体范围见[建模功能边界](plugins/editor/modeling.md#几何与持久化边界)。这与照明烘焙另行生成 UV2 是不同流程。

## 证据位置

日志、截图、生成测试项目及构建产物保留在忽略目录，不提交 Git；下列为历史本地记录位置，其他检出不保证已有这些文件。复跑入口由 Presets、[`tests/`](../tests/) 及本文命令维护。

| 范围 | 记录位置（相对仓库根） |
| --- | --- |
| 分支复核、资源布局与 KV | `build/branch-review-*`、`build/resource-path-validation/`、`build/catalog-only-fix/`、`build/kv-storage/` |
| 射击 | `build/shooting-range-tests-*.log`、`build/shooting-range-final-report.json`、项目 `cache/screenshots/shooting-range-final-*` |
| 导航与地形导航 | `build/navigation-tests-*.log`、`build/navigation-terrain-tests-*.log`、`build/navigation-terrain-showcase-tests.log`、`build/navigation-review-report.json`、`build/navigation-terrain-review-report.json`、项目 `cache/screenshots/navigation-*-review-*` |
| 物理交互 | `build/physics-interaction-tests-*.log`、`build/physics-subscriptions-tests-*.log`、`build/physics-contact-transition-tests-*.log`、`build/physics-interaction-review-report.json`、项目 `cache/screenshots/physics-interaction-review-*` |
| 特效 | `build/effects-review-r2-report.json`、`build/effects-review-r3-report.json`、`projects/showcase/cache/screenshots/` |
| 旧 QuickJS 与 Windows 交付 | `build/quickjs-validation/`、`build/windows-cross-platform-review/` |
| 公寓 | `build/apartment-mcp-receipts.jsonl`、`projects/showcase/cache/screenshots/apartment-*` |
| 独立照明脚本 | 默认 `build/scene-lighting-gpu-test/project/cache/verification/` 内的 `report.json`、回执、各反弹资源副本与截图 |

资源版本发布与包体布局的正式契约仍以[资源加载](runtime/resource-loading.md)及[构建插件](plugins/editor/resource-build.md)为准；测试目录和历史证据不成为运行时依赖。

## MCP 与公开范例整理（2026-11-03）

本轮围绕编辑器操作链路执行定向验证：Debug／Release 的 13 项脚本、协议、编辑保存及 showcase 测试通过；另有 UI 编辑器与字体/UI Metal 渲染各 2 项通过。新增 UI 双击回归在两种构建复验通过。

直接 TS 执行覆盖当前 Gameplay/模块实例复用、Run 代次、错误隔离、JSON 结果与临时代码回收；UI 覆盖实际点击/双击、Unicode 输入、修饰键选区、遮挡、取消和源文件冲突保护。真实渲染 MCP 验证查询、设置、撤销/重做及资源身份，不以此宣称最终画面质量已验收。

`close_editor` 已在真实编辑器验证完成回执先返回、正常退出码 0，并验证回执写入失败时不会提前关闭。工具数量和边界见 [MCP 覆盖](plugins/editor/mcp-coverage.md)；公开项目的用例、操作脚本及端到端结果见 [showcase 覆盖](editor/showcase-coverage.md)。V8 本轮仅做变更实现的语法编译，实际脚本执行使用默认 QuickJS；未重新声明全部仓库或其他平台通过。

## PBR IBL / SSR 定向验证（2026-11-04）

**精确 sRGB 输出补充验证**：macOS Metal Release 的 `engine.window_render` 对线性 RGB `0.001 / 0.0031308 / 0.01` 实测显示值 `3 / 10 / 25`，独立标准 sRGB 参考为 `3.29 / 10.31 / 25.46`（8 位量化单位）；清屏 RGB `10 / 64 / 127` 解码再编码后保持相同。`engine.planar_reflection` 检查主视图和反射共用的清屏解码在 sRGB 转折点两侧的数值，窗口测试继续覆盖真实镜面反射；该检查不单独量化反射背景像素。`engine.font_ui_render` 验证现有 UI 和 3D 字体绘制、HDR 关闭时的字体稳定性及资源生命周期，但不单独量化字体暗色样点。三个 Release 测试通过，受影响目标的 Debug 与 Release 编译通过。显示空间的 UI 预乘混合保持原有行为。历史 gamma 2.2 输出和新 sRGB 结果不能直接混用；下述历史数字保留采集时的显示变换。

macOS Metal Release 的 `engine.window_render` 使用正式资源目录加载白色 RGBA16F 立方纹理及合成 RG16F split-sum LUT，验证视角 0°、60°、80°，金属度 0、0.5、1 与粗糙度 0.1、0.5、1 共 27 组白炉能量组合。截图经逆 Reinhard 与 gamma 2.2 变换后的值均约 0.998，参考为 1；有色半金属 RGB 分别为 0.8297、0.3975、0.3277，参考为 0.8351、0.3964、0.3297；允许 8 位输出量化误差。测试 LUT 为固定的合成输入，不验证 IBL 烘焙，也不代替线性浮点缓冲回读或真实 HDR 环境的验收。

同一窗口测试还比较金属度 0、0.5、1 下开启和关闭 SSR 的屏幕像素，覆盖 IBL 与 SSR 同时启用及有色 SpecGloss 材质，并保留 AO、正交相机、反射及水材质回归。有色 SpecGloss 的 80° 掠射、漫反射及合成 IBL 各通道均与参考计算对照，使用截图反推线性值。无 IBL LUT 时，环境光、光照贴图和探针的漫反射使用与 SSR 相同的 Three.js DFG 拟合估计菲涅耳能量分配，不凭漫反射数据生成不存在的镜面环境；窗口测试对照多种金属度和有色 SpecGloss 的独立积分值。该拟合仍有误差：例如 80°、粗糙度 0.1 的菲涅耳估计相对独立积分绝对误差约 0.062。SSR 只替换线性反射子项，非线性与多次散射保留环境估计，不能称为完整物理反射。未实测其他图形后端。

另运行 `python3 tests/plugins/editor/scene_bake/ibl_gpu_test.py --run`，在公开临时项目使用白色线性 HDR 纹理完成实际 GPU IBL 烘焙、资源发布和场景加载：97/97 作业完成，漫反射六面值为 1，预过滤各 mip 和六面值为 0.99951–1；真实 LUT 的九个采样点与独立 8192 样本积分最大绝对误差为 0.013142。接收面截图从 RGB 0 变为 186，逆显示转换约为 0.998。该结果仅验证白环境、九个 LUT 采样点以及 SDR 截图，尚不代表真实复杂 HDR 环境或线性浮点渲染读回通过。脚本通过 MCP 正常关闭编辑器，退出码为 0；测试中发现并修复了烘焙句柄重复销毁造成的退出崩溃。

运行 `python3 tests/plugins/editor/scene_bake/ibl_directional_hdr_gpu_test.py` 验证非均匀线性 HDR 的实际 GPU 烘焙。512×256 的 RGBA16F 全景输入为 `L(w) = (2 + 1.2 wx, 1.5 + 0.8 wy, 1 + 0.7 wz)`，包含大于 1 的辐亮度；IBL 分辨率为 64、采样数 256、LUT 分辨率 128。完成 97/97 步；修复源 mip 滤波后，漫反射立方图 54 个样点相对解析余弦半球积分的最大绝对误差为 0.060939，预过滤立方图 7 个 mip 的 300 个样点相对独立数值积分的最大绝对误差为 0.124219，最差点位于 1×1 mip；修复前对应数值为 0.033595 和 0.083203。测试逐通道绝对误差阈值为 0.15，包含有限采样、纹理分辨率、mip 滤波及半精度存储的偏差，并非对其他环境的精度保证。截图仅用于验证资源发布和加载产生画面响应；环境是平滑的低频解析输入，截图为 8 位 SDR，尚不能代替摄影 HDR、Blender 对照或线性浮点读回。测试使用临时公开项目，经 MCP 正常关闭编辑器，退出码 0；数据位于 `build/ibl-directional-hdr/`。

高对比测试 `python3 tests/plugins/editor/scene_bake/ibl_high_contrast_hdr_gpu_test.py` 使用峰值 RGB `[32, 16, 8]`、背景 `[0.2, 0.2, 0.2]`、角半径 10° 的线性 HDR 亮斑，与独立 GGX 数值积分对照。输入全景仅有基础层时，原实现直接读取不存在的全景 mip，预过滤容易漏掉或夸大亮斑。改为从立方图基础层逐级按立体角滤波并进行全景足迹采样后，64 分辨率、256 样本下 +Z 面中心红色通道的 mip 4/5/6 分别由 `0.2722/0.2311/1.8018` 变为 `1.5078/0.9106/0.9668`，独立积分参考为 `1.7999/1.0718/1.1589`。单层 cube 输入重建 mip 后对应结果为 `1.4844/0.8979/0.9541`。白环境回归仍通过；高亮结果仍有明显偏差，且 mip 1 为 `26.8125`、参考 `28.9515`，不能视为精确收敛或摄影 HDR 验收。报告与输入纹理位于 `build/ibl-high-contrast-hdr/`。

标准 SpecGloss 颜色空间回归使用正式纹理资源，RGB 字节 `(128, 176, 224)` 分别按 sRGB 和线性读取，并以 alpha 160、80 覆盖线性光滑度；正入射直接光的三个颜色通道与独立 sRGB 解码及 GGX 参考对照。例如 sRGB/alpha 160 实测为 `0.2248/0.3348/0.3891`，参考为 `0.2234/0.3369/0.3856`。这些数值从 8 位截图反推线性值，仍受截图量化影响；同一窗口回归通过。

非 2 的幂立方输入的 mip 生成验证使用 5×5 单层线性 HDR，+Z 面中央亮列；经实际 GPU 烘焙后，预过滤 mip 4/5/6 的五点与独立积分相比最大绝对误差为 0.07068，左右对称点差约 0.00098。测试限定此输入、1024 采样及指定点，不能推广为所有 NPOT 或摄影环境的精度保证。完整输入和回执位于 `build/ibl-npot-cube/`。

全景小亮斑验证运行 `python3 tests/plugins/editor/scene_bake/ibl_panorama_footprint_gpu_test.py`，输入为 1024×512 线性 HDR 全景中 4×4 像素亮斑，峰值 2048、背景 0.25；以经纬图源像素的精确球面立体角积分作为参考。实际 GPU 烘焙输出的亮斑超出背景部分的球面积分，相对误差在赤道、接缝和近极点分别为 2.47%、2.56%、3.91%，三项均低于该固定测试的 10% 回归阈值。测试只覆盖三个位置及该尺寸，有限采样无法保证任意亚像素亮斑不丢失；报告位于 `build/ibl-panorama-footprint/`。

**源 mip 选择修正后的补充验证**：上面各段保留当时的历史结果。依据 GPU Gems 3 的样本立体角与立方图平均像素立体角公式补回 `4π` 后，在相同 macOS Metal Release 和 256 样本下重新烘焙：高对比全景 +Z 面的 mip 1/4/5/6 红色通道从 `26.8125/1.5078/0.9106/0.9668` 变为 `28.8594/1.7666/1.0518/1.1875`，独立数值积分参考为 `28.9515/1.7999/1.0718/1.1589`；6 面、7 个 mip、RGB 共 126 个通道样点的平均绝对误差从 `0.07374` 降到 `0.00684`，最大值从 `2.13899` 降到 `0.09212`。单层 cube 输入的最大绝对误差为 `0.14548`；全景输入以 1024 样本烘焙时为 `0.04320`。测试增加低粗糙度 mip 1/2 的独立积分回归，允许每通道误差不超过输入对比度的 1.5%，原 mip 4/5/6 的 2% 阈值保持不变。

同条件重跑平滑方向 HDR，漫反射 54 点最大绝对误差从 `0.060939` 降到 `0.009234`，预过滤 300 点从 `0.124219` 降到 `0.016797`；白环境、NPOT cube 和赤道／接缝／近极点小亮斑测试均通过，NPOT 五点最大误差为 `0.054505`、左右对称误差为 `0.000977`。这些指标只覆盖指定输入和采样点；使用平均立体角以及有限分辨率和样本数仍是近似，不保证所有高动态范围环境或其他 GPU 后端达到相同误差。

`engine.pbr_hdr_direct_light` 使用生产共用的直接光 Shader，将受控结果写入 RGBA16F GPU 目标并读回，对照独立数学公式。macOS Metal Release 的七组输入覆盖方向光、金属度、低粗糙度、点光距离和聚光锥角；金属和低粗糙度高光分别为 `162.875`、`7764`，参考值为 `162.975`、`7764.916`。此测试不覆盖完整场景的材质采样、IBL、后处理或 HDR 回读，也不表示灯光已按物理单位标定。

`engine.window_render` 的切线回归比较静态节点非均匀／负缩放与独立预变换几何，两组的变换结果与预变换结果的红通道均为 `126/126`；负缩放故意使用错误手性时为 `0/126`。该实窗测试输出为显示编码图像，验证范围限于测试几何、材质及所测变换。

普通 `.mesh` 导入、FBX 和建模网格复用编辑器侧 MikkTSpace 切线生成。Release 的建模核心与管线、网格及贴图管线、FBX 绑定姿态和两项架构检查共 7 项通过，插件选择检查另行通过；独立启用建模和独立启用资源管线的配置也分别通过相应功能测试。普通网格测试覆盖非单位法线、镜像 UV 拆点、UV2、颜色、子网格、已有切线、无 UV 和无效数据不发布。运行时动态网格不经过编辑器离线生成，仍使用运行时既有路径。

`engine.window_render` 的正式场景组合验证使用 sRGB 材质贴图、方向光和白色 IBL，分别检查直接光、IBL 与两者叠加。对 SDR 截图按已知 Reinhard 和 gamma 2.2 反变换后，组合 RGB 为 `0.9980 / 1.6691 / 2.6058`，独立解析参考为 `0.9948 / 1.6637 / 2.6336`，最大绝对误差约 `0.0278`。高亮蓝通道的一个 8 位码级约相当于 `0.0967` 的线性值；该测试可以检查指定场景的组合关系，不能替代浮点 HDR 直接回读。

2026-11-04，固定版本 MikkTSpace 的小角度补丁和导入指纹更新后，macOS Debug／Release 受影响编辑器与测试目标编译通过；Release `modeling.core`、`editor.mesh_pipeline`、`editor.fbx_pipeline` 共 3/3 通过。细长三角形切线回归在补丁前失败、补丁后通过，完整 showcase 资源导入和编辑器正常关闭也已验证。HDR 捕获另以 Release `engine.scene_hdr_capture` 验证已提交帧后失去相机以及读回期间重建 MSAA 目标，1/1 通过。

使用最新 Release 编辑器与 Blender 5.2，在 1280×720、相同曝光和 Reinhard、精确 sRGB 编码下采集 12 组、对照其中 11 组材质球；逐球数值和具体限制见[当前 PBR 对照](../projects/showcase/docs/pbr-comparison/current-results.md)。单文件图文报告为 `build/material-comparison/current/1793770721339431000/review.html`；这是 macOS Metal 的受控场景结果，不能代替其他后端或完整场景的验收。

**透明与渲染扩展的线性 HDR 捕获**：2026-11-04，macOS Metal Release 使用 Retina 实际 256×256 帧缓冲完成 `engine.scene_hdr_transparency` 和 `engine.hdr_extension_capture`。透明测试逐像素核对去除边缘后的 240×240 区域：受光背景及两层 PBR 表面分别与解析参考比较，验证 straight alpha、premultiplied alpha 的线性排序合成；测试参考的反向排序红通道差大于 0.1。合成参考 RGB 为 `1.37001 / 0.45327 / 1.00331`，两种模式中心均为 `1.36914 / 0.45288 / 1.00293`。非受光透明层覆盖受光背景时，RGB 与解析参考比较，HDR 目标 alpha 表示的 tone-map 权重由 1 降到 0.5；同时验证不透明表面的深度遮挡。启用 4 倍曝光、Filmic 和 Bloom，捕获仍为后处理前的线性结果。

扩展测试在实际 `RenderExtension` 上绘制线性 RGB `2 / 0.75 / 0.125` 色块，逐像素核对扩展区域和 sRGB 解码后的背景；关闭扩展、开启扩展及开启后处理三种情况下，整张图的最大 RGB 绝对误差均为 `0.000027`。以上测试覆盖指定场景，不单独覆盖透明边缘、MSAA 下的透明合成、扩展与透明网格的混合或深度关系；扩展测试只核对 RGB，不核对其目标 alpha。

`engine.destruction_runtime` 验证碎块组装保留已有切线及手性，缺失切线按实际 UV 分块生成；同步及异步破碎后均检查数据。macOS Release 测试通过，相关 Debug 和 Release 目标均已编译。


## 大气天空与 Hex-Tiling 标准 PBR 接入（2026-11-04）

本轮专项验收使用 **macOS ARM64 / Metal / Release**，真实绘制分辨率为 `384×216`，通过正式
场景、材质、平面反射、线性 HDR 回读和离线烘焙所用的 diffuse capture 接口运行。
`engine.atmosphere_hex` 为 `1/1` 通过（该次 2.04 秒是测试运行时间，不是渲染性能结论）。
专项回归可用根 Preset 重跑：

```sh
cmake --build --preset ninja-engine-release --target engine_atmosphere_hex_test
ctest --preset ninja-engine-release -R '^engine\.atmosphere_hex$' --output-on-failure -V
```

| 范围 | 实际结果与参考 | 验证边界 |
| --- | --- | --- |
| 纯大气背景 | 只有相机、环境和太阳，没有占位 Mesh；线性 HDR 图像均值 `0.05902850`，太阳强度翻倍的均值误差为 `0` | 证明背景可独立绘制和辐照度线性关系，不代表对摄影天空或 Blender 的逐像素拟合 |
| 显示转换 | 修改曝光及 tone mapping 后，显示转换前的 HDR 捕获保持相同 | 天空输出线性值，经现有唯一显示 resolve；没有引入示例内置曝光／tone map 的第二次处理 |
| 烘焙捕获 | 独立 40 m 高度的捕获随太阳强度翻倍；完成后主相机图像不变；未列入捕获灯光清单的太阳 Transform 变化仍取消旧捕获 | 复用离线烘焙的捕获接口，未宣称已实现随时间自动更新的 IBL |
| 水面平面反射 | 开启真实水面 Planar 后天空的平均贡献为 `0.00938355`；该反射贡献随太阳翻倍的均值误差为 `0.00001642` | 比较分别扣除两档太阳下无 Planar 时的水体结果，以隔离吸收、折射和固有颜色；没有把独立水体项视为太阳反射或放宽阈值 |
| Hex 开关 | 重复有色纹理开启后的均值差异为 `0.13712553`；关闭后的像素复原误差为 `0`；平法线和白 ORM 保持原 PBR 响应 | 只验证所列纹理、视角和参数，不代表全部材质的采样成本或视觉质量 |
| Hex 方向法线 | 已知 tile 的四分之一旋转与独立编写的参考法线贴图相比，HDR 均值误差为 `0`；关闭随机旋转时负 UV 尺度的方向法线一致 | 正确应用 UV 与梯度互为转置的变换；按 bgfx 实际矩阵约定，Metal 该样点的 +T 对应 +B，未以未旋转 RGB 法线混合代替 |
| 生命周期 | 上述测试包含开关反复切换、删除水面、场景销毁和请求清理 | 源参数唯一存组件／材质，LUT 与提交状态仅为场景派生 GPU 缓存；更多跨模块往返由集中回归记录 |

三个大气 Compute、天空 VS/FS，以及包含 Hex 的 PBR、Surface 和 Water 片元共 **8 个 Metal
Shader stage 编译通过**。场景数据测试另增加完整参数往返、非法相函数、空太阳和预制体引用
重映射／退休引用用例；材质测试增加默认关闭、精确布尔值、contrast 端点以及透明／cutout／
SpecGloss 组合拒绝用例。这些 CPU 用例与 Debug 编译的集中结果应按各自回执记录，不能从
本项 GPU 测试的 `1/1` 推断全仓已通过。

大气参考固定 bgfx `53-sky2`，Hex 网格／采样参考 `49-hextile`，法线梯度参考固定 MIT 原版
mmikk；版本、版权和适配范围见[来源与许可](../third_party/notices/bgfx-atmosphere-hex.md)。
参数和所有权见[类图](runtime/class-diagrams/atmosphere-hex-material.md)。专项日志位于
`build/upstream-atmosphere-hex-ctest-release.log`，定向构建日志位于
`build/upstream-atmosphere-hex-build-release.log`，均为忽略目录中的本地证据，不提交产物。

首期限制：大气使用 Y-up、米制场景到千米 LUT 的近地模型，没有空气透视、逐表面太阳透过率、
自动动态 IBL 或轨道级相机支持。Hex 初版限定 UV0、不透明、无 alpha cutoff 的标准
Metallic/Roughness PBR；SpecGloss、透明水、三平面和直方图预处理不在本次范围。BaseColor、
Normal、ORM 及其余已绑定表面纹理共用随机 UV、显式梯度和权重，Lightmap UV2 保持独立。
本轮没有摄影天空对照、广泛粗糙度／法线素材矩阵、其他后端运行或采样成本测量；不据此宣称
Windows、Android、iOS、Linux 或移动设备已验收。共享依赖的 macOS 部署目标警告仍遵守本文
既有发行边界，不能以本机运行替代较旧 macOS 系统验收。


## 上游渲染能力与统一 MCP 功能验收（2026-11-04）

本轮使用 **macOS ARM64 / Metal / Release / QuickJS**。Debug 与 Release 最终完整构建均成功（退出码 0）；Debug 未运行测试。Release 全量首轮为 **127/131 通过**，四项失败分别涉及测试夹具缺少文件、烘焙暂存资源缺少邻接 meta、新菜单项改变首个 Tab 焦点、测试扩展硬编码旧 view。修复后定向复验 `engine.window_render` 为 **1/1 通过**，`editor.texture_pipeline`、`engine.font_ui_render`、`editor.scene_lighting_files` 为 **3/3 通过**；没有再次执行单次全量，不能将这些结果写成单次 131/131。最终增量组件、绑定及 MCP CPU 回归另为 **9/9 通过**，日志 `build/upstream-rendering-components-final-tests.log`。

`engine.render_submission` 在首轮全量中通过（1.41 秒是测试运行时间）。它通过真实 GPU 读回验证逐物体/实例化图像一致、材质变化分批及提交计数；剔除、LOD 选择、导入、组件数据和脚本/MCP 边界由相应模块测试覆盖。本机实际采用忽略的 `local-ninja-debug/release` 配置覆写依赖位置；复跑使用仓库共享预设，不要求存在本机的 `CMakeUserPresets.json`：

```sh
cmake --preset ninja-engine-release -DENGINE_ENABLE_GPU_TESTS=ON
cmake --build --preset ninja-engine-release
ctest --preset ninja-engine-release --output-on-failure
```

新增公开用例 `Rendering features and submission`，由菜单进入 `scenes/render-features.scene`。固定 384 个共享球体与 1 个贴图地面；Camera 剔除、每个 MeshRenderer 的实例化/LOD 在该用例显式开启，核心三个新属性的默认值仍为 false。项目 TS 只组合组件和材质，实际提交与诊断通过正式 MCP 查询和控制。复跑真实 UI/MCP 验收时先在第一终端启动编辑器、等待资源导入完成，再在第二终端执行脚本：

```sh
build/ninja-engine-release/editor_app \
  --project projects/showcase --runtime-dir build/upstream-rendering-runtime \
  --width 1280 --height 720
```

```sh
python3 projects/showcase/tests/render_features_test.py \
  --server build/ninja-engine-release/mcp_app \
  --runtime-dir build/upstream-rendering-runtime --timeout 180
```

本次脚本真实退出码 0，输出：

```text
PASS 384 个固定对象：真实 UI/MCP 功能开关、导入 LOD、逐视图提交、调试、天空/Hex 与返回清理
```

窗口逻辑尺寸为 1280×720，Retina 实际捕获为 **2560×1440**。同一固定相机、同一 385 个对象得到以下功能对照；这不是性能分数，也未收集 CPU/GPU 耗时：

| 功能 | 开启 | 关闭 | 实际检验 |
| --- | --- | --- | --- |
| 视锥剔除 | 剔除 14，可见 371 | 剔除 0，可见 385 | 改相机属性后实际候选与提交集合变化 |
| 静态实例化 | 3 draw、2 实例批次 | 371 draw、0 实例批次 | 同一对象/材质恢复逐 draw 路径 |
| 静态 LOD | 440332 三角形 | 1515532 三角形 | 保持同一相机和可见对象，使用正式导入 LOD |
| 远镜头 LOD | 196620 三角形 | 1572876 三角形 | 同一远镜头分别开关 LOD；源拓扑为 4096/2048/1024/512 三角形 |

原生统计标注 `source: scene_mesh_submission`、`gpuCompletion: not_measured`；renderer 只读汇总明确为 `scope: world_summary`，包含常驻场景，指定组件为 `scope: component`。材质 Hex 的 `effective: null` 明确没有逐物体测量，不能从别的物体批次推断其效果。

该次脚本还验证了：正常 RmlUi 点击进入、切换与返回；Camera/MeshRenderer 单对象编辑、撤销/重做不修改其他对象；太阳 NodeID 的读写往返及错误角色拒绝；天空和 Hex 的 UI/MCP 开关；五项临时诊断与实际调试线；错误身份/缺少目标/只读能力拒绝；两次进入和返回后对象、UI 及请求清理。脚本不启动或关闭编辑器，成功后停留菜单；此次验收随后经正式 `close_editor`（delayMs=500）正常关闭，同一项目、运行目录和会话的回执成功，编辑器退出码 0，回执为 `build/upstream-rendering-editor-close.json`。

截图人工核对可见球体当前 LOD 线框、大气关闭后的背景变化、Hex 地面混合；调试线在原图有 205 个黄色像素（x=1279，y=607..811），没有以设置接受代替实际绘制。截图只用于该次功能检查，不宣称全部材质/模型或其他后端通过。首期实例化仅覆盖资格兼容的静态不透明网格，其他对象走正常逐 draw；LOD 仅消费离线生成的静态级别，无平滑过渡；大气与 Hex 的进一步限制见上一节。

证据均在忽略目录，本次真实 MCP 回执、结果和五张截图位于 `projects/showcase/cache/render-features/20261104T104733Z-b5d87fc1/`；构建日志为 `build/upstream-rendering-build-debug-final.log`、`build/upstream-rendering-build-release-final.log`；全量及复验日志为 `build/upstream-rendering-ctest-release.log`、`build/upstream-window-recheck-ctest.log`、`build/upstream-rendering-ctest-recheck.log`。其他检出通过上述命令重新生成证据，不提交缓存或截图。


## 全模块架构与文档整理回归（2026-11-04）

架构结论与后续收敛点见[架构审查](architecture.md#全模块架构审查2026-11-04)。本轮没有更改公开运行契约或渲染算法，主要调整内部文件归属、编辑器网格处理目标名、文档与工具路径。窗口/显示设备等独立服务保持直接接口，场景效果和对象属性继续由组件承载。

- macOS ARM64 的 Debug、Release 完整增量构建均返回 0；Debug 未运行测试。日志：`build/architecture-audit-debug.log`、`build/architecture-audit-release.log`。已有依赖的 macOS 最低部署版本链接警告仍存在，不据此声明更低系统版本通过。
- Release 定向 CTest **11/11 通过**，覆盖 Android MCP 传输、MCP 登记、LOD 与导入、范例契约、工具路径、跨语言 schema、插件选择、架构边界/规则和角色控制器。日志：`build/architecture-audit-release-tests.log`。其中 Android 使用 fake adb 设备，只验证传输与失败处理，不是真机运行。
- 导入插件合并前，架构检查对当时配置导出的 **168 个 engine 目标**与自有源码直接包含校验，边界错误为 0；平台代码现已参与扫描，平台编辑器插件仍保持 plugins/editor 身份。规则夹具 **10/10** 通过，包含平台反向依赖反例。
- 原有 **128 份类图**的符号/继承/可见性、链接和围栏结构筛查完成；导入插件合并另补充统一关系图，当前共 129 份；参数、所有权的人工核对范围以[架构说明](architecture.md)为准。符号筛查不等于全量参数类型证明，未运行 Mermaid 渲染器或开展新 GPU/性能验收。
- 项目文档集中到 `projects/showcase/docs/` 后，额外运行 `editor.showcase_contract` 与 `engine.asset_tools`，**2/2 通过**（日志：`build/architecture-audit-project-docs-tests.log`）。18 篇原文迁移并新增目录入口，项目根/工具/资源目录不再散放 Markdown；3 份随资源分发的许可证未移动，移出的两份来源说明不再登记为运行资源。

复跑 C++ 构建使用[根预设](building.md)；本机日志中的 `local-ninja-*` 是继承 `ninja-engine-*` 的本地依赖缓存预设。对应 Release 回归可在已构建的根预设执行：

```sh
ctest --preset ninja-engine-release --output-on-failure -R \
  '^(engine\.(architecture_boundaries|architecture_rules|android_mcp|mcp_registry|character_controller|plugin_selection|scene_schema_cross_language)|editor\.(mesh_lod|mesh_lod_pipeline|showcase_contract|portable_tools))$'
```


## 统一资源导入插件回归（2026-11-04）

五个纯格式插件合并为 `editor/asset_import`，统一 CMake 目标/命名空间与宿主开关；Importer ID、转换算法、meta 与资源 GUID 契约保持原样。格式说明及选择方法见[资源导入插件](plugins/editor/asset-import.md)，类归属见[统一类图](plugins/editor/class-diagrams/asset-import.md)。

- 启用配置：macOS ARM64 的完整 Debug、Release 构建均返回 **0**；Debug 未运行测试。实际 engine 目标从合并前的 168 个减少到 **164 个**，五个旧格式目标不再创建，只有一个 `engine_asset_import`。日志：`build/asset-import-configure-{debug,release}.log`、`build/asset-import-build-{debug,release}.log`。
- Release 定向 CTest **21/21 通过**：包含 FBX 导入/绑定姿态、USD、字体、JSON、UI、真实字体/UI 渲染、脚本 UI、资源管线与出包、编辑命令、插件选择和架构边界。日志：`build/asset-import-release-tests.log`。此结果不代表本轮重跑全部 131 项或其他平台。
- 插件选择测试验证统一插件的五种实现、实际目标依赖、宿主单次链接、UI 的依赖闭包、禁用后目标/feature 清理，以及旧插件名被明确拒绝。该测试的目标夹具不冒充第三方实际编译，真实链接由完整构建与下一项覆盖。
- 禁用配置：使用根 Release 预设的独立构建目录 `build/asset-import-disabled`，只选择编辑器 `asset_build;asset_sync;mcp`，实际编译/链接 `editor_app`、`mcp_app`、`asset_build_app` 成功；三个 `--help` 均返回 **0**。无 `engine_asset_import` / `engine_ufbx` 目标，配置及实际链接不包含 USD/FreeType；未运行项目或重新生成资产。日志：`build/asset-import-disabled-{configure,build,verification}.log`。

构建沿用已安装依赖及编译缓存；未更新依赖版本、触发 CI 或改变运行包格式。原有 macOS 依赖最低部署版本警告继续按前文边界记录，不作为低版本设备或跨平台实测。


## 场景查询与内部职责收敛回归（2026-11-04）

本轮完成[架构审查中的职责收敛](architecture.md#本次职责收敛)：按类型拆分场景编辑命令，将名称/局部身份/子路径查询归 Scene / Node，建模源文件 I/O 归插件私有实现，并移除 `scene_bake` 对依赖图插件的强制装配。公开查询按[Scene 类图](runtime/class-diagrams/scene.md#场景查询收敛契约)与[Node 类图](runtime/class-diagrams/node.md#相对子路径查询)实现；未新增查询系统或命令框架。

- macOS ARM64 / Metal / QuickJS 的 Debug、Release 完整增量构建均返回 **0**；Debug 未运行测试。构建使用继承根预设的 `local-ninja-debug/release`，日志为 `build/scene-convergence-build-debug.log`、`build/scene-convergence-build-release.log`。
- Release 定向 CTest **22/22 通过**，总运行时间 76.66 秒，日志为 `build/scene-convergence-release-tests.log`。覆盖场景/ECS、脚本/角色、编辑命令/预制体/保存、建模/MCP、插件闭包和架构边界；此结果不等于全量 132 项或其他平台验收。
- 命令组合回归覆盖 Transform、Camera、材质与 Light 的同批次编辑、陈旧撤销拒绝及已撤销部分的回滚、未编辑字段保留、创建撤销/重做后的新实体身份、旧外部 ID 拒绝，以及 `get_scene` 多材质槽快照。原事务、NodeAliases 和 JSON 形状保持。
- 查询回归覆盖实体/局部 ID 区分、不同场景和常驻节点隔离、候选内部查询与 Published 边界、删除重建、同名顺序、空名及含斜线名称、非法/歧义路径、重挂父级和旧脚本 Run 拒绝。TS 外观未改变。
- 架构检查通过自有源码包含和实际 CMake 目标依赖校验；插件选择用例确认空运行时插件列表仅选择 `editor/scene_bake` 时不会隐式加入 `runtime/resource_graph`。

新增 `editor.scene_bake_core_loader` 是独立的真实 GPU 可执行测试，既不链接也不构造 ResourceGraph。核心默认加载器从正式 GUID 目录和挂载读取输入，场景组件/材质持有网格、Shader、贴图；测试完成纹理捕获、IBL、UV2、Lightmapper 和生成光图正式资源回载。最终 Release 回归耗时 6.01 秒，实际输出：

```text
core-loader bake: graph=absent IBL=0.999512 lightmap=3.144531 expected=pi captures=complete
```

常量环境的 IBL 目标值为 1，光图目标值为 π，光图断言为 `abs(mean - pi) < 0.03`。GPU 用例仅在开启 `ENGINE_ENABLE_GPU_TESTS` 且非交叉编译时登记；测试输出详见 `build/local-ninja-release/Testing/Temporary/LastTest.log`。该验证证明无依赖图装配的所列资源与烘焙链路，不代表所有烘焙素材、后端或平台。

其他检出沿用[构建指南](building.md)准备根预设与 GPU 测试，再执行相同范围：

```sh
cmake --preset ninja-engine-debug
cmake --build --preset ninja-engine-debug
cmake --preset ninja-engine-release -DENGINE_ENABLE_GPU_TESTS=ON
cmake --build --preset ninja-engine-release
ctest --preset ninja-engine-release --output-on-failure -R \
  '^(engine\.(script_runtime|scene|ecs_scene|plugin_selection|architecture_boundaries|architecture_rules|character_controller|character_host|ui_script)|modeling\.(commands|pipeline|mcp)|editor\.(physics_audio_commands|commands|mcp_commands|scene_persistence|animation_commands|model_commands|character_controller|scene_bake_core_loader|scene_lighting_files|prefab_commands))$'
```

构建缓存与日志留在忽略目录，不提交产物或本机依赖路径。既有第三方库的 macOS 最低部署版本警告仍在；本轮没有低版本 macOS、跨平台运行或性能测量。

## 2026-11-04：参数驱动动画、重定向与制作命令

平台为本机 macOS ARM64，脚本后端 QuickJS；只在 Release 执行功能测试，Debug 仅编译。此次范围是动画功能及其直接资源／脚本／宿主链路，没有开展敌人性能基准。

实现入口与边界见[动画模块](runtime/animation.md)、[三层动画接口](runtime/scripted-animation-controller.md)、[制作 MCP](plugins/editor/class-diagrams/animation-authoring.md)。编辑器只通过 MCP 和配置文件操作，原生窗口用于预览／运行，不添加面板、时间线或图形状态机编辑器。

| Release 用例 | 验证范围 |
| --- | --- |
| `engine.animation_state_machine_script` | 一份不可变定义、八个实例的参数／时钟／Trigger 隔离；原子参数提交、过渡打断、暂停／自然完成、Run 身份与同步条件 |
| `engine.animation_retarget` | 异名、参考姿态与模型轴差异、目标附加父骨、目标骨长、hips 策略、共享并发与失败保持；正式 GUID 资源读取 |
| `editor.retarget_importer` | 独立 profile 的 meta／GUID／源目标骨架依赖、增量与原子发布；坏路径／参考姿态拒绝并保留旧版本 |
| `resource.skeleton_rig_definitions` | socket 唯一性、精确骨路径、轴、祖先链与数值校验 |
| `engine.animation_events_script` | 真实 AnimationSystem→Run 主线程分发；标量载荷、跨循环、角色隔离、解除、回调内重播／清轨／销毁／停用、同步回调与 IK 输入校验 |
| `engine.animation_rigging_events` | 原子播放失败、完成、零时刻／跨圈／事件预算，model／world 骨矩阵、inverse mesh binding、挂点与子树、源组件替换、detach、同帧主手挂点驱动副手 IK 与反馈拒绝 |
| `editor.animation_authoring` | 原 FBX 邻接 meta 的事件／socket／IK 编辑，正常重导入，撤销／重做、hash 防冲突、错误拒绝与 batch 回滚 |
| `engine.animation_event_interval` | 开闭区间、多圈顺序、同相位标记、预算整批拒绝与大周期精度边界 |
| `engine.animation`、`engine.model_animation_script`、`editor.animation_commands` | 原播放、混合、暂停／停止、旧脚本和编辑器命令兼容 |
| `engine.plugin_selection` | 可选 runtime retarget 独立装配，制作插件复用 FBX 导入，未选择不拉脚本或其他运行插件 |

可复现命令（根预设已选中对应插件）：

```sh
cmake --build --preset ninja-engine-debug --target editor_app engine_app asset_build_app mcp_app
cmake --build --preset ninja-engine-release --target editor_app engine_app asset_build_app mcp_app \
  engine_animation_state_machine_script_test engine_animation_retarget_test \
  engine_retarget_importer_test engine_skeleton_rig_definitions_test \
  engine_animation_events_script_test engine_animation_rigging_events_test \
  engine_animation_authoring_commands_test engine_animation_event_interval_test \
  engine_animation_test engine_model_animation_script_test engine_animation_commands_test
ctest --preset ninja-engine-release --output-on-failure \
  -R '^(engine\.(animation|animation_event_interval|animation_retarget|animation_state_machine_script|animation_events_script|animation_rigging_events|model_animation_script|plugin_selection)|editor\.(retarget_importer|animation_authoring|animation_commands)|resource\.skeleton_rig_definitions)$'
node projects/showcase/settings/typescript/node_modules/typescript/bin/tsc \
  --project projects/showcase/settings/typescript/tsconfig.json --noEmit
```

最终结果：受影响 Debug／Release 宿主与测试目标构建均成功；Release 上述用例及 `editor.fbx_pipeline` 共 **13/13 通过**（9.82 秒），TS 类型检查通过。完整输出保留在 `build/scripted-animation-final-tests.log`。

本机使用继承上述预设的 `local-ninja-debug/release`，复用已安装依赖。日志保留在忽略目录 `build/scripted-animation-*-final.log`、`build/animation-final-native-tests.log`；不将本机依赖路径写进共享预设。已知依赖带有“macOS 27 构建、13.3 链接”警告，这不是 macOS 13.3 的实际运行验收。

公开范例从菜单打开 courtyard：正式资源管线导入后，用正常游戏 UI 进入，MCP 读取八个 Animator 的状态与播放身份；独立 Idle／Walk／Run 同时存在。返回菜单后重进创建新实例；停止后 UI 文档清空，再用延迟关闭命令正常退出。详细操作和临时待机表现见[项目说明](../projects/showcase/docs/animation-state-machines.md)。本机回执保留在 `build/scripted-animation-showcase-functional-receipts.jsonl`，不将这些缓存加入 Git。

限制：当前只验证共享实例和功能正确性，不宣称 128 或更多敌人的性能达标。重定向差异覆盖来自合成骨架，真实多角色素材的脚底接触与美术效果仍需逐资源验收。基础 IK 不包含脚锁／全身求解／末端手掌方向锁；跨角色、递归或反馈目标明确拒绝。带主动碰撞、刚体或导航控制的挂件暂不允许跟随，以免 Transform 与物理双写。制作 MCP 的 `reimportPending` 不表示导入已发布，需等待正常管线回执；没有可视化制作面板。
