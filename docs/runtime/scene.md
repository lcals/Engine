# 场景模块

[场景类图](class-diagrams/scene.md) · [节点类图](class-diagrams/node.md) · [组件类图](class-diagrams/component.md) · [世界系统](class-diagrams/world-system.md)

## 对象与所有权

场景中的对象统一以 `Node + Component` 表达：可见网格使用 `MeshRenderer`，相机使用 `Camera`，方向光使用 `Light`，变换使用 `Transform`。项目 `Gameplay` 位于场景之外，连接引擎与场景，可以选择场景、动态创建内容并承载项目业务。

`WorldSystem` 独占最多一个活动 `Scene`，`Scene` 独占节点集合，`Node` 独占无具体类型的组件列表。节点只管理身份、层级和组件，不保存相机、灯光等具体组件指针。C++ 通过 `add_component<T>()` / `get_component<T>()` 操作组件，TS 对应 `AddComponent(Type)` / `GetComponent(Type)`；每种组件在一个节点上至多一个。

新节点自动附加 `Transform`。节点的父指针为可空借用，节点所有权始终属于 `Scene`；父子关系不能跨场景或形成循环。删除节点会删除其后代；若活动相机位于该子树，先清空相机引用。添加相机不会隐式将其设为活动相机，调用方须显式选择。

每个节点关联一个 flecs 实体。运行时实体 ID 包含代次，节点删除后旧 ID 不会解析到重用槽位的新节点。场景生命周期 ID 在世界系统重新初始化后也不会重用。脚本句柄保存场景和节点运行 ID，每次访问重新查找，所属对象销毁后失效。持久化节点 ID 与运行时实体 ID 分开管理。

## 数据与变换

场景、节点和组件的持久数据由 `include/schemas/runtime/scene.fbs` 定义。行为类直接使用生成的数据结构；场景变换不会再复制为另一份 ECS 数据。`scene.fbs` 只保存节点、组件及资源 GUID 引用，网格、材质、Shader 和资源目录各用独立 schema。

`Transform` 的位置、欧拉角和缩放使用 double。欧拉角单位为度，局部矩阵为 `T * Rz * Ry * Rx * S`；Y 轴向上，相机和方向光的局部前向为 -Z。父链以 double 精度累乘，输出列主序 float GPU 矩阵；计算溢出或不能表示时返回失败，输出保持单位矩阵。分量须有限，缩放各分量非零，可以为负。此数据精度不表示已实现浮动原点或大世界分区。

组件设置通过返回值报告失败，非法范围不会覆盖原值。禁用组件不会销毁组件，渲染器跳过禁用对象。GPU 资源由 `RenderSystem` 持有，场景停止、替换及设备关闭时按生命周期释放，见[渲染模块](rendering.md)。

## 保存与加载

源场景使用 `.scene` 后缀和 TOML 内容。`SceneImporter::read` / `write` 负责编辑器端读取与保存；资源管线将其转换为 `cache/gen/<完整GUID>` 中的 FlatBuffers 二进制。运行时通过活动资源目录解析 GUID，`Scene.Load("project/scenes/main.scene")` 加载场景。节点 ID、父子关系及活动相机经过保存与加载保持一致。

示例项目从磁盘场景加载方块、相机和方向光三个节点，网格、材质及 Shader 通过独立资源组装。`Scene.Create` 仍支持动态场景。停止或热重载销毁旧场景，再按新资源版本创建，业务脚本状态不会自动迁移。运行时修改的材质实例参数不会自动回写源材质。

`Scene::instantiate_model` 将统一 Model 资源实例化成普通节点与组件，失败回滚候选对象；FBX/USD 由编辑器插件转成该统一资源，运行时不读源格式。编辑器保存界面、带覆盖传播的预制体系统、完整 USD 场景语义及大世界分区尚未实现。资源身份及导入规则见[资源管线](../editor/asset-pipeline.md)。
