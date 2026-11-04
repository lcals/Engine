# 场景模块

[场景类图](class-diagrams/scene.md) · [节点类图](class-diagrams/node.md) · [组件类图](class-diagrams/component.md) · [世界系统](class-diagrams/world-system.md) · [ECS 场景](class-diagrams/ecs-scene.md)

## 对象与所有权

场景中的对象统一以 `Node + Component` 表达：可见网格使用 `MeshRenderer`，相机使用 `Camera`，方向光使用 `Light`，变换使用 `Transform`。项目 `Gameplay` 位于场景之外，连接引擎与场景，可以选择场景、动态创建内容并承载项目业务。

`WorldSystem` 独占一个 Flecs world 与场景生命周期；同时最多一个活动 Scene，可另有常驻场景和未发布候选。真实节点属性、层级、具体组件和领域运行数据全部存入 ECS。Scene 只管理加载、发布、卸载、资源与局部 ID 索引，不再拥有节点对象集合。Node 与 Component 是按需创建的可复制值句柄，没有每实例虚函数或 unique_ptr 行为对象。C++ 的 `add_component<T>()` / `get_component<T>()` 返回具名组件值句柄，TS 保留 `AddComponent(Type)` / `GetComponent(Type)` 外观；每类型在一个实体至多一份。

新节点自动附加 `Transform`。Flecs ChildOf 是唯一父子关系，`parent()` 返回 Node 值句柄，空 Node{} 表示无父级；跨世界、跨场景、循环与失效非空句柄均拒绝。删除节点会删除其后代并清空相应活动相机选择。SceneInfoT 中活动相机以局部 ID 保存，按需返回 Camera 值句柄，添加相机不隐式选中它。

Node 保存世界借用、世界代次与完整实体 ID，Component 再保存类型与附加实例身份；Material 再校验网格组件和材质槽身份。实体或组件删除后重建不能复活旧句柄，ECS 移表不影响有效身份。WorldSystem 对象须活过 C++ 句柄使用；shutdown/reinitialize 使旧代次失效。脚本每次经当前 Run、场景、实体和组件身份重新解析，不存原生数据地址。持久局部 ID 与运行实体 ID 分开。

## 数据与变换

场景、节点和组件的持久数据由 `include/schemas/runtime/scene.fbs` 定义。具体生成 Data 直接存于 ComponentStorage<Data> 的 ECS 列，ComponentFlagsT 与 NodePropertiesT 同样来自 schema；动态 ComponentDataT union 与完整 NodeInfoT 只在保存、恢复和撤销边界组装，运行时不维护第二份对象图。`scene.fbs` 只保存节点、组件及资源 GUID 引用，网格、材质、Shader 和资源目录各用独立 schema。

`Transform` 的位置、欧拉角和缩放使用 double。欧拉角单位为度，局部矩阵为 `T * Rz * Ry * Rx * S`；Y 轴向上，相机和方向光的局部前向为 -Z。WorldSystem 在结构稳定安全点以 double 精度沿 ChildOf 累乘，缓存派生的列主序 float 世界矩阵；计算溢出或不能表示时返回失败，输出保持单位矩阵。分量须有限，缩放各分量非零，可以为负。此数据精度不表示已实现浮动原点或大世界分区。

组件的可选效果开关默认关闭，由项目显式开启；这与节点/组件本身的激活及可见标记不同。组件设置通过返回值报告失败，非法范围不会覆盖原值。禁用组件不会销毁组件，渲染器跳过禁用对象。GPU 资源由 `RenderSystem` 持有，场景停止、替换及设备关闭时按生命周期释放，见[渲染模块](rendering.md)。

节点和组件都有独立的激活、可见标记，默认开启并随场景保存。`set_enabled(false)` 暂停对应对象参与各系统；`set_visible(false)` 只隐藏画面，物理、导航和动画继续。节点标记影响整棵子树，组件标记只作用于自身；父级恢复时保留子级各自的局部设置，Transform 仍可用于坐标计算。有效状态通过 `active()` 和 `visible_in_hierarchy()` 查询，绘制要求两者都为真。状态与变换修改标记派生缓存失效，消费前由 `refresh_scene_state()` 刷新，不能跨帧使用陈旧父链结果。

TS 对应 `SetEnabled()`、`SetVisible()`、`enabled`、`visible`、`active` 和 `visibleInHierarchy`。UI 隐藏或停用保留文档、控件句柄和 Controller，期间不接收交互；移除组件才释放这些对象。所有组件共用 `Component::instance_id()`，同类型删除再添加也会获得新身份，旧脚本句柄不会指向新组件。

## 保存与加载

源场景使用 `.scene` 后缀和 TOML 内容。`SceneImporter::read` / `write` 负责编辑器端读取与保存；资源管线将其转换为 `cache/gen/<完整GUID>` 中的 FlatBuffers 二进制。运行时通过活动资源目录解析 GUID，`Scene.Load("scenes/main.scene")` 加载场景。节点 ID、父子关系及活动相机经过保存与加载保持一致。

`projects/showcase` 按菜单模块加载各自的磁盘场景，网格、材质及 Shader 通过独立资源组装；庭院角色再由脚本实例化。节点数量不是写死的小规模对象数组，`Scene.Create` 同样支持动态场景；实际资源/内存预算与数据校验仍然适用。停止或热重载销毁旧场景，再按新资源版本创建，业务脚本状态不会自动迁移。运行时修改的材质实例参数不会自动回写源材质。

分帧加载由 `Scene.BeginLoad/StepLoad/CommitLoad/CancelLoad` 组织，当前场景与常驻 UI 保留到候选成功提交；资源加载、节点恢复与 GPU 就绪分别判断，见[资源加载流程](resource-loading.md#后台加载与分帧提交)。`Scene.SetPaused` 控制自动世界模拟，UI 与 Gameplay.Update 继续处理；暂停不等于跳过脚本更新，详见[模拟暂停](world-pause.md)。

`Scene::instantiate_model` 将模型资源（Model）展开为 Node + Component 层级，失败回滚候选对象；模型只组织节点层级及其 Mesh/Material 引用；蒙皮 Mesh 自己引用骨架，Animator 另行选择独立动画，贴图由材质引用。同一美术源文件的其他子资源不因实例化而全部加载。FBX/USD 由编辑器插件按各自已支持的范围转成统一资源，运行时不读源格式。预制体来源、实例修改与覆盖传播通过现有[预制体链路](class-diagrams/prefab.md)维护；完整 USD 场景语义及大世界分区不在本次 ECS 迁移范围。资源身份及导入规则见[资源管线](../editor/asset-pipeline.md)。

## 系统查询与安全点

渲染、物理、音频、动画、UI、字体以及角色、导航群体、地形、破坏和特效插件直接查询自身需要的具体 ECS 列，并要求 Published 标记。未发布候选即使与当前场景共用一个 world，也不会被运行系统消费。需要稳定选择/绘制顺序时使用明确的节点创建顺序，不依赖 archetype 内部行位置。

C++ 句柄每次解析，`settings()` 返回独立值，`get_settings(output)` 提供显式失败；数据借用、Runtime 指针与 span 不允许跨结构修改持有。需要增删实体/组件的批次先收集身份句柄，再退出查询执行提交。oneTBB 继续负责工作调度，worker 只处理允许共享的只读资源或独占快照，完成后在原生命周期安全点校验身份并提交；不增加第二套线程池。

`collect_nodes()` / `collect_components()` 为编辑、持久化与诊断提供独立身份快照；原生游戏系统不依赖全场景节点数组。`instantiate_model/prefab` 可输出精确创建集合，编辑 Undo 用专属身份映射重绑，不能令旧 TS 句柄复活。

公开范例 `ecs-many-objects` 的分档、操作与验证记录见 [showcase ECS 范例](../../projects/showcase/docs/ecs-many-objects.md)。数量档位是功能场景规模，不构成帧率或平台性能承诺。
