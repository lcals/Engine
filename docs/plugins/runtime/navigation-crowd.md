# 群体导航插件

核心直接复用 Recast Navigation 1.6.0 的 DetourCrowd 路径队列、近邻格、分离避让和位置积分；可选 `runtime/navigation_crowd` 插件将 `NavigationAgent` 绑定到普通 Node。编辑器 `editor/navigation_crowd` 只负责持久参数编解码。默认完整预设启用两者；已有显式插件缓存需将 `navigation_crowd` 加到对应清单。插件不依赖角色控制器，核心不依赖该插件。

```ts
const agent = node.AddComponent(NavigationAgent);
if (!agent || !agent.Configure({ radius: 0.3, height: 1.8, maxSpeed: 3, arrivalDistance: 0.4 }))
    return false;
if (!agent.MoveTo({ x: 12, y: 0, z: 8 })) return false;
// 此后原生系统推进；仅在需要时读 agent.state / agent.velocity，或调用 agent.Stop()。
```

代理根节点保持单位缩放、无父节点，不同时启用 CharacterMotor。需要物理查询或接触身份时，可同时启用 `Collider + RigidBody`，其中 RigidBody 必须为 `kinematic`，线速度和角速度均为零；Crowd 唯一写入 Transform，Physics 同步该碰撞形体。仅启用 Collider、仅启用 RigidBody、动态或静态 RigidBody，以及非零刚体速度的组合均被拒绝。当前沿导航面移动，未与 Jolt 的动态位移、重力或击退合并；需要这些运动能力的角色继续使用 Motor。代理半径与高度不得超过烘焙净空。`MoveTo` 返回成功仅表示请求接受；`pending`、`moving`、`arrived`、`unreachable`、`invalid` 由原生反馈，精确到达不会用低速度代替。密集目标可设置适当到达范围；局部避让不保证双向窄门限时无拥堵。

脚本只做创建、提交目标及低频观察，不逐帧搬位置。完整网格重建使核心句柄失效，插件重新登记并提交组件保留的目标；局部瓦片更新保留身份、丢弃旧走廊后重新排队。禁用、删除和切场景移除代理，场景文件只保存配置。公开契约与容量/路径预算见[导航模块](../../runtime/navigation.md)及[代理类图](class-diagrams/navigation-crowd.md)。

地形修改成功后，调用方合并变化范围并以最新静态几何同步调用 `Navigation.Replace`。断路返回不可达，恢复连通后原目标重新排队；正常更新无需脚本逐个重发目标。局部生成失败会使影响区域失效，范例停住代理并保留脏区供重试。局部重建的各 tile 可在任务域并行准备，但 Replace 仍在统一发布后返回；不包含跨帧导航构建、自动追踪动态残骸或脚下地面消失后的物理下落。

公开交叉移动、窄门与地形变化范例见[测试与范例](../../testing.md)。
