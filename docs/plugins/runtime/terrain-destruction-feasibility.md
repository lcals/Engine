# 地形与建筑破坏的采用边界

运行契约见[地形与破坏](terrain-destruction.md)，制作流程见[编辑器说明](../editor/terrain-destruction-authoring.md)。本页保留必要的选型来源与约束，当前依赖版本统一见[维护清单](../../development.md#当前固定版本)，实际平台验证见[测试文档](../../testing.md#验证范围与限制)。

## 结论与采用范围

- 地形使用 CPU 密度数据、Transvoxel regular-cell 表和现有 bgfx；只参考 Voxel Tools 的连通域/浮岛处理，不引入 Godot。LOD transition 未接入。
- Jolt 提供静态网格、凸形/compound、查询和角色能力；地形碰撞使用分段叶子及组树，渲染仍需处理 Mesh 上传成本。
- Blast 采用 low-level、官方损伤 Shader、编辑器 Authoring 和 ExtStress 标量路径，不引入 PhysX 或 Toolkit。
- 制作输入仍是有界参数试件，不等同任意空心建筑力学；综合世界存档和动态残骸导航未完成。
- FastNoise2、sse2neon 和 SIMDe 是历史研究候选，未接入，不在日常文档维护其平台与版本状态。

## 核对基线

| 开源上游 | 固定研究/接入版本 | 当前用途 |
| --- | --- | --- |
| [Voxel Tools](https://github.com/Zylann/godot_voxel/tree/045ff9326a32fd10ac6fdac6357b5a098aaf2093) | `045ff9326a32fd10ac6fdac6357b5a098aaf2093` | 连通域/浮岛算法参考，未引入 Godot 框架 |
| [Transvoxel](https://github.com/EricLengyel/Transvoxel/tree/51a494f03c5b024cd153b596bcc7152eb3cc93a6) | `51a494f03c5b024cd153b596bcc7152eb3cc93a6` | 正式子模块，regular-cell 查找表，MIT |
| [FastNoise2](https://github.com/Auburn/FastNoise2/tree/721400f9560831be2fc9790be9b72c44bee7d33f) | `721400f9560831be2fc9790be9b72c44bee7d33f`；候选发布 `v1.1.1` | 尚未接入 |
| [Blast](https://github.com/NVIDIAGameWorks/Blast/tree/636e7eb43b8d41e44c40eb1b4799bfd027f17ba7) | `636e7eb43b8d41e44c40eb1b4799bfd027f17ba7`，5.1.0 | 正式子模块，NVIDIA BSD-3-Clause；附属依赖保留各自许可 |

正式复用保留固定版本、源文件许可和必要来源说明；Voxel Tools 的完整 MIT 声明见[许可记录](../../../third_party/notices/voxel-tools-terrain.md)，Blast 兼容补丁及其依据位于 [third_party/patches/blast](../../../third_party/patches/blast/README.md)。商业/私有参考源码、实现和资源不得进入仓库。

## 适配边界

Voxel Tools 的完整实现依赖其 VoxelBuffer、数学与 Godot 外层，不能作为独立引擎库直接接入。Blast ExtStress 使用上游标量求解器，目标级适配隔离 x86 SIMD/探测路径；补丁依据、许可与维护方式见 [Blast 补丁说明](../../../third_party/patches/blast/README.md)。制作工具只进入编辑器，设备消费编译后的独立资源。

上游平台声明、独立 ARM 探针与本引擎真实场景验收是不同证据。旧 Mac/Android 探针不证明当前完整 APK、实体手机、iOS 宿主或长期热负载通过；阶段报告保留于 Git，不在本文持续累积。历史局部碰撞指标不能代表整条地形、破坏和物理链路达标。
