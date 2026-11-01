# 动画模块

`AnimationSystem` 使用 [ozz-animation](https://github.com/guillaumeblanc/ozz-animation) 完成骨架采样、局部姿态混合和模型空间骨骼矩阵计算；渲染系统通过 compute 完成 GPU 蒙皮。运行时不依赖 FBX 解析器或编辑器，旧模型与新模型使用相同的组件和执行路径。

同一个普通 `Node` 上挂 `Animator` 和 `MeshRenderer`。模型、骨架、Mesh 和每个动作都是独立 GUID 资源；动作引用源骨架，Mesh 引用目标骨架。运行时按完整关节名称及父层级匹配轨道，再映射到 ozz 的 DFS 次序，相同层级的独立源骨架和目标骨架可复用动作，不要求 GUID 或数组索引相同。不同层级的重定向由离线导入处理，不在运行时猜测骨骼含义。

已校验的 FlatBuffers 数据经 ozz 纯内存 Builder 转换为不可变运行对象，不读取未经校验的 ozz archive。场景缓存按骨架 GUID、资源代际，以及动作的源/目标骨架身份共享编译结果；缓存保留本场景已使用动作，往返切换不重复编译。每个实例独占时间、采样上下文、局部姿态和矩阵。禁用或删除组件释放实例数据；`clear()`、切场景和结束玩法释放编译缓存，不建立进程永久动画缓存。

`SamplingJob` 分别采样两层动作，`BlendingJob` 混合局部平移、旋转和缩放，最后一次 `LocalToModelJob` 生成模型矩阵。混合与共享不可变运行对象分别参照 ozz 的 [blend](https://github.com/guillaumeblanc/ozz-animation/blob/master/samples/blend/sample_blend.cc) 和 [multithread](https://github.com/guillaumeblanc/ozz-animation/blob/master/samples/multithread/sample_multithread.cc) 示例；本模块当前在统一帧调度中执行，不引入额外工作线程。

骨骼 palette 组合 Mesh 到骨架绑定变换、关节当前模型变换和关节逆绑定矩阵，输出为 Mesh 局部坐标、源骨骼索引顺序、列主序 `float[16]`。法线 palette 仅保存线性 inverse-transpose，切线和副切线使用普通 palette。渲染 compute 对每个实例每次姿态更新生成一次顶点流，主画面、阴影和 AO 共用；Mesh 资源的绑定顶点、拓扑和资源身份不变。动画系统不再每帧处理或复制全部顶点。

`play()` 从停止重新开始、从暂停继续；`pause()` 保留姿态并冻结过渡；`stop()` 立即恢复原 Mesh，取消过渡。`seek(seconds)` 支持 `[0, duration]` 定位并取消过渡，停止时定位进入暂停。循环保留跨周期余数，单次播放到结尾暂停；速度 `0..16`，零速度冻结动作时间。

`cross_fade(path, seconds)` 在资源与骨架绑定预检成功后切换，目标动作从零秒播放；零秒立即切换。正常过渡中两层动作独立采样，来源保留请求时的速度和循环设置；再次打断时从已显示的混合局部姿态快照继续，不增长混合树。过渡时长使用实际秒数，不受动作速度影响；短动作到末帧后仍完成剩余过渡。暂停冻结两层时间及混合进度，直接换动作、停止和定位会取消过渡。

待机、走跑、跳跃、受击等状态及优先级由项目脚本或玩法插件选择，核心只提供播放与混合。脚本使用 `CrossFade`、`SetSpeed`、`SetLoop` 等组件接口；脚本中按字符串选择的动作须显式列入项目打包清单，不能依赖编辑器全量目录掩盖缺少的运行资源。

公开契约见 [Animator](class-diagrams/animator.md)、[AnimationSystem](class-diagrams/animation-system.md) 和 [动画脚本接口](../plugins/runtime/class-diagrams/animation-script-api.md)。
