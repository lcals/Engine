# 动画模块

`AnimationSystem` 使用 [ozz-animation](https://github.com/guillaumeblanc/ozz-animation) 完成骨架采样、局部到模型空间转换和 CPU 蒙皮。运行宿主借用世界系统创建动画系统；运行时不依赖 FBX 解析器或编辑器。

同一个普通 `Node` 上挂 `Animator` 和 `MeshRenderer`。模型、骨架、Mesh 和每个动作都是独立 GUID 资源；动作引用源骨架，Mesh 引用目标骨架。运行时按完整关节名称及父层级匹配轨道，再映射到 ozz 的 DFS 次序，因此相同层级的独立源骨架和目标骨架可以复用动作，不要求 GUID 或数组索引相同。

已校验的 FlatBuffers 数据经 ozz 纯内存 Builder 转换为运行对象，不读取未经校验的 ozz archive。每个动画实例拥有采样上下文、姿态和变形缓冲；清除实例、禁用组件、删除节点、替换场景和结束玩法都会释放相应缓存。Mesh 保存的绑定姿态及资源身份不被修改，渲染器消费瞬态顶点流并更新 GPU 顶点缓冲。

蒙皮矩阵组合 Mesh 到骨架绑定变换、关节当前模型变换和关节逆绑定矩阵，输出仍为 Mesh 局部坐标。法线使用 inverse-transpose，切线和副切线使用普通线性变换，重建正交切线及镜像手性。

`play()` 从停止重新开始、从暂停继续；`pause()` 保留姿态，`stop()` 立即恢复原 Mesh。`seek(seconds)` 支持 `[0, duration]` 定位，停止时定位进入暂停。循环保留跨周期余数，单次播放到结尾暂停；速度 `0..16`，零速度冻结时间。当前不包含多动作混合、骨骼重定向和根运动提取。

公开契约见 [Animator](class-diagrams/animator.md) 和 [AnimationSystem](class-diagrams/animation-system.md)。
