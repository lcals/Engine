# 世界暂停（待评审）

[世界类图](class-diagrams/world-system.md) · [运行宿主](class-diagrams/runtime-host.md) · [脚本运行契约](class-diagrams/script-runtime.md)

本方案为新增公开契约评审稿，尚未实现。目标是打开菜单、设置、图鉴或等待手柄重连时冻结世界，继续显示当前画面并接收 UI 事件；恢复后从冻结状态继续。首版仅暂停/恢复，不增加时间缩放、异步加载或新的组件脚本框架。

## 最小接口

| 所属 | 接口 | 结果与边界 |
| --- | --- | --- |
| `WorldSystem` | `set_paused(bool paused) -> bool` | 世界已初始化且有活动场景时成功；幂等，失败不改状态。 |
| `WorldSystem` | `paused() const -> bool` | 查询临时暂停值；未初始化或无场景时为 `false`。 |
| 现有 `SceneBinding` 的脚本实例 | `Scene.SetPaused(paused: boolean): boolean` | 仅活动回调且 Scene 属于当前 Run/活动世界时可修改；其他情况为 `false`。 |
| 现有 `SceneBinding` 的脚本实例 | `Scene.GetPaused(): boolean \| null` | 相同身份与活动回调校验；失败为 `null`。 |

WorldSystem 的两个方法为 `noexcept`、`[[nodiscard]]`，查询另为 `const`。不改变原生 `Scene` 的公开方法或场景 schema。Scene 的脚本外观只是已有绑定到 WorldSystem 的一次身份受检转发，不创建包装类。

世界不判断调用方为何暂停。项目显式调用 `scene.SetPaused(true)` 后显示菜单；Gameplay 继续处理菜单输入，只有 `scene.GetPaused() === false` 时才调用自己的世界业务更新。菜单离开时显式恢复。现有 `CharacterControls.SetPaused()` 可继续处理输入清空和等待回中；暂停世界不代替设备上下文切换。

## 调度与职责

保留 `Engine::tick()` 与 `System::tick_tree()` 原样，不增加通用暂停字段、子系统枚举、类型判断或阶段调度器。WorldSystem 保存一个 `paused_`，实际模拟系统在各自既有更新入口读取。所有权和装配顺序保持现有契约。

优先复用本仓库已经选用的成熟库状态和现有引擎调用链，不另写暂停模拟器或调度框架：

| 已选库 | 复用方式 |
| --- | --- |
| Flecs | 暂停时不调用已有 `ecs_progress()`，保留 ECS 对象。 |
| Jolt | 暂停时不进入已有物理固定步和 `PhysicsSystem::Update()` 调用，保留原生刚体/角色状态；不用销毁重建或逐刚体伪造静止。 |
| Effekseer | 复用实例 `Manager::SetPaused()` 并与组件自身暂停状态合并；世界暂停时不调用 `Manager::Update()`，继续由现有 renderer 绘制实例。恢复只解除世界原因，保留组件自身原因。 |
| DetourCrowd | 暂停时不调用现有 `dtCrowd::update()`，保留代理、路径与请求；不另写路径推进。 |
| SDL3 Audio | 复用现有 `AudioSystem::set_paused()` 内的 `SDL_PauseAudioDevice()` / `SDL_ResumeAudioDevice()`。 |
| RmlUi | 继续现有事件处理与 Context 更新/渲染，使菜单交互持续运行。 |

Animator 保留引擎现有采样姿态与播放状态，略过时间推进和采样；不为暂停新增动画播放器。上述改动只在库调用边界执行显式暂停条件，不改变库内部实现，也不引入新的依赖。

| 链路 | 暂停时行为 |
| --- | --- |
| `WorldSystem::on_tick()` | 不调用 `ecs_progress`；仍返回成功，让原有子系统链继续执行。 |
| `WindowSystem` / `InputSystem` / `UiSystem` / `FontSystem` | 继续事件、输入发布、布局、焦点与字体工作。 |
| `ScriptSystem` / `ScriptRuntime` | 继续绑定事件分发和 `Gameplay.Update()`；不整体停 VM，不改 Time.deltaTime 为零。项目显式跳过自己的世界业务。 |
| `PhysicsSystem` | 维护必要的场景身份/资源释放后，不推进固定步、不写回模拟位姿、不产生新碰撞/触发事件，不把本帧时间放入累计器。已有速度与合法模拟状态保留。 |
| `CharacterControllerSystem` | 不提交移动、转向、跳跃或相机切换；处理好待消费的一次性输入，避免恢复后重放暂停期间的输入。项目仍负责进入菜单前清输入、禁用相应通道。 |
| `NavigationCrowdSystem` | 不自动调用 `step_crowd()` 或写回代理移动；保留路径/代理状态，不累积暂停时间。 |
| `NavigationSystem` | 保持现有同步查询/显式步进底层；它没有自动 `on_tick`，也不借用 WorldSystem，不为暂停新增世界依赖。项目不得在暂停分支显式步进世界导航。 |
| `AnimationSystem` | 保留最后采样姿态，不推进播放时间、CrossFade、根运动或模拟驱动变形；不改每个 Animator 自己的播放/暂停状态。 |
| `EffectSystem` | 保留已存在粒子并继续绘制，不推进粒子/发射器/特效时间，不积累 `pending_seconds_`；检查 `draw()` 的回退同步路径，不能仅在 `on_tick()` 早退后由绘制恢复推进。 |
| 其他自动改变世界的已装配插件 | 宿主/插件在实际模拟入口遵守同一核心查询，特别是相机跟随及地形/破坏的自动连续处理；核心不枚举插件类型。生命周期清理与静态绘制准备不因暂停跳过。 |
| `RenderSystem` / 插件绘制 | 继续显示冻结世界和变化中的 UI；只准备绘制，不借机推进模拟。 |
| `AudioSystem` / `RuntimeHost` | 复用已有设备暂停，合并 `!window.can_render() || world.paused()`；菜单恢复不能覆盖窗口仍不可绘制这一原因，窗口恢复也不能覆盖世界仍暂停这一原因。 |

世界暂停可在 Gameplay 更新中设置。现有顺序为 UI/脚本在角色意图、物理、动画和大多数模拟插件之前，因此菜单请求可阻止本帧后续模拟。已经完成的工作不回滚，尤其 ECS 当前帧可能已经推进；不为追求回滚额外拆分 System 调度。

音频是输出设备独立推进，给模拟系统传 `0` 秒无法暂停音频。RuntimeHost 在窗口状态变化及引擎更新完成后同步上述合并结果，使本帧 Gameplay 改变的世界暂停及时反映到设备；保留 AudioSource 自己的 paused/stopped 状态，不把所有源改成 Play。首版使用现有统一音频输出，若将来要求菜单音乐继续而世界音效冻结，再独立评审音频分组。

## 生命周期与失败

- 世界初始化、成功创建空场景、成功替换活动场景、销毁活动场景和关闭后，暂停值为 `false`。新场景需要暂停，由调用方在加载成功后明确设置。
- 场景加载失败不替换活动 Scene，也不改变旧场景暂停值。
- 候选 Run 的目录/模块预检不得改变活动世界。未激活的 BindingContext 不能设置暂停，候选销毁不得执行全局暂停复位。
- 活动 Run 停止、初始化失败及场景释放依照现有场景身份清理；只有拥有当前场景的绑定可以销毁它，WorldSystem 在该场景真正释放时复位。不能仅因任意 `Run` 析构就解冻另一个 Run/宿主的场景。
- 不改变目前热重载的事务保证：候选预检失败保留旧运行及其暂停值；进入已有激活流程并停止旧运行后的失败，按既有停止/场景释放语义结束，不承诺恢复已销毁的旧世界。
- 暂停时不累计 delta_time。保留进入暂停前不足一个固定步的合法余量可以，但恢复后不得加上暂停期间的墙钟时间或补发过期的一次性输入。
- 暂停不禁止调用方明确执行加载、查询、修改或销毁；这些接口仍按各自身份与失败契约工作。自动世界推进暂停，菜单业务的显式资源操作仍由调用方决定。

## 需要同步的其他评审入口

本次只修改世界类图与本说明。实施前由对应负责人同步：

- 脚本绑定类图：SceneBinding 的 `SetPaused` / `GetPaused` 外观和活动场景身份约束，`engine.d.ts` 同步；ScriptRuntime 无新增方法。
- 运行宿主类图：记录音频暂停原因的合并和同帧刷新；不用新增公开 RuntimeHost 方法。
- 各实际模拟系统类图/说明：记录读取 WorldSystem 暂停值的行为与借用关系；本来已借用 WorldSystem 的系统无需新增包装或暂停公共接口。
- Engine/System、原生 Scene 和持久 schema：不新增契约。

## 实施后验收

1. 运行中打开菜单，UI 导航/设置/返回仍可用；同一物体的位姿、速度、动画时间、特效时间和导航代理位置冻结。
2. 暂停等待后恢复，只推进新帧；无物理/导航追赶、粒子喷发、跳跃/视角切换重放或重复碰撞回调。
3. 单独暂停的 Animator/Effect/AudioSource 在世界恢复后仍保持自己的暂停状态。
4. 世界暂停与窗口不可绘制交错进入/退出，音频只有两种原因都解除时恢复。
5. 暂停中成功切场景/停止后恢复默认；失败加载、候选预检失败和旧句柄不能解冻或暂停他人的活动场景。
6. 关闭脚本插件仍可由原生调用方设置世界暂停；关闭某个可选模拟插件不影响其他系统与 UI。
