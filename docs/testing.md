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
npm ci --prefix projects/showcase/settings
cmake --workflow --preset ninja-engine-debug
cmake --workflow --preset ninja-engine-release
```

两个 workflow 包含脚本准备、构建和测试。已准备过且依赖未变时，可分别执行 `cmake --preset ninja-engine-debug`、`cmake --build --preset ninja-engine-debug`、`ctest --preset ninja-engine-debug --output-on-failure`；Release 使用对应预设。针对 C++ 改动运行受影响的两种配置，不因普通回归清除共享依赖或缓存。测试集合随所选插件变化，不能用更小配置的结果代替完整配置。

showcase 的独立 TS 类型检查入口为：

```sh
node projects/showcase/settings/node_modules/typescript/bin/tsc \
  --project projects/showcase/settings/tsconfig.json --noEmit
```

已知 `engine.scene_schema_cross_language` 失败及其范围见[已知失败与限制](#已知失败与限制)。本页保留失败事实，不以筛掉该测试的结果宣称全仓通过。

### 真实窗口与 GPU 测试

普通 CTest 不要求图形桌面；窗口测试需显式开启 `ENGINE_ENABLE_GPU_TESTS`，并在有真实图形会话的机器串行运行。完整引擎示例：

```sh
cmake --preset ninja-engine-debug -DENGINE_ENABLE_GPU_TESTS=ON
cmake --build --preset ninja-engine-debug
ctest --preset ninja-engine-debug --output-on-failure
```

Release 使用对应的 `ninja-engine-release`；不要让两个配置的窗口测试同时抢占窗口。若只验证基础窗口，原最小预设命令仍可使用：

```sh
cmake --preset ninja-debug -DENGINE_ENABLE_GPU_TESTS=ON
cmake --build --preset ninja-debug
ctest --preset ninja-debug
```

- `engine.window_render`：真实后端、场景绘制、效果开关、窗口生命周期与 GPU 资源回收。
- `engine.font_ui_render`：多字体/Shader/图片、跨文档焦点、3D 文字、缩放与反复启停。
- `engine.input`：平台窗口、输入映射、设备配对与触摸路由。
- `editor.project_lifecycle`、`editor.mcp_commands`：真实编辑器、TS 编译、MCP 启停/重载、场景编辑、批量回滚、撤销与会话隔离。

这些测试仅在相应插件与 GPU 开关启用时登记；协议、文件传输和一般命令测试不要求 GPU。`engine.architecture_boundaries` 检查源码包含方向、实际目标链接方向及循环，`engine.architecture_rules` 检查规则的正反例。

### 启动和停止公开项目

`projects/showcase/` 共用一套设置、资源目录和缓存。完整桌面预设默认装配菜单所需插件；显式裁剪插件时需按对应模块文档补齐运行时与编辑器清单。

```sh
build/ninja-engine-debug/editor_app \
  --project projects/showcase --runtime-dir build/showcase-runtime
```

编辑器空白待机后，在另一终端启动项目：

```sh
python3 scripts/mcp_client.py --server build/ninja-engine-debug/mcp_app \
  --project projects/showcase --runtime-dir build/showcase-runtime start
```

Windows 使用对应 `.exe`。选择菜单场景后，先聚焦引擎窗口再操作；`Back to scenes` 返回菜单。使用同一条 MCP 命令的 `stop` 停止项目，检查场景与相关资源释放、窗口恢复空白。项目目录和运行目录必须与编辑器实例一致。

默认 `assets/scripts/game.ts` 导出 `modules/menu.js`，无需逐场景改入口。修改脚本源文件后，通过 MCP 的 `stop_project → sync_assets → start_project` 更新缓存，或关闭后重新打开编辑器；只停止再启动不会重新导入源码。生成资源、截图、日志和 `node_modules/` 不提交。

### 截图与 MCP 回执

截图必须来自实际渲染器。只有收到 `complete` 并检查 PNG 文件有效后，才把截图作为完成证据。命令若返回 `activation: pending`，按原 `session/requestId` 查询 `get_editor_receipt`，不要重复提交原操作。复用客户端见 [editor_mcp_session.py](../scripts/editor_mcp_session.py)。

## 公开范例

### 功能用例与常驻 UI

菜单按功能组织 12 项用例，稳定 ID、标题、目标、场景和 Gameplay 入口统一定义在项目 [showcase_cases.ts](../projects/showcase/assets/scripts/models/showcase_cases.ts)。逐项操作、预期结果、自动覆盖和待补测试见[范例功能覆盖](editor/showcase-coverage.md)；不再在菜单与文档各维护一套场景注册。

项目行为测试放在 `projects/showcase/tests/`，核心算法、组件和协议测试继续归 `tests/`。测试须标明使用脚本替身还是实际引擎；脚本状态机检查不能代替 GPU、Jolt、RmlUi 和真实窗口的验收。MCP 工具覆盖见[模块清单](plugins/editor/mcp-coverage.md)。

菜单 UI 根通过 `Node.SetPersistent(true)` 创建、绑定一次，切关复用原文档与 Controller，停止或热重载时完整清理。`UI and scene lifetime` 可检查换关、取消、加载失败、无活动关卡时输入和显式销毁；输入框内容应随原文档保留。生命周期契约见[常驻节点](runtime/class-diagrams/persistent-nodes.md)。

### 现代公寓

[apartment.scene](../projects/showcase/assets/scenes/apartment.scene) 保存客厅、卧室、厨房与餐区，包含 322 个可编辑模型、相机及 5 个灯光节点。家具、摆件、倒角、变换、材质和灯光由 [create_modern_apartment.py](../scripts/create_modern_apartment.py) 通过正式 MCP 工具制作。

正常运行时从菜单选择 `Authored scene and local lights`。如需脚本切镜头与截图，可使用已构建的 Release 编辑器，在进入公寓后运行：

```sh
build/ninja-engine-release/editor_app --project projects/showcase \
  --runtime-dir build/apartment-runtime --width 1600 --height 1000
```

另开终端用相同目录启动项目并在菜单进入公寓，再调用：

```sh
python3 scripts/mcp_client.py --server build/ninja-engine-release/mcp_app \
  --project projects/showcase --runtime-dir build/apartment-runtime start
python3 scripts/create_modern_apartment.py --view-only --configure-view \
  --view wide --capture screenshots/apartment-wide.png
python3 scripts/create_modern_apartment.py --view-only \
  --view living --capture screenshots/apartment-living.png
python3 scripts/create_modern_apartment.py --view-only \
  --view kitchen --capture screenshots/apartment-kitchen.png
```

脚本默认连接 `build/ninja-engine-release/mcp_app` 和 `build/apartment-runtime`；其他构建或运行目录须显式指定 `--server`、`--runtime-dir`。`--start` 只负责启动项目，不会代替菜单选择。`--view bedroom` 切卧室，`--dry-run` 只打印构图清单；`--save-only` 保存当前场景。不带 `--view-only` 会复用或创建模型并重设示例布局。

截图在项目 `cache/screenshots/`，MCP 回执默认在 `build/apartment-mcp-receipts.jsonl`。场景参数已经落盘；`--configure-view` 修改组件后可通过 `save_scene` 保存。该例采用几何造型、颜色材质、实时 PBR、方向光阴影与局部点光，不包含烘焙光照或 IBL。

### 动画庭院与第一／第三人称

进入 `Character, animation and camera` 后，WASD 移动，Space 跳跃，按住鼠标右键转向，Tab 切换视角。第一人称仅在自身镜头隐藏身体和头部，身体阴影与旁观镜头中的角色应保留；第三人称镜头通过球体检测避开墙体。`Back to scenes` 返回菜单。

庭院有铺装、立面、台阶、廊架、长椅、绿植及带物理的 Player/PAB 蒙皮角色。环境由 MCP 创建并保存：231 个节点、225 个环境 Mesh、47,004 个环境三角形、31 个静态碰撞体；三棵树共 840 片薄叶，每树合并为一个 Mesh。人物在场景加载后实例化，避免保存后重复生成。贴图来源和处理见[庭院资源说明](../projects/showcase/courtyard-source.md)。

范例默认使用 InPlace，位置由物理或导航推进。导航、物理交互及地形范例按速度调节动作倍率；庭院仍固定循环 walk，用于检查蒙皮，不是动画状态图。该场景尚未提供触屏摇杆。样例源法线反向，使用 FBX `.meta` 的 `normals="generate"` 导入，未修改源 FBX。

动画导入、动作切换、坐标适配与尚未实现的重定向／Root Motion／IK 范围见[动画模块](runtime/animation.md)；角色制作的后续方向见[制作流程](editor/character-production.md)。这些规划不属于本场景已经通过的验证。

### 射击靶场

`Ray and sphere hit queries` 打开[独立靶场](../projects/showcase/assets/scenes/shooting-range.scene)，包含近靶、远靶和蓝墙后的遮挡靶。WASD 移动，按住鼠标右键观察，左键或 `Fire` 单发射击，Space 跳跃，Tab 切视角。

- `Ray` / `Sphere 0.12 m` 切换查询方式；墙体应阻挡射击。
- 命中显示落点、移除靶面并更新计分；R 或 `Reset targets` 恢复三靶与计分。
- 全部命中后可重置再玩；`Back to scenes` 返回菜单。

这是从相机近裁剪面发出的即时命中范例，尚无枪口弹道、弹药、敌人 AI 或伤害系统。触屏沿用移动／观察区域和 `Fire` 按钮，未在移动设备实机验证本场景。查询身份、过滤和失效语义见[物理模块](runtime/physics.md)。

### 多角色导航

`Crowd and terrain navigation` 提供 16／32／64 个角色，四组交叉移动或两组通过 2.4 米窄门；可反转目标、停止、继续、重置和切观察镜头。每次进入等待地形就绪后构建共享导航面，切换角色数量不重复烘焙。参数和按钮见[范例说明](../projects/showcase/navigation.md)。

挖坑、切断门洞地面和恢复地形后，观察地形版本、导航版本及本次重建瓦片数；断路时代理应停住，恢复连通后沿原目标重新排队。停止群体再修改地形，代理应保持 idle；返回菜单再进入应创建新场景和代理。

此例只提交静态地形；移动残骸不会自动进入导航，脚下地面消失后也没有与物理下落的控制权交接。局部重建仍同步发布，局部避让不保证双向窄门限时无拥堵。核心和插件契约见[导航模块](runtime/navigation.md)。

### 物理交互

`Physics queries and contact events` 打开[物理交互范例](../projects/showcase/physics-interactions.md)：两条路线使用 CharacterMotor，三条使用 NavigationAgent 与零速度运动学 sensor 胶囊，移动由原生系统推进。

选择路线后沿蓝线执行 Raycast 或半径 0.12 米的 SphereCast，观察对象类型、实例身份、距离和命中点。球形／轴对齐盒范围查询将对象标绿，橙色 Trigger 统计 enter/stay/exit。按钮提供暂停、继续、反转路线和清空统计；返回菜单释放角色及订阅。

该场景观察统一物理身份、范围查询和接触生命周期，不含伤害或战斗规则。脚本回归模拟宿主边界，Jolt 与 Detour 行为由原生测试和实窗分别验证。

### 特效实验室

`Particle and model effects` 显示两个公开上游例子：Laser 粒子和带 UV 动画的 Arrow 模型，均通过普通节点的特效组件使用。可检查同时显示、独立暂停／恢复／停止、批量创建、删除撤销和重做重建后的播放，再返回菜单及停止项目检查释放。展台关闭方向光阴影，方便观察特效本身。

历史 Metal 实窗分别批量创建了 32 个粒子特效和 32 个模型特效；这是功能检查，不是容量结论。支持的官方效果类型与尚未接入的扭曲、软粒子等能力见[特效类图](plugins/runtime/class-diagrams/effects.md)。

### 地形实验场

`Terrain editing and restoration` 打开 `scenes/terrain.scene`：64×64 米、32 米高的自制地形。可挖隧道、爆炸挖坑、填充、保存／加载、重置，切换俯瞰／洞口观察及第三人称行走。角色行走仍需键鼠，触屏只操作实验按钮。

编辑器状态文件位于 `cache/terrain-states/*.terrainstate`，脚本状态位于 App 持久目录的 `terrain/<slot>.bin`；保存状态不覆盖源 `.terrain` 或导入缓存。塌方产生的动态土石不能写入旧静态体积存档，执行保存／加载或可撤销体积编辑前应先 Reset。笔刷、设置、重置和加载可检查编辑器撤销／重做；保存文件本身不属于场景撤销。

桌面和 Android 的资源清单均登记了该场景，登记不等于 Android 场景运行已经验收。阶段范围见[地形与破坏方案](plugins/runtime/terrain-destruction-plan.md)，技术和验证边界见[研究记录](plugins/runtime/terrain-destruction-feasibility.md)。

### 建筑破坏实验场

`Fracture bonds and support` 提供切片塔、Voronoi 墙、分段柱等实心试件。通过 UI 施加固定球形损伤、重置和切观察镜头；在编辑器预览中可检查损伤的撤销／重做。停止项目后应清空场景及生成节点。

输入仍为参数盒体，单资源最多 256 个碎块，不是任意复杂 FBX 建筑预破碎工具。断开锚点不保证立即落地，其他接触仍可能支撑碎块；实际位移由重力、碰撞和冲量共同决定。编辑快照也不是独立游戏存档槽。

macOS 阶段验收和 Android 探针的区别见[步骤 2 验证记录](plugins/runtime/terrain-destruction-feasibility.md#步骤-2-当前实现与验证范围)。

### 综合破坏与火箭场

`Destruction system integration` 组合地形、Blast、Jolt、Effekseer、音频和第三人称角色。步骤 4 增加同步应力、动态土石、静态导航瓦片更新及密集火箭场。按钮、参数与资源来源见[项目说明](../projects/showcase/destruction-integration.md)。

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

### 2026-11-03：预制体与公开范例迁移，macOS ARM64 / QuickJS

Debug／Release 全目标构建通过，两配置各通过 31 项定向 CTest：预制体字段合成、资源管线、运行时实例、编辑命令、场景保存、Model/FBX/USD、资源闭包、脚本实例化、架构边界和 5 项公开项目行为测试。覆盖同值/0/false 的明确覆盖、模板连续更新的稳定身份、引用重映射、失败回滚、材质恢复与撤销、缺失或临时实例的保存约束。另有 53 项 Python 资源打包/导出/架构规则检查通过；没有运行性能测试或 CI。

公开范例 10 个场景改为引用直接预制体，动态人物使用 FBX 关联预制体。迁移核对保留全部 851 个原节点的数据，静态环境增加 9 个恒等变换包装根。TS 类型检查通过；脚本替身测试不能代替真实引擎运行。制作、覆盖和首版限制见[预制体流程](editor/prefabs.md)。

本地构建与定向测试证据为 `build/prefab-build-*-final.log`、`build/prefab-ctest-*.log`；本轮未重跑既有跨语言 schema 失败项，不宣称全仓或其他平台通过。

公开范例全量导入计时：`projects/showcase/assets` 共 2,641 个文件，逻辑大小 81,356,707 字节（77.59 MiB，含源资源和 meta）。Release 导入工具使用 8 线程，2026-11-03 19:39:45.744 开始，19:41:44.079 完成，约 118.34 秒；回执为 `imported=1326 reused=0 removed=0 threads=8`。耗时按导入日志创建至完成回执写入统计，覆盖扫描、转换与发布，不包含 C++ 构建及打包。当时还有另一个项目并行导入，文件系统缓存也已被读取；这是实际全量资源重导耗时，不是独占机器或冷磁盘基准。日志为 `build/prefab-import-showcase-release.log`。

Release / Metal 真实编辑器已打开迁移后的方块、公寓、庭院场景，分别为 3、332、233 个节点。通过 MCP 创建两个角色预制体，验证其中一个的位移和 `enabled=false` 不影响另一个，明确覆盖查询、恢复模板位置、撤销/重做一致；关闭重开恢复源场景，项目菜单正常启停，最后 `close_editor` 退出码 0。回执和报告位于 `build/prefab-live/`。首轮验证遇到加载期间状态超过 MCP 的 5 秒存活窗口，客户端等待状态重新发布后完成；未将此扩展为其他模块重构。

公开范例按正常 `--package --target macos` 构建运行资源，产出 1,240 项资源、2 个包，完整运行依赖校验通过；其中保留脚本动态创建的角色预制体，静态场景模板已展开，不额外打入模板及来源 Model。打包回执为 `build/prefab-package-showcase.log`。

### 2026-11-03：架构收敛与节点状态，macOS ARM64 / Metal / QuickJS

Debug／Release 全目标增量构建通过。两配置各通过 50 项定向 CTest，覆盖组件身份、父子激活与可见状态、场景持久化、物理、音频、动画、导航、地形与破坏、脚本、编辑器撤销、资源文件发布、MCP 传输及真实窗口。另执行的 `engine.scene_schema_cross_language` 仍因既有 flatc 输入枚举 TS import 路径错误失败（`TS2307`），见[已知失败](#已知失败与限制)；不宣称全仓测试通过。公开范例 TS 类型检查通过。

状态回归包括父级恢复不覆盖子级局部标记、组件重建后旧脚本句柄失效、隐藏仍保留碰撞、UI 隐藏／停用保留文档及订阅、回调内停用后停止剩余事件投递，以及暂停期间生成碎片同步显示但不发布异步破坏候选。渲染实窗检查覆盖父级隐藏的实际像素变化；项目启停、MCP 编辑和延迟关闭仍通过真实编辑器验证。

架构收敛覆盖统一组件身份、唯一脚本预检启动路径、组件直接持有渲染设置、统一编辑命令通道、无状态 MCP 登记函数、资源元数据撤销快照及源文件／meta 发布共用函数。首轮新增测试的重复持锁已修复；MCP 边界测试通过同一会话检查所有非法 ID，保留原 60 秒上限及全部断言。重复持锁、超时两项问题及最后补充的状态用例均已定向复验通过。

本地证据为 `build/architecture-build-*.log`、`build/architecture-test-debug*.log`、`build/architecture-test-release*.log`、`build/architecture-typescript.log`。没有执行性能测试或 CI，未新增其他平台实测结论。

### 2026-11-03：Basis Universal 2.50 与测试目录整理，macOS ARM64

BasisU 2.50 的 Debug／Release 库已实际构建安装，两配置引擎全目标构建通过。各通过 15 项定向 CTest：贴图与资源导入、HDR 半浮点转码和预算、资源编解码、UI 导入、特效导入与运行、光照资源、渲染基础、脚本编译、架构规则及桌面／Android 资源打包；另各通过 `engine.window_render` 和 `engine.font_ui_render` 两项真实 Metal 测试。

贴图回归覆盖 PNG/JPEG、TGA/QOI、HDR/EXR、完整 mip、增量缓存、无效设置不发布，以及保留 KTX cube／LUT。HDR 检查大于 1 的亮度、65216 上限、透明通道和设备能力；不把有损 HDR 编码当作任意浮点数据存储。默认导入并发为 8，并验证项目配置仍能覆盖它。

114 个测试与辅助文件按模块迁移，CTest 名称及目标名保持不变；18 个 Python 文件的新位置编译／导入通过。迁移回归发现资源目录解析器漏接受 `light_probe=17`，修复后 Android 资源打包 28 项、桌面导出 16 项 Python 检查均通过。证据保存于忽略目录 `build/basisu-upgrade/`。

上述为定向功能回归，不是全仓测试或性能验收；没有新增 Windows、Android、iOS 实机结论。移动端关闭 BasisU CLI 的配置已保留。

### 2026-11-03：渲染升级，macOS Metal

Debug／Release 均完成 `engine_app`、`editor_app` 及受影响测试目标构建，每种配置通过 17 项相关 CTest，覆盖场景与资源编解码、脚本绑定、预算配置、烘焙数学／发布、反射数学、真实渲染、字体和特效。公开范例 TS 类型检查通过；额外跨语言 schema 测试仍失败，见[已知失败](#已知失败与限制)。这些结果不是全仓 CTest 全过声明。

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

- **跨语言 schema 测试失败（2026-11-03）**：`engine.scene_schema_cross_language` 使用 flatc 25.9.23，为既有 `InputControlState` 生成的 TS 枚举 import 路径不正确，报 `TS2307`。使用渲染修改前 HEAD 的 schema 复现同样问题；该轮未修改输入模块或绕过测试，不能宣称全仓全过。
- **Android 验收边界**：APK 为本机调试签名；单 `libmain.so` 包的 ZIP 与 ELF LOAD 16 KiB 对齐检查通过，但未在 16 KiB 页设备运行。adb 同步只支持 Debug 包，正式发布签名和内容更新渠道未实现；FNV-1a 仅作完整性检查。软件 Vulkan 不代表真机性能，纹理压缩格式矩阵仍待实机验证。
- **图片验证器差异**：固定 bimg 的部分带 alpha 图片往返因 `m_hasAlpha` 元数据不一致导致 `texturec --validate` 失败。已测样本像素一致，元数据兼容问题未修补。
- **渲染限制**：SSR 无屏幕外信息或时序降噪；透明按子网格排序，不解决交叉面的逐像素排序及多层折射。HDR 关闭时单通道权重只近似区分受光照／无光照贡献，加色与多层混合不能精确分离曝光。静态 GI 不随破坏实时重算，探针单点插值不能自动消除薄墙漏光；没有动态 GI 或自动局部反射立方探针。
- **物理与音频边界**：约束关节尚未提供；动态刚体不支持父节点，球要求等比缩放，胶囊横向缩放须一致。音频无压缩／流式长音频、混响和音频总线；自动测试使用 SDL dummy，不能替代扬声器听觉验证。
- **资源与角色边界**：FBX 未转换源材质、贴图、混合形状或双四元数蒙皮；USD 仅支持自包含静态范围。尚无通用动画状态图、运行时 Humanoid 重定向、Root Motion 驱动或 IK；控制器不附带触屏 UI。
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
