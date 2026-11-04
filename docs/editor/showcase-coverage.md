# 公开范例功能覆盖

`projects/showcase/` 以功能检查组织菜单，复用原有场景和资源。唯一用例清单位于 [showcase_cases.ts](../../projects/showcase/assets/scripts/models/showcase_cases.ts)：记录稳定 ID、按钮、显示标题、观察目标、场景和 Gameplay 构造函数。菜单 Controller 与项目契约测试共同消费该定义，新增功能在这里登记，不向引擎增加项目用例注册接口。

本页记录操作和覆盖边界；启动、构建、平台实测与证据索引统一见[测试与验证](../testing.md)。项目宿主替身回归与真实编辑器 MCP 交互分开记录；前者不能证明真实 RmlUi 布局、键鼠焦点、物理求解、声音或 GPU 画面正确。

## 菜单功能与验收动作

| 稳定 ID | 操作及预期结果 | 当前自动覆盖与剩余检查 |
| --- | --- | --- |
| `pbr-material` | 打开旋转方块，观察变换持续更新及材质受光；返回并重开 | 项目菜单生命周期；原生 `engine.window_render`。视觉高光仍需实际窗口确认 |
| `authored-scene` | 打开公寓，检查保存的模型、材质、相机及局部灯光；退出后释放场景 | 项目场景入口契约；`editor.scene_persistence`。家具遮挡和室内光照需实窗检查 |
| `character-animation` | 移动、跳跃、切换一／三人称，观察蒙皮和阴影；切输入设备再继续操作 | 项目真实输入配置；`engine.character_controller`、`engine.animation`、`engine.model_animation_script`。交互、视角及脚底接触需实窗检查 |
| `vector-text` | 观察三维文字的字形、颜色、遮挡；返回菜单 | 项目入口契约；`engine.font_system`、`engine.font_ui_render`。字体布局与场景阅读性需实窗检查 |
| `effects-lifecycle` | 分别播放、暂停、继续和停止粒子／模型特效，删除后不再绘制 | 项目入口契约；`engine.effects_runtime`。尚无项目级自动操作序列 |
| `terrain-editing` | 挖洞、填充、保存／加载、Reset，并让角色走过修改区域 | 项目入口契约；`engine.terrain_runtime`、`engine.terrain_destruction_script`。笔刷到角色碰撞的完整交互需实窗检查 |
| `fracture-graph` | 损伤不同结构的连接／支撑，观察碎块脱离，再 Reset | 项目入口契约与原生 destruction 系列。完整 UI 操作序列尚未自动化 |
| `destruction-integration` | 火箭命中触发破坏、土石、音效和导航更新；停火、Reset、返回 | 项目入口契约与对应原生模块测试；跨模块完整交互仍需实窗检查，见[操作说明](../../projects/showcase/docs/destruction-integration.md) |
| `hit-queries` | 射线／球扫命中近远靶；墙后目标不得得分；重复命中、重置和返回 | 项目真实命中处理、计分去重、遮挡反馈与清理；`engine.physics_interaction`。实际物理查询与鼠标操作需原生／实窗验证 |
| `navigation-terrain` | 交叉与窄门、反转、停止、挖穿、恢复和失败重试；保留代理目标和身份 | 项目导航编排与失败恢复；`engine.navigation`、`engine.navigation_terrain`。真实避让与拥堵需实窗检查，见[导航说明](../../projects/showcase/docs/navigation.md) |
| `physics-interactions` | 射线／球扫角色、切球／盒范围、观察 Enter／Stay／Exit，暂停／恢复路线 | 项目查询参数、反馈、订阅及失败回滚；`engine.physics_interaction`。原生接触与可见高亮需实窗检查，见[物理交互说明](../../projects/showcase/docs/physics-interactions.md) |
| `persistent-ui` | 输入文字、加载 A/B、加载中编辑、取消、失败、卸载、销毁 UI | 项目实际 input/click 回调、原控件身份、文本与订阅保留；`engine.scene`、`engine.ui_script`。真实输入焦点、键盘和文档布局需实窗验证 |
| `ecs-many-objects` | 默认 10,000，可选 1,000／50,000；逐实体检查组件表迁移后的身份、父级隐藏／激活、换父级、删除重建及整组回收 | 项目入口契约和 TS 检查；[真实 MCP 功能脚本](../../projects/showcase/tests/ecs_many_objects_test.py) 通过 UI 操作并读取原生场景计数，[macOS Release 实测记录](../testing.md#ecs-大量对象) |
| `render-features` | 对比组件上的视锥剔除、静态实例化、LOD，切换镜头距离、大气天空和材质 Hex；诊断与逐视图提交数通过 MCP 查询 | 项目入口契约、`engine.render_submission` 和 [真实 MCP 功能脚本](../../projects/showcase/tests/render_features_test.py)；[实际结果与限制](../testing.md#上游渲染能力与统一-mcp-功能验收2026-11-04) |

以上原生测试名表示相关验证入口，不表示每一行都已在本轮重新执行。菜单中的数字及角色数量档位用于选择功能，不是性能结论。

## 项目宿主替身回归

项目测试位于 [projects/showcase/tests/](../../projects/showcase/tests/)。下表中的 `.mjs` 测试编译真实项目 TS，再仅替代 `engine` 宿主边界；临时编译结果测试后删除。项目专属测试不放在根 `tests/`；核心、编辑器和协议测试按[所属模块](../testing.md#测试目录)归档。

| CTest 名称 | 脚本 | 实际检查 |
| --- | --- | --- |
| `editor.showcase_navigation` | [launcher_test.mjs](../../projects/showcase/tests/launcher_test.mjs) | 同一个常驻菜单多次进出场景；订阅不重复；初始化／UI 刷新／关闭失败清理；角色输入设备映射；射击遮挡、去重、重置和释放 |
| `editor.showcase_navigation_crowd` | [navigation_crowd_test.mjs](../../projects/showcase/tests/navigation_crowd_test.mjs) | 等待地形就绪、局部导航替换、保留目标、部分笔刷失败、重建失败恢复、动画倍率与退出 |
| `editor.showcase_physics_interactions` | [physics_interactions_test.mjs](../../projects/showcase/tests/physics_interactions_test.mjs) | Motor／Crowd 查询参数、球／盒范围、身份校验、触发订阅与部分失败回滚、暂停恢复及动画速度 |
| `editor.showcase_contract` | [case_contract_test.mjs](../../projects/showcase/tests/case_contract_test.mjs) | 用例／按钮／场景唯一；源场景、GUID 和打包根存在；真实 Gameplay 请求的路径符合声明；真实 Controller 的标题、选择和禁用状态 |
| `editor.showcase_persistent_ui` | [persistent_ui_test.mjs](../../projects/showcase/tests/persistent_ui_test.mjs) | 真实文本输入回调；原节点／控件身份及输入值跨场景保留；等待、取消、资源失败、提交失败、卸载后可操作；销毁及部分初始化清理 |

准备项目 TypeScript 依赖后可从仓库根直接运行，例如：

```sh
npm ci --prefix projects/showcase/settings
node projects/showcase/tests/case_contract_test.mjs .
node projects/showcase/tests/persistent_ui_test.mjs .
ctest --preset ninja-engine-release -R '^editor\.showcase_' --output-on-failure
```

测试中的 input 事件会调用真实项目 Controller，验证 Model 计数与 View 刷新；它不经过 SDL、RmlUi 命中测试或键盘焦点。该区别需要在自动化报告中保留。测试也不会在 JS 重新实现 Jolt、Detour 或动画采样。

## 真实编辑器 MCP 交互

[mcp_interaction_test.py](../../projects/showcase/tests/mcp_interaction_test.py) 复用 `EditorMcpSession`，连接调用方已经启动的 showcase 编辑器。它不启动编辑器，也不替换原生宿主。编辑器完成项目导入后，先确认 `get_status` 返回非空 `session` 和 `editorAlive: true`，再运行；导入尚未完成、没有会话身份时不能开始交互验证。

```sh
python3 scripts/mcp_client.py --server build/ninja-engine-release/mcp_app \
  --project projects/showcase --runtime-dir build/showcase-runtime status
python3 projects/showcase/tests/mcp_interaction_test.py \
  --server build/ninja-engine-release/mcp_app --runtime-dir build/showcase-runtime
```

`--runtime-dir` 必须与该编辑器启动参数一致，`--server` 使用同一构建的 `mcp_app`。脚本核对项目、运行目录、编辑器会话和 Run 身份，必要时启动已加载项目，过程中停止并重开项目，成功结束时通过 `close_editor` 正常关闭该编辑器。

| 检查 | 经过的实际链路与判定 |
| --- | --- |
| 临时 TS 访问活动玩法 | `execute_project_script` 编译临时 TS；校验传入对象是既有 Gameplay、Model 和已加载模块实例；一次调用写入计数，另一调用读回并恢复原值 |
| 单请求错误隔离 | 故意抛错的临时脚本失败后，当前 Run 身份不变，后续脚本仍能读取既有 Model |
| Run 生命周期 | 停止后 `runId` 清零；重开分配新 ID；停止后与重开后均拒绝旧 ID |
| PBR 菜单交互 | `click_ui` 经指针命中与 RmlUi 事件打开 PBR 范例；只读脚本确认活动场景和 Model 选择计数，再真实点击返回菜单 |
| 常驻 UI 输入 | 真实滚轮定位菜单项并点击；`ui_input` 经焦点、End 键和 Unicode 文本输入修改控件；只读脚本确认项目 input 回调增加 `Model.events`，`get_ui_element` 确认实际输入值 |
| 跨场景控件保留 | 真实点击加载方块，核对场景提交计数增加、原 UI 身份不变，原控件句柄仍能读取输入文字；随后真实点击返回菜单 |
| 正常关闭 | `close_editor` 先返回已完成回执，再观察编辑器状态为 `closed`、Run 清零 |

临时脚本读写验证与 UI 验证独立：UI 阶段不通过脚本修改 Model、不直接调用 Controller，也不使用 `set_ui_element` 代替输入。当前常驻 UI Model 保存的是事件计数；输入文字由真实控件保留，不能把这项检查描述为 Model 已保存文字。

每次运行使用独立的 `projects/showcase/cache/mcp-interaction/<运行标识>/`，保存 `receipts.jsonl` 和 `result.json`。待处理请求只查询原回执，不重复提交；失败保留记录，不自动重开或强制结束编辑器。临时 TS 不写入 `assets/`，结果目录不进入 Git。该脚本不注册为普通 CTest，避免无窗口环境运行或意外关闭已有编辑器。

这组交互覆盖菜单点击、滚轮、文字输入、焦点及指定生命周期，不代表全部范例已实窗验收，也不证明 PBR 高光、阴影、反射的视觉质量、物理求解、声音或其他平台表现正确。拖动、手柄与触摸仍需独立实测。

## 尚未覆盖的功能

- 前向+、CSM、点／聚光阴影、SSR、平面反射、水折射、Lightmap 与 SH9 探针尚无分别可选的 showcase 交互用例。渲染实现已有原生／GPU 验证入口，不能因此宣称菜单已提供这些用例；相关入口见[独立 GPU 照明验证](../testing.md#独立-gpu-照明验证)。
- 特效、地形、破坏及组合场景还没有完整的项目自动操作序列；入口契约检查不能替代这些交互。
- 真实 MCP 交互脚本仅覆盖上表的输入序列；拖动、手柄、触摸及其他页面焦点流程仍需经过引擎输入路径验证。宿主替身的直接 click 回调不能算作这些输入的证据。
- 技术范例只组织试件、操作反馈和生命周期，不扩展为私有游戏的伤害、武器、波次或结算业务；私有资源保持项目隔离。

## 实测记录

平台、构建类型、实际通过结果、失败与证据位置统一见[测试与验证](../testing.md)。本文的入口、操作和预期行为不代表最新检出已经重新执行全部用例；宿主替身、原生回归、真实 UI 操作和图像验收分别记录。
