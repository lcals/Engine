# 静态模型离线 LOD

离线生成、正式资源导入和组件运行选层已接入。实际测试结果与平台限制统一见[测试文档](../testing.md)。

[类图](class-diagrams/static-model-lod.md)

## 现有能力与上游选择

- `meshoptimizer` 已是资源管线依赖，当前版本为 1.3。复用已链接的
  `meshoptimizer::meshoptimizer`，不增加减面库或运行时依赖。
- bgfx API 167 对应的 `examples/42-bunnylod` 使用 `progmesh.c` 展示渐进网格；
  `examples/12-lod` 加载预先生成的多份树网格，展示切换与 stipple 过渡。
  两者不是完整的 PBR 离线模型导入管线。首期采用 meshoptimizer 官方属性感知减面，
  参考 12 的多层绘制组织；抖动过渡在后续独立接入。
- `MeshData.indices/submeshes` 保存 LOD0，`lods` 保存较低层级；`GpuGeometry` 已共享不可变顶点与索引，
  `GpuMesh` 仅拥有实例姿态和变形缓冲。继续沿用这种所有权。
- 相机选择、烘焙、反射和主视图已复用 `SceneGpuResources::draw_meshes`；
  LOD 选择接在这一收集路径中，不能在 TS 每帧替换网格。

上游参考：

- [meshoptimizer v1.3](https://github.com/zeux/meshoptimizer/tree/v1.3)，MIT。
- [bgfx 12-lod](https://github.com/bkaradzic/bgfx/blob/b2513038f/examples/12-lod/lod.cpp)，BSD-2-Clause。
- [bgfx 42-bunnylod](https://github.com/bkaradzic/bgfx/blob/b2513038f/examples/42-bunnylod/bunnylod.cpp)，BSD-2-Clause；
  其中渐进网格算法不作为引擎的减面实现。

引入上游源码片段时保留对应来源和版权；直接调用已选库时继续由现有依赖管理许可。

## 最小数据契约

`include/schemas/runtime/mesh.fbs` 定义 `MeshLodData`：

| 字段 | 类型 | 含义 |
| --- | --- | --- |
| `screen_height` | float | 从前一级进入本级的屏幕高度占比阈值，有限、在 (0,1] 内，逐级严格递减 |
| `indices` | uint[] | 本级三角形索引，引用同一 MeshData 的原始顶点 |
| `submeshes` | MeshSubmesh[] | 本级材质槽索引区间；槽号与 LOD0 对应 |

`MeshData.lods` 只保存 LOD1 及更低层级。LOD0 继续使用现有 `indices/submeshes`，
既有源文件未配置时仍只有 LOD0。所有层共享顶点、UV0、UV2、法线、切线和颜色；
首期不移动或重建顶点，不添加第二份持久几何模型。

LOD 是同一静态 Mesh 的可替换渲染拓扑，沿用现有独立 Mesh GUID 和 `cache/gen/<GUID>`。
不增加 LOD 资源种类、不把整个 FBX 同源资源变成一个运行时容器。
Model、Mesh、Material 和 Texture 的真实引用关系保持不变。

新增字段由 Schema 指纹触发相关 Mesh 的正常增量导入。单项解码校验必须检查：
有限阈值、严格递减顺序、非空三角形索引、顶点范围、材质槽与完整区间覆盖、
有限层级列表以及各级索引数严格减少。没有实际减少的候选不写入，后续层的阈值保持来源对应。
重复阈值、越界和错误数据不能被静默截断成可用结果。

本次不改变碰撞、导航、破坏、光照烘焙使用的原始网格语义；这些系统继续读取 LOD0。

## 源资源设置与统一导入

在 `.mesh`、建模 `.mesh`、FBX、USD 的相邻 `.meta` 中使用可选的源级配置：

```toml
[lod]
enabled = false
triangle_ratios = [0.5, 0.25, 0.125]
screen_heights = [0.3, 0.15, 0.075]
max_relative_error = 0.01
```

以上是默认导入参数；质量需按实际模型确认，不代表所有模型均能达到目标比例。
数组必须非空、等长且全部为有限值；比率在 (0,1) 内且严格递减；阈值如上；
误差在 (0,1] 内。每一级相对 LOD0 单独生成，保证误差均相对于原始网格，避免逐级误差累积。
拓扑约束可能使实际三角形数高于目标；返回真实数量，不为了满足比例放松质量限制。

复用 `src/engine/editor/mesh/` 的现有公用离线处理位置，增加内部自由函数，
由每个导入器在切线生成及全部顶点拆分结束后调用：

```cpp
[[nodiscard]] bool generate_mesh_lods(schema::MeshDataT& mesh,
                                      const toml::table& meta,
                                      std::string& error);
```

此函数和声明留在 `src/engine/editor/mesh/mesh_lods.{hpp,cpp}`，不是核心公开接口。
输入 mesh 先复制候选的 LOD 输出，完成全部校验后一次提交；失败不改变原 mesh。
不新建 `LodSystem`、`LodManager`、资源发布器或任务池。
已有导入器拥有 meta 识别和工具指纹，管线仍负责任务、SQLite、GUID、依赖及原子发布。

减面流程：

1. 确认静态 Mesh，保留 LOD0 的所有值和材质槽。
2. 按原始 submesh 的三角形范围分别减面，防止跨材质合并；未显式分组视为槽 0。
3. 调用 `meshopt_simplifyWithAttributes`，使用法线、UV0、存在的 UV2、切线和颜色作为属性度量；
   按 meshoptimizer 的官方参数单位处理相对误差，不使用 sloppy/update/permissive 路径。
4. 使用 `meshopt_SimplifyLockBorder`，保留材质分区边界、UV chart 接缝与切线手性不连续处。
   拆开的同位置顶点需要明确辨认并锁定，不能仅假定“调用属性函数就一定不会破坏接缝”。
5. 对各级各材质范围调用 `meshopt_optimizeVertexCache`；保持原顶点数组，共享 GPU 顶点缓冲。
6. 输出实际有效层级，正常写入原 GUID 的候选缓存；本批失败保留上一批完整版本。

属性权重需使用明确的固定初值与 PBR 对照验证。首期不把大量属性权重作为用户参数；
不能宣称“索引合法”就意味着视觉等价。高对比法线贴图、UV 接缝、顶点色和薄片是必测内容。

FBX 包内静态 Mesh 可以生成；带 skin 的 Mesh 保留 LOD0，并在导入结果报告未生成原因。
混合静态与蒙皮的源文件允许只为静态 Mesh 生成。没有可减面网格时返回明确结果，
不能把空结果描述成已经生成多层。非法配置或转换错误使本次导入失败。

## 运行选择、视图隔离与开关

`GpuGeometry` 在原有共享资源内增加每级索引缓冲及材质区间，仍只拥有一份顶点缓冲。
`GpuMesh` 不复制 LOD 缓冲。所有缓冲由原有 SceneGpuResources 缓存持有并在 bgfx 关闭前销毁。

渲染收集阶段：

1. 先完成有效激活、手动隐藏和自动视锥剔除。
2. 将同一保守世界包围盒的 8 个角投影到当前实际视图，屏幕高度占比为
   `(ndc_y_max - ndc_y_min) / 2`。不先裁到屏幕边界；否则屏幕边缘大物体会错误降级。
   透视与正交均使用实际投影矩阵；物体跨越近裁面、位于相机内或数值异常时选 LOD0。
3. 依当前层及阈值选择：降低精度需低于阈值乘 `(1-h)`，提高精度需高于阈值乘 `(1+h)`；
   h 为内部选择函数的滞回比例，首期固定 0.1。允许跨越多层，不能每帧只追一层。
4. 主视图的 depth、surface、opaque 共用本帧选层；相同物体不能不同 pass 使用不同拓扑。
5. 反射按反射相机自身的视图和分辨率独立选择并维护历史，不能改写主视图层级。
   场景光照烘焙与几何采样固定 LOD0；首期阴影固定 LOD0，避免把主镜头规则套进光源视锥。

历史保存在现有 `SceneGpuResources` 的私有按视图缓存中，仅保存身份与选层数字，
不保存 ECS 列指针。键包含场景代次、观察者组件实例身份、反射面实例身份（适用时）、
网格组件实例身份及源资源修订；bgfx view id 会被复用，不能单独作历史键。
删除、重建、切场景、资源热重载和开关变化清理对应记录。

运行开关属于每个 `MeshRendererDataT.static_lod_enabled`，默认 false；
使用 `MeshRenderer::static_lod_enabled()` / `set_static_lod_enabled(bool)` 读取与编辑。
RenderSystem 只读组件，不保留平行全局开关。Camera 的视锥开关只控制可见性，
MeshRenderer 的静态实例化开关只控制该网格是否参与合批，三者分别属于适当组件。
该网格关闭时立即选 LOD0 并清理其历史；不删除离线资源，也不影响其他网格实例。
源 `.meta` 的 `lod.enabled` 控制“是否生成”，网格组件属性控制“本实例是否使用”，两者都需可读回。

首期下列实例固定 LOD0，并在诊断结果报告原因：蒙皮、瞬态动态网格、顶点变形、
任一材质为非 opaque、绑定有效 Lightmap 的实例。保留烘焙网格哈希与姿态校验，
不以减面悄悄改变已有 lightmap 语义。后续可在光照贴图 UV/chart 与误差验收后扩展。
自动实例化的批次键必须包含最终 LOD 及对应索引缓冲；跨层实例不能误合批。

## 编辑器、MCP 与保存

源级 LOD 导入设置放在编辑器资源导入属性中。复用现有命令与资源管线：

- `get_mesh_lod_import`：按 assets 相对源路径读取配置，并返回各 Mesh 子资源的 GUID、
  实际层级、各级三角形数及未生成原因。
- `set_mesh_lod_import`：校验并原子更新源 `.meta` 的 `lod` 表，保留主/子 GUID 和无关参数；
  触发现有 `AssetPipelineSystem::build_assets` 增量导入，返回实际完成状态与 revision；
  保存源配置失败不触发导入。现有管线是同步调用，不另建异步任务框架。
- 运行时 LOD 开启、关闭和状态查询复用 MeshRenderer 组件属性的 MCP 编辑命令；
  保存进入场景/预制体，不新增另一套全局设置工具。

MCP 只登记输入 schema、调用编辑器命令并格式化结果；不直接运行减面、写 gen、
改 SQLite 或持有模型原始解析器。重复设置相同参数不应触发无意义重导入。
在导入期间修改设置仍由现有批次指纹校验拒绝过期提交。

导入设置位于 `.meta`；运行功能开关保存到 `MeshRendererDataT.static_lod_enabled`，属于通用渲染属性。
不放入 EnvironmentComponent、PostProcessComponent 或独立 Render 全局配置。
首期不新增每对象强制层级的持久公开接口；调试可用统一诊断视图展示本帧层级与数量。

## 实现位置

| 位置 | 修改 |
| --- | --- |
| `include/schemas/runtime/mesh.fbs` | 追加 MeshLodData 与可选 lods |
| `src/engine/runtime/resource/codecs/mesh_resource_codec.cpp`、`runtime/utils/mesh_utils.cpp` | 结构、索引、材质槽及阈值校验 |
| `src/engine/editor/mesh/mesh_lods.{hpp,cpp}` | 公用内部离线减面函数 |
| `src/engine/editor/asset_pipeline/importers/mesh_importer.cpp` | 普通 .mesh 设置、指纹、生成 |
| `src/engine/plugins/editor/modeling/modeling_mesh_importer.cpp` | 建模资源的相同生成流程 |
| `src/engine/plugins/editor/asset_import/fbx_importer.cpp`、同目录 `usd_importer.cpp` | 静态模型子资源生成、结果报告 |
| `cmake/editor/targets.cmake` 与对应插件清单 | 共用内部离线目标链接 meshoptimizer，不链接进运行时 |
| `src/engine/runtime/render/resources/gpu_geometry.{hpp,cpp}` | 共享 LOD 索引缓冲生命周期 |
| `src/engine/runtime/render/scene_gpu_resources.hpp`、`scene_draw.cpp` | 按视图选择、滞回及同视图各 pass 一致性 |
| `include/engine/editor/asset_pipeline/mesh_lod_commands.hpp`、`src/engine/editor/asset_pipeline/mesh_lod_commands.cpp` | 源 meta 的查询与编辑命令，具体归档与总评审统一 |
| `src/engine/plugins/editor/mcp/` 相应资源工具 | 薄层 MCP 注册 |

## 验收边界

功能验收采用 Release，Debug 只要求受影响目标编译。测试按所属模块归档。

- 生成：小网格、完全不能减面的网格、多材质、UV 接缝、法线贴图、顶点色、坏参数；
  三角形比例达不到目标时保留真实结果，LOD0 字节数据不变。
- 管线：四种来源走同一算法，参数变化只重建相关 GUID；GUID 不变；失败保留完整旧缓存；
  相同参数再次导入命中缓存；删除源移除资源，其他同源未使用资产不被扩大打包依赖。
- 运行：正交与透视、不同 FOV、窗口尺寸、非均匀缩放、相机穿过包围盒、阈值来回抖动、
  反射与主视图不同选层、热重载和对象删除后不复活历史。
- 渲染：同视图 depth/surface/opaque 逐级一致；透明、蒙皮、动态、Lightmap 实例固定 LOD0；
  关闭开关与基线画面一致，开启后记录真实每级网格数和提交三角形数。
- MCP：设置读回、源路径越界、错误参数、失败状态、同值更新、网格属性关闭生效；
  引擎核心和运行包不依赖 TOML、meshoptimizer 或编辑器 MCP。

首期不包含：平滑过渡、几何 morph、HLOD、流式 LOD、自动蒙皮减面、透明排序 LOD、
运行时在线减面、碰撞 LOD 或破坏碎块自动重新减面。这些不作为本次已完成能力宣称。

当前实现已包含离线生成、四类导入挂接、资源命令和共享 GPU 索引缓冲；
选层接入主绘制收集。针对性测试为 `editor.mesh_lod`、`editor.mesh_lod_pipeline`、
`runtime.mesh_lod_selection` 与真实 GPU 的 `engine.render_submission`。
后者比较剔除/实例化逐像素结果、材质覆盖/负缩放批次隔离和实际 LOD 提交三角形数量。
实际通过结果统一记录到 `docs/testing.md`，本说明不以语法检查代替功能验收。
