# 多角色导航范例

从主菜单第十项 **Crowd and terrain navigation** 进入 `scenes/navigation.scene`。这是独立技术场景，用于观察群体路径跟随、局部避让、拥堵，以及真实地形破坏后的局部导航重建；没有敌人、战斗或射击规则。

- **Crossing groups**：四组不同颜色的角色在左侧广场交换位置。
- **Narrow doorway**：两组角色在右侧对向通过 2.4 米门洞；中间隔墙使它们不能绕入左侧广场。
- **16 / 32 / 64**：重新创建指定数量的真实模型和导航代理。这些是观察档位，不是已验证容量。
- **Reverse goals**：把目标切回初始位置，再次点击恢复对侧目标。
- **Stop all / Resume**：停止全部代理，或重新提交保存的目标；失败请求可用 Resume 重试。
- **Reset group**：保持模式与数量，销毁旧角色并从初始位置重新开始。
- **Dig crossing pit**：在左侧交叉中心挖半径 3 米的坑，周围保留绕行空间。
- **Cut doorway floor**：沿右侧场地的 `z=0` 使用六个重叠球形笔刷，把门洞下方整条地面挖穿；两侧目标保持不变，由原生导航报告不可达。
- **Restore terrain**：通过 `Terrain.Reset` 恢复原始地形，按此前累计修改区域调用 `Navigation.Replace`。保留代理身份、目标和停止状态。
- **Retry navigation**：局部重建失败时，重试保留的脏区。失败会先停住代理并禁用移动、重建角色与进一步挖掘，避免使用旧导航；恢复成功后只重发因失败而取消的原有目标。
- **Whole arena / Close view**：切换固定观察镜头；左上角沿用公共返回菜单按钮。

地面是自制的 `terrain/navigation.terrain`，范围 48×32 米、体素 0.5 米、地表下 4 米。场景地面节点只持有 `TerrainComponent`，其 Mesh 与碰撞由地形插件生成；没有覆盖坑洞的立方体地板或门槛。分区墙和两侧门墙保持原布局，地板色块仅作远离挖掘区域的起点标记，不进入导航几何。

初次加载时先绑定 UI，等待 `terrain.ready` 和生成的 `MeshRenderer`，然后只把地面、分区墙与门洞两侧墙体显式提交给 `Navigation.Build`，最后创建代理。构建参数与代理半径、高度一致：半径 0.3 米、高度 1.8 米。模式、数量和目标变化不会重复烘焙静态网格。

当前 `DigSphere` 和 `Reset` 成功返回前，`TerrainSystem::commit` 已同步提交新 Mesh、碰撞和地形 revision，因此按钮操作同帧以最新 Mesh 调用 `Navigation.Replace`；不以任意等待帧数代替几何提交完成判定。脚本合并球形笔刷的显式脏区，并在半径外增加 4 个体素（2 米）：覆盖 `TerrainVolume::apply_sphere` 的 2 体素采样扩展及相邻网格/梯度影响。垂直范围覆盖整个地形体积 `[-4, 4]`，导航瓦片边界扩展由已有导航实现负责。门洞操作中即使后续笔刷失败，也会提交此前已成功改变的部分，避免新地形对应旧导航。

正常修改与恢复不调用代理 `Stop`、`MoveTo` 或 `SetPosition`；原生导航保留目标并自主重排路径，停止中的代理也不会被恢复操作自动启动。面板显示地形 revision、导航 revision 与本次 rebuiltTiles，供观察局部替换是否发生。初次启动失败不会逐帧重复 Build，退出仍清理 UI 与已创建对象。

每个角色是单位缩放、无父节点的普通 Node，挂 `NavigationAgent`；子模型保留现有 `0.05 m` 放置偏移。本导航场景的代理根只使用 NavigationAgent，不挂 CharacterMotor 或 RigidBody。引擎另支持同时挂 Collider 与线/角速度均为零的 kinematic RigidBody，保持 Crowd 唯一写入 Transform；该组合的查询和 Trigger 用法见[物理交互范例](physics-interactions.md)。原生系统负责路径请求、跟随、邻居避让及朝向；脚本只在按钮操作时创建或提交命令，每 0.25 秒读状态和速度，更新统计及 walk 播放速度。

本范例为群体目标设置 **0.4 米到达区域**，允许角色在邻居分离作用下停在目标附近；不要求每个角色精确重叠目标点。该参数属于范例配置，引擎 API 的默认到达距离仍为 0.15 米。窄门中的双向拥堵可能持续，不能保证所有角色在固定时间内通过；局部避让不等于全局交通调度。

`MoveTo` 成功仅表示请求被接受。面板分别列出 pending、moving、arrived、idle、unreachable、invalid 和提交失败数量；等待路径不会显示为已经到达。停止和返回会清理代理，返回时同时清除本场景导航网格和 UI 订阅。

角色复用 [ozz 示例授权模型](models-source.md)及既有 walk 动作，各实例通过自己的 `root/GENERIC MAN new` 子路径绑定蒙皮与动画，不使用全场同名查找。资源和菜单统一进入 `settings/resource-build.toml`，不涉及本地付费项目。

`locomotions.fbx.meta` 为 walk、jog、run 显式配置 `animation_motion`：提取 `root/Hips` 的前向 Z 位移，保留 X/Y 摆动及全部旋转。当前角色使用默认 InPlace，根节点由导航移动，避免动画自身的前进位移在每次循环时回跳。共享配置 `assets/scripts/character_animations.ts` 记录原始 walk 参考步速：每周期位移 `142.5300068 × 0.01 = 1.425300068 m`，除以 `1.33333337 s`，约为 **1.068975 m/s**。播放倍率使用当前水平速度除以该步速，遵守 Animator 的 16 倍上限；代理以 3 m/s 移动时约为 2.806 倍。停止或等待路径时倍率为零，恢复后只改变倍率，保留原有播放进度。

可见验收应观察交叉和窄门两种模式的角色真实移动、挖坑后绕行、门洞地面挖穿后显示 no route、恢复后原目标继续寻路，以及 Stop 后编辑仍停止、重置后旧实例清理和返回再进入。本场景未启用运动学碰撞形体；直接挖掉脚下地面时的下落物理不属于此范例。

[tests/navigation_crowd_test.mjs](../tests/navigation_crowd_test.mjs) 运行实际 TS 模块与 MVC，只模拟原生宿主边界；检查首次等待地形、同步新 Mesh 后局部 Replace、正常操作不重发目标或搬动角色、部分笔刷失败与重建失败的恢复、退出清理。同时检查参考步速对应 1 倍播放、水平速度换算及倍率上限、停止恢复只调速且不重复设置动作或调用 Play。不在 JS 中模拟 Detour 寻路或原生动画采样；真实路径连通性与动画循环由原生回归和窗口验收确认。实际窗口及平台验证范围由[测试与验证](../../../docs/testing.md)统一记录，本文件不把静态资源登记当作运行验证。
