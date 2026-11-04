# bgfx 上游渲染能力接入

[类图入口](../class-diagrams/index.md) · [现行渲染](rendering.md)

当前状态：最小接口已获用户评审，七项能力和统一控制已接入源码及公开范例。构建和实际验收结果见[测试记录](../testing.md)，不能将源码接入等同所有平台验收。继续使用既有 bgfx API 167、标准 PBR、HDR、反射与 ECS 架构。

## 能力范围与类图

| 能力 | 本次实现范围 | 对应类图 |
| --- | --- | --- |
| 17 基准与视锥剔除 | showcase 固定渲染用例、按实际视图的保守边界剔除、真实提交统计 | [视图提交](class-diagrams/render-submission.md) |
| Frame/View uniform | 复用 bgfx 正式频率接口，按帧/视图提交公共参数，保留逐 draw 材质 | [视图提交](class-diagrams/render-submission.md) |
| 05 静态实例化 | 兼容材质/光照的不透明静态网格，正式 Shader 实例变体和安全回退 | [视图提交](class-diagrams/render-submission.md) |
| 42 减面与 12 LOD | meshoptimizer 离线属性减面、原 Mesh GUID、屏幕占比和切换滞回 | [LOD](../editor/class-diagrams/static-model-lod.md) |
| 28/29 线框和调试 | 限定主场景的真实边线、上游 debugdraw、包围盒/视锥及模块提交线段 | [功能与调试](class-diagrams/render-feature-controls.md) |
| 53-sky2 | 大气 LUT、天空、显式太阳灯联动、现有阴影与唯一 HDR 映射 | [大气与材质](class-diagrams/atmosphere-hex-material.md) |
| 49 Hex-Tiling | 可选标准 PBR 材质参数，共用颜色/法线/ORM 采样与一致梯度 | [大气与材质](class-diagrams/atmosphere-hex-material.md) |

42 的 progmesh.c 不是 meshoptimizer；12 的示例按距离切现成 LOD，不直接提供屏幕占比算法。采用 meshoptimizer 1.3 的正式属性减面，示例仅作为对应渲染组织的参考，不冒充完整现成管线。

## 统一开启关闭入口

统一编辑器命令入口与 MCP `get_render_features` / `set_render_feature`，公开 showcase 的 Rendering features and submission 页面提供正常 RmlUi 操作面板。它们发现真实能力并转发到实际状态拥有者，不建立第二套场景、材质状态或新的 Manager。

- 运行属性：视锥剔除属于 Camera，实例化与 LOD 属于各 MeshRenderer，沿现有组件保存/预制体覆盖链路；不另存全局运行开关。
- 调试：线框、调试线、包围盒、视锥和统计仅由 MCP 操作 RenderSystem 临时诊断状态，不写场景。
- 天空：写现有 EnvironmentComponent 的生成数据，支持场景保存、撤销和重开。
- Hex：写指定材质槽的具名参数，遵守现有材质实例覆盖规则。
- 离线 LOD：源 .meta 通过 get_mesh_lod_import / set_mesh_lod_import 编辑，参数变化走正常增量导入；运行开关不改资产。
- 参数频率升级属于渲染后端实现，查询显示 Frame/View 路径，不制造可视效果开关。
- 查询区分 requested / supported / effective 和原因，目标不明确、设备或 Shader 不支持时明确返回；不能只更新勾选状态。
- TS 通过 Camera / MeshRenderer 等对应原生组件接口修改运行属性，声明、绑定和类图同步；调试开关仅从 MCP 操作，MCP 只留在编辑器插件。

完整命令参数、撤销、身份及关闭语义见[统一 MCP 控制](../plugins/editor/render-feature-controls.md)。场景内容参数与渲染器策略的保存位置按各自所属模块处理，核心不读取 TOML。

## 实现关系

1. 基准、逐视图统计与自动剔除，先保留可比较的旧提交路径。
2. Frame/View 参数频率与静态实例化，验证主视图、反射、捕获隔离。
3. 离线减面和静态 LOD，复用实例批次键，保证各 pass 同层。
4. 限定主场景的线框/调试，供检查边界、层级和提交结果。
5. 大气天空，接现有环境/方向灯/HDR，并覆盖纯天空场景及平面反射。
6. Hex 标准 PBR 材质，校验颜色空间、显式梯度、法线旋转和采样成本。
7. 统一编辑器、TS、MCP 的完整往返与公开范例验证。

大气透视、动态 IBL、LOD 平滑过渡、蒙皮实例化和遮挡剔除留待后续明确需求；本次不会添加对应空开关。领域调试由对应系统提供实际几何，渲染不反向依赖物理、导航或编辑器插件。

## 正式资源与验收

新增 Mesh/Shader 数据只扩展现有 schema、编解码及标准导入指纹，保留原 GUID；不在运行时读取源 .meta/TOML，不引用本机 vcpkg build 路径。上游代码/Shader 使用固定版本并保留许可，私有项目和商业参考不进入 Git。

C++ 修改完成 Debug/Release 构建；仅 Release 运行 CTest、功能与真实 Metal 截图。基准只围绕本轮渲染链路，关闭开关必须真正恢复对照路径。画面一致性、MCP 状态读回、切场景、设备重建和释放同样属于验收；不将某项测试等同全部能力完成。
