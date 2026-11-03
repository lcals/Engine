# 物理交互技术范例

主菜单第十一项 **Physics queries and contact events** 打开独立场景，不修改既有射击或导航场景。资源沿用公开 ozz 角色与 walk 动作，只有接口验证与可见反馈，不包含伤害、敌我或战斗业务。

- 外侧两条路线使用真正的 `CharacterMotor` 胶囊、原生重力与贴地。脚本只提交水平速度意图，达到端点后反向。
- 内侧三条路线使用 `NavigationAgent + Collider + kinematic RigidBody`。线/角速度为零，Crowd 写入位置，Physics 镜像查询形体；Collider 设为 sensor，不添加另一套碰撞位移求解。胶囊中心在角色脚点上方 0.9 米。
- 所有角色使用各自模型实例的子路径绑定 Animator；脚本不改写角色位置。每 0.25 秒读取五个已缓存角色的状态、速度与区域结果，更新动作速度和材质颜色。

walk、jog、run 在 `locomotions.fbx.meta` 中显式提取 `root/Hips` 的前向 Z 位移，保留其他平移轴及旋转。当前使用默认 InPlace 的 walk，由 NavigationAgent 或 CharacterMotor 移动根节点，避免动画前进位移在循环边界回跳。两类角色共用 `assets/scripts/character_animations.ts` 的参考步速 **1.068975 m/s**：原动作每周期位移 `142.5300068 × 0.01 m` 除以 `1.33333337 s`。播放倍率为实际水平速度除以参考步速，上限为 Animator 支持的 16 倍；速度变化只调用 SetSpeed，初始化后不重复设置动作或调用 Play。

**Probe the selected lane** 沿蓝线执行原生 `Raycast` 或半径 0.12 米的 `SphereCast`。Target 按钮轮换五条角色路线；查询明确包含 CharacterMotor、kinematic 与 sensors，排除场景静态标记。命中后显示 `kind`、`instanceId` 和距离，角色短暂变金色，方块标记实际命中点。

**Overlap** 在世界轴对齐盒（中心 `[0,1,0]`、半尺寸 `[3,1,7]`）和球（同中心、半径 6.8 米）间切换。绿色地板或圆环标识其水平范围；命中角色变绿。只有原生返回且 `IsObjectValid` 成功的参与者计数，脚本不以节点距离替代物理重叠判定。

橙色边界是独立静态 sensor 区域。范例使用 Unity 风格的 `Physics.OnTriggerEnter(node, callback)`、`OnTriggerStay`、`OnTriggerExit` 订阅，只统计该区域与五个角色之间的接触；回调中的 `self` 始终为区域，`other` 为相接对象。快照保留 frame、step、接触点与朝向 `self → other` 的法线，界面显示 enter / stay / exit。脚本不轮询事件数组，也不做逐帧去重；清空计数后等待下一次原生回调。历史 Exit 不强求对方对象仍存活，场景身份仍必须匹配。

订阅捕获当前物理组件实例，在首次物理同步前即可登记。绑定层在观察到组件禁用、销毁或替换时自动解除；模块退出还会对保存的 ID 显式调用 `Physics.Off`。中途订阅失败会立即释放已经成功的订阅，避免部分初始化留下回调。

**Pause routes / Resume routes** 只改变原生移动意图或目标，重力与事件观察继续；实际水平速度降至零后动画倍率归零，移动恢复后继续原有播放进度。**Reverse routes** 保留当前位置，反转各自目标。退出会取消意图、清除导航、释放模型、物理与 UI 订阅。

核心 C++ 验证覆盖真实命中、重叠与接触，绑定回归验证原生订阅的生命周期；[tests/physics_interactions_test.mjs](tests/physics_interactions_test.mjs) 只替代原生宿主边界，检查查询参数、身份校验、颜色反馈、Enter/Stay/Exit 回调登记和解除，以及两类角色的步速换算、倍率上限、暂停恢复不重启动画。不模拟 Jolt、Detour、原生动画采样或事件去重算法。macOS Metal 实窗已确认导航 Collider 射线命中、CharacterMotor 球扫命中、球/盒范围高亮、触发统计、暂停/恢复、反转及菜单往返。停止后节点归零；未进行其他平台或容量验证。回归与操作证据位置统一记录在[测试与验证](../../docs/testing.md)。
