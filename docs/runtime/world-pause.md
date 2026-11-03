# 世界暂停

[世界类图](class-diagrams/world-system.md) · [运行宿主](class-diagrams/runtime-host.md) · [脚本运行契约](class-diagrams/script-runtime.md)

世界暂停冻结自动模拟，保留画面和 UI 交互。调用方负责决定暂停与恢复时机；当前不提供时间缩放。

## 公开接口

| 所属 | 接口 | 结果与边界 |
| --- | --- | --- |
| `WorldSystem` | `set_paused(bool paused) -> bool` | 世界已初始化且有活动场景时成功；幂等，失败不改状态。 |
| `WorldSystem` | `paused() const -> bool` | 查询临时暂停值；未初始化或无场景时为 `false`。 |
| 脚本 `Scene` 实例 | `SetPaused(paused: boolean): boolean` | 仅活动回调且 Scene 属于当前 Run/活动世界时可修改；其他情况为 `false`。 |
| 脚本 `Scene` 实例 | `GetPaused(): boolean \| null` | 相同身份与活动回调校验；失败为 `null`。 |

暂停值由 `WorldSystem` 持有，不写入场景资源。`SceneBinding` 校验场景身份后转发请求；原生接口见[世界类图](class-diagrams/world-system.md)，脚本声明见 [`include/engine.d.ts`](../../include/engine.d.ts)。

项目打开菜单时调用 `scene.SetPaused(true)`，离开时显式恢复。`Gameplay.Update()` 和 UI 回调继续执行，`Time.deltaTime` 保持正常；项目须跳过自己的世界业务，并管理输入通道、清空输入和恢复前的等待回中。

## 调度与职责

`Engine::tick()` 与 `System::tick_tree()` 保持原有调度。各模拟系统在自己的更新入口读取世界暂停值，复用既有库调用和状态。

| 链路 | 暂停时行为 |
| --- | --- |
| `WorldSystem::on_tick()` | 不调用 `ecs_progress`；仍返回成功，让原有子系统链继续执行。 |
| 窗口、输入、UI、字体、脚本、渲染 | 继续事件、回调、布局和绘制。 |
| `PhysicsSystem` | 不推进 Jolt 固定步、不写回位姿、不产生新接触事件；保留模拟状态，丢弃角色一次性输入。 |
| `CharacterControllerSystem` | 丢弃输入，不提交移动、转向、跳跃或视角切换。 |
| `NavigationCrowdSystem` | 不调用 `step_crowd()`，保留 DetourCrowd 代理、路径与请求，不写回移动。 |
| `AnimationSystem` | 保留最后采样姿态，不推进播放时间、CrossFade 或采样。 |
| `EffectSystem` | 合并组件暂停与世界暂停到 Effekseer `SetPaused()`，不调用 `Manager::Update()`；已有粒子继续绘制，新发射器等待恢复。绘制回退同步同样遵守暂停。 |
| `CharacterCameraSystem` / `TerrainSystem` / `DestructionSystem` | 略过自动相机跟随及地形/破坏同步；显式关闭和场景释放仍走原有清理链。 |
| `AudioSystem` / `RuntimeHost` | 用 SDL 音频设备暂停合并 `!window.can_render() || world.paused()`；两种原因都解除后才恢复输出。 |

各组件自身的播放/暂停意图保持不变。暂停期间不累计模拟时间，恢复只推进新帧；物理允许保留暂停前不足一个固定步的余量。

## 生命周期与失败

- 初始化、成功创建或替换场景、销毁场景和关闭后，暂停值为 `false`；场景加载失败保留旧场景及其暂停值。
- 候选 Run 预检不能修改世界；预检失败保留旧运行。活动 Run 停止或初始化失败时，绑定只清理自己拥有的场景，场景真正释放后才复位暂停。
- 热重载进入激活流程并停止旧运行后，若激活失败，按既有停止/场景释放语义结束，不恢复已销毁的旧世界。
- 显式加载、查询、修改与销毁仍可调用，继续遵守各自身份及失败契约。

## 限制

- 暂停只影响后续自动更新。同帧已经完成的工作不回滚，ECS 可能已在脚本请求暂停前推进。
- `NavigationSystem` 的同步查询和显式步进没有世界依赖；项目须避免在暂停分支自行步进导航。
- 音频统一暂停输出，尚无世界音效与菜单音乐分组。自定义宿主若拆开窗口事件和引擎更新，须像 `RuntimeHost` 一样在窗口处理及玩法更新后同步合并暂停条件。

## 验证方式

通过[构建指南](../building.md)的 Presets 运行受影响的 Debug / Release 定向测试。`physics_test`、`animation_test`、`character_controller_test`、`navigation_crowd_test`、`effects_runtime_test` 与 `script_runtime_test` 覆盖冻结与恢复、组件独立暂停、一次性输入、特效绘制回退和场景生命周期。

实窗还需检查：菜单交互持续可用；等待后恢复无追帧、输入重放或重复接触回调；世界暂停与窗口不可绘制交错时音频只在两者都解除后恢复。插件开关按实际装配验证，关闭脚本后原生调用仍应可用。实际执行结果与平台限制维护在[测试与范例](../testing.md)，不在此累积测试流水。
