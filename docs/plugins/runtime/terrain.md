# 体积地形插件

运行时与制作工具由 `runtime/terrain` 和 `editor/terrain` 两个插件实现。前者提供 `TerrainVolume`、`TerrainSystem` 及普通节点上的 `TerrainComponent`；后者负责源数据导入、组件编解码、可撤销编辑与 MCP。核心只补通用瞬时网格碰撞提交，不依赖地形插件。运行时复用现有 MeshRenderer 和 Jolt，生成的显示/碰撞组件标记为瞬态，不重复写入场景文件。

默认插件清单包含两者；显式选择时将 `terrain` 加入对应 `ENGINE_RUNTIME_PLUGINS` / `ENGINE_EDITOR_PLUGINS`。Transvoxel 固定版本 `51a494f03c5b024cd153b596bcc7152eb3cc93a6` 作为子模块，只使用普通单元查找表；初始化方式为：

```sh
git submodule update --init third_party/transvoxel
```

`.terrain` TOML 描述尺寸、采样间距、16/32 格分块、起点、种子、平面/正弦起伏和球形洞穴，邻接 `.meta` 保存唯一 GUID，资源管线增量生成 `cache/gen/<GUID>`。运行时密度为浮点数；每轴最多 256 格且总样本最多 4M。高度图、FastNoise2、LOD 过渡仍待实现；步骤 3/4 已增加调用方显式地基联动与动态土石塌方，建筑连接损伤和应力归[独立破坏插件](destruction.md)。

MCP 工具包括 `create_terrain_asset`、`set_terrain`、`get_terrain`、`terrain_brush`、`terrain_reset`、`terrain_save_state` 和 `terrain_load_state`。笔刷、设置、重置与加载支持现有撤销/重做事务；保存源文件或状态文件本身不使用场景撤销。球形修改采用节点局部坐标，失败保留有效版本。脚本 `TerrainComponent` 提供 `DigSphere`、`FillSphere`、`CollapseDetached`、`RetireFragments`、`Reset`、`Save`、`Load` 和只读统计。步骤 4 增加非撤销运行命令 `terrain_collapse_detached`、`terrain_reset_runtime`，`get_terrain` 返回 `fragmentCount`；动态土石不能写入旧的静态体积存档，塌方后保存/加载与可撤销体积编辑须先显式 Reset。`RetireFragments(maxCount, maxAgeSeconds)` 按数量与模拟寿命显式回收，回收不回填已抽离密度，也不解除静态存档限制，见[回收契约](class-diagrams/fragment-retirement.md)。

状态文件保存完整体积与基础资源 GUID/hash，恢复时校验基础版本，不覆盖 `.terrain` 或导入缓存。编辑器保存路径限制在 `cache/terrain-states/*.terrainstate`；脚本使用 App 持久目录的 `terrain/<slot>.bin`。两种入口共用相同运行时格式，路径职责分离。

当前脚本笔刷与土石抽离复用块缓存，CPU 只重建实际变化样本影响的块及相邻梯度法线范围。渲染仍合并为单个 Mesh 并完整上传 GPU；碰撞则按稳定的 4 单元叶子分段提交，复用未变 Jolt 形状和上层分组，一个地形保持一个 Collider/BodyID。MCP 笔刷经 `TerrainStateEdit` 提交 ETRS 快照，`restore` 会重新初始化体积，因此 MCP 笔刷、撤销/重做、加载和重置仍重建全部块；脚本 `Load`/`Reset` 同样如此。同步接口保留；异步球形笔刷在共享任务域准备独占体积/网格，按请求版本在主线程有界发布。Reset、首次生成、连通塌方及 MCP 事务仍有同步路径；分块 GPU 上传和差量存档尚未实现。接口与形状复用细节见[当前实现](../../runtime/performance.md#当前实现)。

架构见[运行时类图](class-diagrams/terrain.md)、[编辑器类图](../editor/class-diagrams/terrain-editor.md)和[脚本接口](class-diagrams/terrain-script-api.md)；跨插件职责见[联动契约](terrain-destruction.md)；固定来源与历史平台研究见[技术研究](terrain-destruction-feasibility.md)。

公开实验场与运行验证见[测试与范例](../../testing.md)。
