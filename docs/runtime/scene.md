# 场景模块

[场景类图](class-diagrams/scene.md) · [节点类图](class-diagrams/node.md) · [组件类图](class-diagrams/component.md) · [世界系统](class-diagrams/world-system.md)

## 当前动态场景

场景中的所有对象统一以 `Node + Component` 表达，方块为 `Node + MeshRenderer`，相机为 `Node + Camera`，方向光为 `Node + Light`。不增加 GameObject、Behaviour 或 MonoBehaviour。Gameplay 是场景外的项目入口，连接引擎与场景，可动态创建场景、选择场景，也可承载项目业务。

`WorldSystem` 独占最多一个活动 Scene；Scene 独占节点集合，Node 独占组件。节点父指针只表达可空借用，层级所有权仍集中在 Scene。删除一个节点会一并删除其后代；若活动相机在该子树中，先清空相机借用。父子关系不能跨场景或形成循环，每种内置组件每节点至多一个。

每个节点建立一个 [flecs](https://www.flecs.dev/flecs/) 实体，使用包含代次的完整 64 位实体 ID；节点删除后旧 ID 不再解析到重用槽位的新节点。节点变换和组件参数只有一份 C++ 状态，本轮不另存一份 ECS 变换副本。Scene 使用世界内递增的生命周期 ID，关闭世界再初始化也不重用。脚本句柄保存场景 ID、节点 ID，每次访问重新查询，销毁后安全失效。

Transform 是节点固有的普通数据，位置、欧拉角和缩放使用 double。欧拉角为度，局部矩阵顺序是 `T * Rz * Ry * Rx * S`；Y 向上，Camera 与 Light 的局部前向为 -Z。父链在 double 精度下累乘，最终输出列主序 float GPU 矩阵；无法表示或计算溢出返回失败，输出保持单位矩阵。所有变换分量须有限，缩放各分量非零，允许负缩放。高精度数据基础不等于已经实现大世界浮动原点或分区。

组件设置使用返回值表达失败，拒绝非法范围并保留旧值。禁用组件不销毁组件；渲染时跳过禁用对象。首个 Camera 组件自动成为活动相机，可以显式切换或清空。Light 首版为方向光，MeshRenderer 首版只提供内置 cube 和普通材质数据。GPU 资源归 RenderSystem，本轮停止或替换场景时释放，详见[渲染模块](rendering.md)。

当前场景和 Mesh 数据在代码中生成，不保存场景文件；测试项目通过 TypeScript Gameplay 创建方块、相机、方向光三个节点。场景退出与热重载清理旧节点和组件，再创建新的场景，不承诺保留任意业务状态。

## 后续落盘与加载

下一阶段将增加场景与预制体的保存、读取、实例化和卸载。一个 USD 资源中的多个节点转换为引擎 Node，保留嵌套层级，功能由组件表达；编辑器操作场景数据，运行时据此构建节点和组件。

场景和预制体将通过稳定资源 ID 引用资源，规则见[资源管线](../editor/asset-pipeline.md)。稳定资源 ID 与本轮节点/场景的运行生命周期 ID 含义不同，不能把运行 ID 直接当作已实现的持久化格式。
