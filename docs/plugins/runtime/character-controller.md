# 角色控制与镜头适配

`CharacterMotor` 是核心物理组件，封装 Jolt 胶囊角色运动。可选 `character_controller` 插件通过核心的 `CharacterMotorDriver` 接口逐固定步提交移动、朝向、跳跃和姿态请求；物理系统是角色根 Transform 的唯一写入方。主动速度、外部击退与平台携带速度分开保存，清除输入保留外力。核心契约见[角色物理驱动](../../runtime/class-diagrams/character-motor-control.md)。

每个 `CharacterController` 独立接收 `SetInput({ move, lookDelta, lookRate, toggleView, actions })`。玩法明确持有目标组件；多个角色可以同时接受各自输入，AI 也可以提交同一契约。基础移动支持加减速、空中控制和移动／观察方向朝向；Sprint、Jump、Crouch 通过显式动作与能力配置启用。新组件不隐式开启能力，完整字段和动作消费规则见[角色控制](class-diagrams/character-control-design.md)与[能力组合](class-diagrams/character-abilities.md)。

`CharacterCamera` 挂在相机节点，只将观察输入和角色状态映射到同节点 `CameraController` 的参数。固定、跟随、环绕、瞄准、投影、晃动、碰撞及边界求解属于独立 [camera 插件](class-diagrams/camera-controller.md)。物理和动画完成后更新角色状态参数，再求解镜头；蹲姿眼高来自实际胶囊尺寸，受阻站起不会提前抬高视角。

`CharacterVisual` 标记身体、头部和第一人称部件；角色视图系统在镜头求解后更新该相机的瞬态隐藏列表，不修改 MeshRenderer 全局可见性。第一人称可隐藏自身身体并保留投影，专用部件只向对应镜头显示。组件字段与所有权见[镜头和角色表现](class-diagrams/character-integration-design.md)。

TOML、MCP 与 TS 使用同一 Schema 生成的数据；编辑器完整设置替换参与撤销，输入、参数和状态查询保持瞬态。持久引用使用场景 local ID；运行时目标覆盖使用有效 Node 句柄。接口只保留当前版本，未知字段、无效类型与未登记能力明确失败；源场景按当前结构导入为版本 3 缓存。操作入口见[编辑器接入](../editor/class-diagrams/character-control-design.md)。

动作播放、InPlace 与蒙皮继续使用[动画模块](../../runtime/animation.md)。项目接入和键位见[公开范例](../../../projects/showcase/docs/character-control.md)，已执行的平台与测试范围见[测试文档](../../testing.md#镜头与角色模块验证)。
