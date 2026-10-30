# 公寓物理与音频示例

`projects/showcase` 使用普通场景、组件和资源目录展示物理与音频。运行时没有公寓专用分支；玩法仍通过 `Scene.Load("project/scenes/apartment.scene")` 加载场景。

- 活动相机添加一个 `AudioListener`。
- 76 个建筑和主要家具节点添加静态 Box Collider；没有 RigidBody 的 Collider 为静态体。地面用连续碰撞体覆盖拼板缝隙，顶面与可见地板一致；圆桌等家具使用局部包围盒近似，不宣称精确 Mesh 碰撞。
- `apartment_physics_ball` 是独立根节点：Transform、MeshRenderer、Sphere Collider 和 Dynamic RigidBody。它复用已有单位球 Mesh 与材质，从 2.2 米处落下；均匀缩放后半径为 0.22 米，质量为 0.35 千克。原有家具和装饰不随球一起参与动态模拟。
- `apartment_ambience` 使用 AudioSource 播放 `audio/apartment-ambience.wav`，循环、非空间、音量 0.2。活动相机处于公寓外的总览位置，因此此背景声不做距离衰减；空间声源机制由音频回归单独验证。

WAV 是仓库脚本原创合成的 8 秒正弦和弦，无外部采样素材：24 kHz、单声道、PCM16；整数周期与缓慢周期包络使循环边界连续。相邻 meta 指定导入为 48 kHz 双声道 float PCM；场景通过 meta GUID 引用，打包依赖闭包自动带入音频，不需要给构建清单另列路径。

`scripts/add_apartment_physics_audio.py` 只维护这份示例的源配置，并保留现有节点 ID、资源 GUID 和其他组件；新增节点再次执行时复用其 ID。它为选定家具从已有 Mesh 计算局部包围盒、补齐组件、恢复落球初态并生成音频，既不负责导入缓存，也不打开物理或音频设备。再次执行不改变已生成内容，`--check` 仅检查是否与脚本定义一致。重新通过 MCP 生成公寓后可调用它恢复示例配置。

职责保持单向：编辑器音频导入器解析 WAV 与 meta，输出统一音频资源；运行时资源 codec 校验数据，AudioSystem 和 PhysicsSystem 消费组件；脚本与 MCP 只调用对应模块的公开操作。示例脚本不代替场景编辑命令，也不成为运行包依赖。
