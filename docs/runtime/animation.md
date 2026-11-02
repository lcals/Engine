# 动画模块

`AnimationSystem` 使用 [ozz-animation](https://github.com/guillaumeblanc/ozz-animation) 完成骨架采样、局部姿态混合和模型空间骨骼矩阵计算；渲染系统通过 compute 完成 GPU 蒙皮。运行时不依赖 FBX 解析器或编辑器，旧模型与新模型使用相同的组件和执行路径。

同一个普通 `Node` 上挂 `Animator` 和 `MeshRenderer`。模型、骨架、Mesh 和每个动作都是独立 GUID 资源；动作引用源骨架，Mesh 引用目标骨架。当前运行时按完整关节名称及父层级匹配轨道，再映射到 ozz 的 DFS 次序，不要求 GUID 或数组索引相同。目标骨架的每个关节都必须在源骨架中有完整路径对应，即使它没有蒙皮权重也不能缺失；源骨架可有额外关节。对应关节完全没有动作轨道时，使用目标骨架的默认局部 TRS；已有轨道的平移、旋转、缩放键均须有效且非空，不能借回退掩盖坏轨道。

有轨道时直接使用源局部 TRS，不包含语义、参考姿态、骨轴或体型补偿；名称和层级匹配不保证不同默认/绑定姿态下的动作效果正确。FBX 外部骨架引用也只按完整节点路径采样，限不带网格的动画源；其缺节点规则见 [FBX 导入契约](../plugins/editor/class-diagrams/fbx-importer.md)，不能将它当作运行时缺骨回退或跨层级重定向。

已校验的 FlatBuffers 数据经 ozz 纯内存 Builder 转换为不可变运行对象，不读取未经校验的 ozz archive。场景缓存按骨架 GUID、资源代际，以及动作的源/目标骨架身份共享编译结果；缓存保留本场景已使用动作，往返切换不重复编译。每个实例独占时间、采样上下文、局部姿态和矩阵。组件删除或停用等条件在系统更新时清理采样条目及组件变形，编译缓存继续保留；停用不销毁 Animator 的播放状态及打断过渡快照。`clear()` 清理系统条目及编译缓存，切场景和结束玩法同样释放该系统缓存，不建立进程永久动画缓存。

`SamplingJob` 分别采样两层动作，`BlendingJob` 混合局部平移、旋转和缩放，最后一次 `LocalToModelJob` 生成模型矩阵。混合与共享不可变运行对象分别参照 ozz 的 [blend](https://github.com/guillaumeblanc/ozz-animation/blob/master/samples/blend/sample_blend.cc) 和 [multithread](https://github.com/guillaumeblanc/ozz-animation/blob/master/samples/multithread/sample_multithread.cc) 示例；本模块借用宿主的 `TaskSystem`，复用 oneTBB worker，不创建自己的线程池。

主线程收集场景与播放状态、维护编译缓存并推进时间；待采样实例达到 `AnimationConfig::min_parallel_count`（默认 8）且超过一个 `batch_size`（默认 16）时，按批次并行执行 ozz 采样、混合及两组矩阵计算。各批次独占实例状态，只共享不可变骨架和动作，不在 worker 访问场景组件。全部批次完成后主线程发布 palette，保持原有本帧更新顺序。未注入任务域、工作量不足或任务域拒绝整个批次时走相同串行计算；已接收的执行失败按失败返回，不重跑。

两项参数均须为正数；实际并发额度由宿主任务域决定，可按平台预算调整。当前 tick 内等待本帧计算，不跨帧写回，不与后续主线程阶段重叠；首次动作编译仍在主线程。Tracy 的 `Animation.sample_batch` 显示批次执行线程，`Animation.sample_join` 显示主线程等待或串行计算，`Animation.publish` 显示结果发布；分析应区分首次编译与稳定采样帧。

骨骼 palette 组合 Mesh 到骨架绑定变换、关节当前模型变换和关节逆绑定矩阵，输出为 Mesh 局部坐标、Mesh 引用的目标 `SkeletonData` 原始关节索引顺序、列主序 `float[16]`，不是源动作索引或 ozz DFS 次序。法线 palette 仅保存线性 inverse-transpose，切线和副切线使用普通 palette。渲染 compute 对每个实例每次姿态更新生成一次顶点流，主画面、阴影和 AO 共用；Mesh 资源的绑定顶点、拓扑和资源身份不变。动画系统不再每帧处理或复制全部顶点。

`play()` 从停止重新开始、从暂停继续；`pause()` 保留姿态并冻结过渡；`stop()` 立即恢复原 Mesh，取消过渡。`seek(seconds)` 支持 `[0, duration]` 定位并取消过渡，停止时定位进入暂停。循环保留跨周期余数，单次播放到结尾暂停；速度 `0..16`，零速度冻结动作时间。

`set_animation()` / `set_settings()` 校验设置并加载 Animation 资源，不同步检查它与 Mesh 的绑定；返回成功不代表该骨架组合已可播放，绑定不兼容会在 AnimationSystem 准备时失败。`cross_fade(path, seconds)` 则先预检资源及骨架绑定，再切换并从零秒播放目标动作；时长须有限且非负，零秒取消混合直接切换。切换请求即时提交，实际姿态在下次动画更新生成。若请求与当前动作 GUID 及资源代际相同且当前未停止，直接成功返回，不重启、不解除暂停，也不改已有过渡。

正常过渡中两层动作独立采样，来源保留请求时的速度和循环设置；再次打断时从已显示的混合局部姿态快照继续，不增长混合树。过渡时长使用实际秒数，不受动作速度影响；短动作到末帧后仍完成剩余过渡。暂停冻结两层时间及混合进度，直接换动作、停止和定位会取消过渡。

待机、走跑、跳跃、受击等状态及优先级由项目脚本或玩法插件选择，核心只提供播放与混合。脚本使用 `CrossFade`、`SetSpeed`、`SetLoop` 等组件接口；脚本中按字符串选择的动作须显式列入项目打包清单，不能依赖编辑器全量目录掩盖缺少的运行资源。

公开契约见 [Animator](class-diagrams/animator.md)、[AnimationSystem](class-diagrams/animation-system.md) 和 [动画脚本接口](../plugins/runtime/class-diagrams/animation-script-api.md)。

## InPlace 与角色移动

当前项目默认采用 InPlace：物理控制器或导航系统负责节点移动，动画只表现姿态。源动作若包含持续前进的骨骼位移，须先在导入时去除；否则节点移动与骨骼位移叠加，动作每次循环又返回首帧，画面会周期性后跳。`SetSpeed` 只改变播放速率，不重置播放时间，也不能消除这段位移。

FBX 导入复用 ozz `MotionExtractor`，在相邻 `.meta` 按动画 key、完整关节路径和局部轴配置。例如 showcase 的公开 walk 动作使用：

```toml
[[animation_motion]]
animation = "animations/walk"
joint = "root/Hips"
position = [false, false, true]
rotation = [false, false, false]
```

该配置将选中轴保留在首帧位置，保留未选轴的上下起伏和侧向姿态。骨骼与轴由资源指定，引擎不猜测模型约定；无配置的片段保留源数据。完整参数、旋转提取对平移的影响及增量发布规则见 [FBX 导入契约](../plugins/editor/class-diagrams/fbx-importer.md)。

项目按实际水平速度除以源动作参考步速设置播放倍率；showcase walk 的参考步速约为 `1.4253 / 1.333333 = 1.068975 m/s`。这只匹配平均步频，不提供脚锁或接触 IK。

当前实现的是导入阶段的原地动画，尚未输出独立运动曲线或实现 RootMotion 驱动及运行时模式切换。未来 RootMotion 需把提取出的位移交给角色运动求解，并处理循环、混合和碰撞；不能直接把骨骼位移当作节点移动。

## 动画复用与重定向方向（规划）

完整资产制作流程见[角色与动画制作](../editor/character-production.md)。模型检查、绑定创建、权重生成及服务调用在离线工具或编辑器侧执行；已有优质绑定优先保留，无绑定或不可用绑定可在派生副本中标准化重建。AI 负责调用与验证，不要求这些算法使用机器学习。

引擎需要支持不同人形骨架共享源动作并在运行时重定向，不能只靠为每个角色离线烘焙一份片段。编辑器侧显式生成和校验骨骼语义/骨链对应、参考姿态、骨轴、比例及 root/hips 策略；运行时按配置执行，不猜测资产或项目含义。骨架层级、绑定姿态或导入坐标变化后，原适配配置必须失效重算。

目标处理顺序为源动作采样及所需坐标计算、转换到目标局部姿态、在同一目标骨架上混合，再经必要接触修正生成骨骼矩阵，交给已有 GPU 蒙皮。源动作与不可变数据可以共享，角色姿态和采样状态继续独立；新增缓存遵守资源代际及场景释放边界，不能污染原 Mesh 或 Skeleton。

现有两动作过渡与共享缓存继续复用，不为此另建完整动画编辑器、通用状态机或 Manager。Wicked/Godot 的实现可评估复用，但不是已接入的独立依赖；具体数据、公开接口与关系在后续类图评审后确定。通用离线/运行时重定向、骨链分配、Root Motion 驱动及接触 IK 均仍属未实现能力；简单骨名映射和位移缩放不能当作任意体型的脚锁、握物与全身 IK 保证。
