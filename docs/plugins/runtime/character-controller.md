# 角色输入与跟随相机

`CharacterMotor` 是核心物理组件，封装 Jolt 胶囊角色运动。主动水平速度 `set_velocity`／脚本 `SetVelocity` 与世界空间速度增量 `add_velocity`／`AddVelocity` 独立，清输入不会吞掉击退。`external_damping` 控制外部水平速度按秒衰减，竖直增量交给重力和碰撞。请求在下一固定步消费一次，没有物理子步的帧保留请求。

输入、相机和角色表现属于可选 `character_controller` 运行时插件，分为三个场景组件：`CharacterController` 只控制选中的玩家，`CharacterCamera` 挂在 Camera 节点并独立跟随目标，`CharacterVisual` 标记身体／头部／第一人称专用子树。未显式选择玩家时仅接受唯一启用控制器；多个候选返回失败，业务可用 `set_player` 选中，传空解除选择。NPC 直接使用 Motor，无需玩家控制器或相机。控制器移除、禁用、替换或解除选择时撤销其主动请求，保留外部击退。

跟随镜头在物理和动画后读取目标世界位置，目标可以没有 Motor。插件独占场景 Camera 的瞬态网格过滤列表，不更改 MeshRenderer 的全局状态或保存数据。第一人称专用部件应放在目标子树并标记 `first_person`，只向对应第一人称镜头显示。此功能不包含动画状态机、Root Motion、群体避让或空间分桶。

MCP 的 `cameraNodeId`／`targetNodeId` 使用当前运行节点 ID 字符串，`"0"` 解除引用；保存时转换为场景持久节点 ID。

旧场景的 `character_controller` 中 `mode`、`eye_height`、`camera_distance`、`pitch_min/max` 需移至相机节点的 `character_camera`，并设置 `target_node_id` 指向角色的持久节点 ID；控制器保留 `camera_node_id`。旧字段导入会明确报错，不能静默丢弃配置。已迁移内置样例，场景导入器指纹更新后会重新生成缓存。

动作播放、InPlace 与蒙皮见[动画模块](../../runtime/animation.md)，庭院操作与制作记录见[测试与范例](../../testing.md)。
