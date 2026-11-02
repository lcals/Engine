# 物理模块

`PhysicsSystem` 使用 [Jolt Physics](https://github.com/jrouwe/JoltPhysics) 建立碰撞世界，由宿主借用 `WorldSystem` 并统一安排生命周期。公开接口和场景组件不包含 Jolt 类型。

宿主可同时注入已初始化的 `TaskSystem`，刚体求解通过 Jolt 官方 `JobSystemWithBarrier` 在共享 oneTBB worker 上执行；任务域控制并发与优先级，不另建物理线程池。域不传时同步执行。域必须活到物理系统关闭之后，关闭或重配不得与物理步进并发。当前帧内完成求解、包装任务引用释放、组件回写及事件发布，保持既有查询和脚本时序；角色循环仍串行。详见[物理任务契约](class-diagrams/physics-system.md)。

场景中的物理对象使用节点和组件表达：`Collider` 定义盒、球、胶囊或静态三角网格，以及中心、摩擦、弹性和触发器属性；`RigidBody` 定义静态、运动学或动态运动、质量、阻尼、重力倍率及速度。持久数据来自 `scene.fbs`。没有刚体组件的碰撞体视为静态物体；禁用碰撞体、刚体或变换会移除对应物理对象。

静态三角网格通过 Collider 的 `mesh_guid` 独立引用 Mesh 资源；可复用渲染 Mesh，也可使用专用碰撞 Mesh，不要求同节点有 MeshRenderer。场景导入将引用加入构建和运行依赖，运行时从统一资源加载接口取得顶点与三角形索引，并由 Jolt MeshShape 构建碰撞体。Collider 和物理快照持有共享引用，删除或清理后按现有资源生命周期释放。源网格使用引擎约定的逆时针绕序；Jolt 清理退化三角形，全部退化或非法索引通过返回值失败。该形状只支持静态用途，动态及运动学刚体组合均拒绝；不跟随蒙皮变形。角色胶囊、台阶和相机球扫直接使用同一碰撞世界。

`.scene` 中的最小静态网格碰撞组件为：

```toml
[[nodes.components]]
type = "collider"
shape = "mesh"
mesh = "<独立 Mesh 资源的完整 GUID>"
```

无需添加 RigidBody；如果显式添加，其 motion 必须为 `static_body`。MCP `set_collider` 的 `mesh` 参数与脚本 `Collider.SetMesh(path)` 接受 assets 相对路径或 `builtin://...` 逻辑路径，落盘仍保存 GUID。

瞬态静态几何通过 `set_mesh_data` 或 `set_mesh_parts` 同步提交，先准备候选，失败保留原碰撞。分段输入使用稳定槽位与共享不可变网格，复用未变 Jolt 叶形状和复合树分组；一个 Collider 仍对应一个 Body，空槽、清空、禁用恢复与局部支撑唤醒见[分段碰撞契约](class-diagrams/mesh-collision-data.md)。这些接口不识别地形或项目，也不负责线程调度。既有单线程基线及后续并行采样集中维护在[性能采样](performance.md#最终可信基线)，旧基线不代表并行后的耗时。

每次更新先按场景和节点 ID 同步组件，再以固定 `1/60` 秒模拟，最后将运动物体的位置、旋转和速度写回组件。每帧最多执行 8 个固定步，过量时间丢弃，避免长帧无限追赶；零时间更新只同步配置，不推进模拟。冲量只作用于动态刚体，作为一次性瞬态请求消费，不写入场景数据。

静态和运动学对象支持可分解的父级变换；动态刚体必须位于场景根节点。祖先不能是启用的运动刚体，本阶段不把嵌套碰撞体合并成复合刚体。剪切、负缩放及不符合基本形状约束的缩放会明确失败：球必须等比缩放，胶囊横向缩放必须相同。更新失败清理物理对象并返回失败，不静默使用错误的位置或形状。

`clear()` 释放全部物理对象并清空累积时间，可以重复调用；场景卸载只在下一次同步移除实际消失的对象；常驻节点保持 Jolt 实例、速度及接触状态。系统遍历世界已发布场景集合，不保存节点指针，候选恢复场景不参与。宿主完整关闭物理时主动调用它。Jolt 的工厂与类型注册只管理本模块创建的实例；已有外部工厂时初始化返回失败，不接管或销毁其他使用者的全局状态。

触发器不产生碰撞响应或撞击快照。`impacts()` 返回当前物理帧的非消费 `PhysicsImpact` 列表，包含双方各自的 `PhysicsObject` 身份（原 Scene、Node、组件类型及实例）、接触点、从节点 A 指向 B 的世界法线、碰前法向接近速度和估算法向冲量。多个子步及复合形状的重复接触按完整对象身份对保留估算冲量最大的一条；速度包含角速度贡献。实现复用 Jolt `ContactListener` 和官方 `EstimateCollisionResponse`，后者不是求解器实际冲量，多体碰撞的结果只适合玩法阈值判断。回调仅采集数值，不调用场景或脚本；全部子步完成并写回组件后才发布快照。普通静置接触不直接触发伤害，具体阈值、冷却及破坏策略由项目脚本决定。

快照在每次物理 `on_tick` 开始失效，零子步帧同样清理；`clear` 及查询时发现节点删除、组件替换或组件/祖先 Transform 禁用时清理相应记录。更换活动场景不会清除常驻对象的有效撞击，跨场景碰撞分别保留双方身份。`frame` 在本系统生命周期内递增，`clear` 不复用旧编号。借用的 span 只能在主线程物理步进外使用，不能跨更新或清理长期持有。运行宿主现有顺序是脚本先于物理，项目在下一次脚本 Tick 处理上一物理帧快照，可在安全点再请求地形/建筑变更。此接口当前覆盖普通刚体，CharacterVirtual 的接触生命周期通过独立 contacts() 提供，不混入撞击估算。见[物理系统类图](class-diagrams/physics-system.md)与[物理组件类图](class-diagrams/physics-components.md)。

组件公共校验范围：碰撞尺寸为 `0.0001..100000`，胶囊半高允许为零，中心各轴绝对值不超过 `100000`；摩擦、弹性和阻尼为 `0..1`。质量为 `0.0001..1000000`，重力倍率为 `0..100`；线速度和角速度各轴绝对值不超过 `10000`，单次及累计待消费冲量各轴不超过 `1000000`。世界空间缩放后仍须满足形状尺寸限制。超出范围通过返回值拒绝，不隐式改写输入。

角色运动使用独立的 `CharacterMotor`，公开胶囊设置、世界空间水平速度、地面状态及一次跳跃请求，内部复用 Jolt `CharacterVirtual`。胶囊中心从脚底原点自动上移 `radius + half_height`；角色之间使用 Jolt `CharacterVsCharacterCollisionSimple`。角色必须是单位缩放、仅 Y 轴旋转的根节点，不能同时启用独立 Collider/RigidBody。设置的台阶高度、贴地距离和坡度限制参与实际碰撞求解；具体范围和契约见[角色物理类图](class-diagrams/character-motor.md)。

角色主动输入与外部速度分离：`set_velocity` 设置水平移动意图，`clear_input` 清除它及未消费的跳跃，但保留击退；`add_velocity` 接受世界速度增量（m/s，单位质量冲量），各轴单次及累计待消费增量绝对值不超过 10000。非法请求原子失败，原有待消费请求不变。没有固定步的帧保留请求，多子步帧只在第一个子步消费一次。水平外部速度按 `exp(-external_damping * dt)` 在每个步进后衰减，阻尼默认 6/s、范围 0..1000/s；垂直增量进入既有重力和跳跃速度。Jolt 负责求解位移，保留速度根据其实际接触法线清除撞墙、落地和撞顶后被阻挡的部分；主动输入受阻不会产生反向击退。

外部水平速度属于单个 Jolt 角色实例，禁用、删除、普通节点卸载和物理角色重建时清除；常驻角色切场景继续保留。胶囊尺寸或坡度限制变化重建物理角色；修改阻尼、重力倍率、台阶高度和贴地距离直接应用，不丢弃已有速度。`restore_data` 清除输入与待消费增量，并在下一次同步复位 Jolt 速度；没有新输入时不会继承恢复前的运动。`instance_id` 是组件只读且不复用的身份，插件可据此丢弃同一节点上旧组件的状态。脚本使用 `CharacterMotor.ClearInput()`、`AddVelocity(x, y, z)`、`SetExternalDamping(value)` 访问相同能力。

`cast_sphere()` 查询已同步的物理世界，返回状态与可通行比例，忽略触发器。第一/第三人称输入和相机防穿墙策略由可选运行时[角色控制器插件](../plugins/runtime/class-diagrams/character-controller.md)负责，核心不读取键鼠、不引用该插件。

`cast_ray(collider, origin, displacement, fraction)` 只查询指定 Collider 已提交的碰撞体，复用 Jolt 射线查询和 BodyID 过滤，其他碰撞体不会遮挡目标。源点与位移使用世界坐标；凸体视为实体、三角形允许双面命中。无命中、合法空网格、尚未同步或禁用目标返回 `true/1`；非法输入、外部世界组件返回 `false/0`。它不隐式创建碰撞或推进模拟，目标是否能作为某种玩法支撑由调用方决定。

`query_ray` 与 `query_sphere` 使用世界起点和非零完整位移查询最近 Collider 或 CharacterMotor，返回查询状态及独立的 `PhysicsQueryHit` 值快照。查询成功但未命中时 hit.hit 为 false；参数、已发布世界同步状态或忽略节点无效时返回 false，输出均清空。结果包含场景、节点、物理组件类型与实例身份、世界点与法线、距离和行程比例；球扫距离是球心的行程，命中点位于目标表面。过滤可排除自身节点及其子树，并选择静态、运动学、动态体和 sensor，默认查询全部实体；不引入业务 layer；include_characters 默认 true，独立于 include_kinematic。CharacterVirtual 通过官方 inner body 进入同一个空间查询，不手算胶囊相交。

查询读取已提交的几何及运动类别，须在主线程物理步进外调用，不加载资源或同步场景。删除、重建或禁用 Collider 会在过滤阶段立即排除，避免其残留物理体遮挡后方对象。`is_object_valid` / `is_hit_valid` 按原属场景、节点、物理组件类型和实例及启用状态检查历史结果，位置或形状变化不重新计算命中。射线从凸体内部出发返回零距离、起点与反向位移法线；球扫初始重叠返回零距离及最深接触。完整数值与失效契约见[物理系统类图](class-diagrams/physics-system.md)。


## 范围与接触生命周期

`overlap_sphere` 与 `overlap_box` 返回范围内的全部 PhysicsObject，使用与最近命中相同的过滤规则、实例代际和有效性查询。盒以世界中心和轴对齐半尺寸定义。复用 Jolt CollideShape，按组件身份去重并排序；成功空列表表示没有对象，失败清空输出。查询不推进模拟，调用方无需用多条射线模拟范围。

`contacts()` 是独立的只读 PhysicsContactEvent 数值快照，包含 a/b 物理身份、begin/stay/end、frame、step、接触点、A→B 法线与 is_trigger。每固定子步按对象对发布，复合形状不产生重复对象事件；同帧开始又结束均保留。零子步不生成 stay；休眠持续接触用上游窄相位复核已有对象对，保持 stay。删除/禁用在同步时产生带历史身份的 end；同一组件在接触中切换 Trigger 类型时，先发布旧类型 end，再发布新类型 begin；clear 清空旧事件；卸载普通节点会为仍存活的常驻接触者产生 end，常驻双方的持续接触保留。双方原属 Scene 均已销毁时丢弃旧区间，不向新场景发布旧 end。不要过滤掉对象已无效的 end，也不要把 stay 当作撞击伤害。

脚本查询提供 `Physics.OverlapSphere`、`OverlapBox`、`IsObjectValid`，接触使用 `OnCollisionEnter/Stay/Exit(node, callback)` 和 `OnTriggerEnter/Stay/Exit(node, callback)` 订阅，返回数字 ID，失败为 0，`Off(id)` 解除。回调在 Gameplay.Update 前同步执行，直接提供 self/other 身份、接触点与 self→other 法线；绑定负责整批去重、实例失效和停止/重载清理，不在 Jolt 回调执行业务。`Physics.GetContacts` 仍供显式读取快照；自行轮询时按 frame 整批去重，step 表示单调固定步。示例见 [Physics interactions](../../projects/showcase/physics-interactions.md)，完整生命周期见[脚本接口类图](../plugins/runtime/class-diagrams/physics-audio-script-api.md)。

导航代理可组合已有 Collider 与启用的零线速/角速 kinematic RigidBody：Crowd 唯一写入 Transform，物理镜像形体并支持命中与触发。非零速度 kinematic 保留原有积分与姿态回写语义，不能同时由 Crowd 驱动。此组合不提供动态击退或下落，CharacterMotor 继续独占自己的运动解算。旧 `cast_sphere` 相机/通行比例接口仍忽略角色代理体与 sensor，避免第三人称镜头扫到自身；需要命中角色时使用带 ignoreNode 的 query_sphere。
