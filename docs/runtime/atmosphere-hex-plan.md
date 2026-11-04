# 大气天空与 Hex-Tiling 接入计划

[类图与参数](class-diagrams/atmosphere-hex-material.md) ·
[环境组件](class-diagrams/environment-components.md) · [渲染 MCP](../plugins/editor/class-diagrams/render-mcp-tools.md)

**本页接口已评审，记录源码接入边界和验证范围；尚未完成的实际验收不得报告为通过。** 使用仓库已固定的 bgfx API 167
发布源码，先评审归属、所有权和公开数据契约，再实现。仅接入当前功能直接需要的渲染链路。

## 已有基础与缺口

| 位置 | 可复用内容 | 需要的增量 |
| --- | --- | --- |
| `include/schemas/runtime/scene.fbs` 的 `EnvironmentData` | 环境/雾/IBL 的唯一持久数据 | 增加大气参数与方向太阳的场景局部引用 |
| `EnvironmentComponent`、`EnvironmentCodec`、`EnvironmentComponentBinding` | 原子设置、ECS、TOML 与 TS 通路 | 生成字段、校验和原路径编解码同步 |
| `render_settings.cpp` / `scene_render_settings.hpp` | 当前场景启用环境解析 | 将大气从当前环境解析为帧/视图输入，不创建独立全局状态 |
| `scene_gpu_resources.hpp` / `scene_gpu_resources.cpp` | 场景 GPU 所有权、资源释放和设备重建 | 大气程序及 LUT；实现可分到独立大气源文件 |
| `render_scene.cpp` | 主视图、两路平面反射和有序 HDR 管线 | 天空可绘制性、每视图 Sky-view、背景提交及正确通道顺序 |
| `scene_render_query.cpp::drawable_scene` | 当前 Mesh/Text/扩展检测 | 纯天空+相机场景也必须可绘制，不能要求另放占位网格 |
| `scene_capture.cpp::draw_capture_sky` | 烘焙用线性背景、独立矩阵与 HDR 回读 | 当大气启用时采样该捕获视图的大气天空；保持捕获快照及取消语义 |
| `scene_draw.cpp::prepare_lighting`、`ShadowPass` | 方向灯方向、PBR 灯数据、级联阴影 | 天空读取同一太阳，复用既有阴影而不搬示例的单独阴影 |
| `pbr_surface.sh` / `fs_surface.sc` | BaseColor/Normal/ORM 与 MRT 共用采样 | Hex 统一 UV/显式梯度/权重，保证主颜色与表面缓冲一致 |
| `builtin_resources.cpp` / `GpuProgram` | 具名参数、sampler、材质值校验、Shader 反射 | Hex 的参数声明与单个额外 vec4 uniform，仍用现有纹理槽 |
| 编辑器 RenderCommands / SceneCommands、MCP 工具登记 | 撤销/重做、批量命令、材质参数编辑与状态查询 | 原入口暴露参数，统一功能入口转发并携带 owner 身份 |

上游本地源码已核对 `53-sky2/sky2.cpp`、`atmosphere.sh`、三个 Compute Shader、背景 Shader、
`49-hextile/fs_hextile.sc`，以及公开 MIT 原版的法线处理文件。不会从私有/付费参考迁移实现。

## 大气最小接入

1. 采用上游 Transmittance `256×64`、Multiscatter `32×32` 和 Sky-view `192×108`，均为线性
   `RGBA16F`，保留上游积分、相函数和 LUT 映射。Compute 后按 bgfx 的独立 View 顺序消费，
   具体 ViewId 统一在 `render_constants.hpp` 登记，不把示例的 0..5 原样搬入。
2. `render_constants.hpp` 的现有反射组从 1/40 开始、主阴影从 80 开始，UI 使用 200，烘焙
   使用 240..243；实现时统一重新分配保留空间。需要三个 LUT 作业排在使用它们的所有镜像、
   主视图和捕获之前，不能只将作业号放到主视图前面。若顺移反射 View 范围，须与所有模块一起核对。
3. 场景米单位转上游 km：`altitude_km=(world_y-ground_height)*0.001`。明确近地面适用范围，
   地下高度按上游近地面保护处理；不宣称支持轨道/太空相机或任意星球中心的全局场景。
4. 天空全屏绘制必须写入原线性 HDR 颜色目标，并将 MRT 的环境高光替换量置零，不写几何
   Surface/深度/法线。背景几何置于该后端的远深度并启用对应深度比较、禁止深度写入，确保
   bgfx 排序不会使天空覆盖已画物体；若使用独立 View，则清理只能发生一次且须在背景之前。
   保留实际相机逆矩阵、平台深度约定和纹理原点；镜像观察使用自身矩阵。
5. **不能原样输出上游 `sky2_tonemap()`**：其背景 Shader 末尾已经做了色调映射，本引擎有统一的
   `HdrPostProcessPass`。适配后背景输出线性散射/日盘值，由现有曝光、Bloom 和显示 resolve
   处理一次，HDR 捕获仍读取曝光/显示转换前的数据。不能为匹配截图叠加 gamma 或强度补偿。
6. 初版包含实时天空和太阳方向/颜色联动；天空变化本身不等于物体已得到实时天空间接照明。
   不搬示例的近似环境项或地形 Shader；空气透视、逐表面太阳透过率及动态 IBL 后续分别实现。
   已有静态 IBL、Lightmap、SH 探针和雾继续各自配置，避免暗中替换光照来源。
7. 反射应能看到天空，离线烘焙捕获应能看到与捕获相机匹配的天空；复用现有捕获/烘焙作业，
   不建另一个实时环境卷积框架。改变太阳或大气设置必须使正在进行且依赖这些输入的捕获
   按现有快照检查取消，不能发布混合参数的烘焙结果。
8. 关闭/切场景/设备重建均有确定的释放路径；缺少 Compute 或纹理能力不回退 Noop，不自动
   关闭已保存开关。报告当前请求与未生效原因，保留场景可操作性。

## Hex-Tiling 最小接入

1. 复用 bgfx `49-hextile` 的三角网格与随机旋转，参考 `53-sky2/hextile.sh` 的公共函数拆分。
   `53` 的地形调用者也用了 `texture2DGrad`；不能因 helper 没有 sampler 就认为它不处理梯度。
   `49` 和 `53` 都不是本引擎完整 Metallic/Roughness PBR 多贴图接入。
2. 仅在当前内置不透明 PBR 开启具名参数；关闭时保留原普通采样及现有校准结果。使用既有
   UV0/`uvTransform` 和 five-sampler 契约，不更换资源导入颜色空间，也不添加屏幕空间坐标纹理。
3. 每像素只算一次网格、三组 UV/旋转和一致混合权重，再取三份 BaseColor、Normal、ORM。
   所有采样传随机偏移之前的显式梯度并按旋转变换，以避免 tile 边缘的错误 mip/各向异性。
4. 法线以 mmikk 固定 MIT 原版的表面梯度方法为依据，正确旋转、混合后转回本引擎 TBN。
   上游的 DirectX 纹理轴约定不能直接套到本引擎现有完整 XYZ 法线约定；用方向性测试贴图确认
   旋转、UV 翻转和负缩放。统一权重是使材质贴图对齐的明确适配，不重新发明噪声或混合算法。
5. ORM 的 R/G/B 对应 occlusion/roughness/metallic，线性混合后继续原 PBR 范围约束；不对 ORM
   用颜色 gamma、法线归一化或为金属度构造另一份随机偏移。Emission/Specular 如有绑定也随
   同一贴图图案采样，Lightmap UV2 保持独立。
6. 通过公共 PBR surface 采样同时覆盖主场景和 Surface/MRT；初版不支持 alpha-cutout、透明
   水、三平面或 SpecGloss，组件和材质共同组合校验可通过原编辑/脚本/MCP 完整失败返回。
7. 预期核心三张纹理从 3 次变成 9 次采样；若同时使用 Specular/Emission，可达 15 次。首版
   保留显式 opt-in，不以关闭法线、降采样或改变 PBR 方程掩盖额外成本。只记录此能力开关的
   CPU/GPU 影响，不扩张成其他模块的性能优化。

## 实施后的验证范围

- Debug 只编译；Release 构建、测试和实际渲染。Metal 为本轮实际后端，其他平台编译/运行结果
  分别记录，不把 Shader 编译成功等同于真机通过。
- 大气：空场景仅相机+环境+太阳；正午、低太阳、地平线下；灯旋转和现有阴影同步；两路
  平面反射、HDR 捕获及烘焙捕获；关闭/重开/切场景和设备恢复后无旧纹理/参数残留。
- HDR：固定相机/分辨率，曝光翻倍的线性 HDR 回读关系、显示只一次 tone map、天空 Bloom
  与场景一致；验证日盘高亮仍在线性 HDR 中，MRT 的非天空表面数据不被污染。
- Hex：规则平面和方向性 Normal/ORM 棋盘，BaseColor 单独及三图组合；tile 边缘与斜视 mip；
  平法线/黑色纹理/负 UV 缩放；关闭后与已有基线一致；Surface 法线/粗糙度和主颜色匹配。
- 编辑与 MCP：参数边界、目标身份、缺少方向灯、不支持的材质组合、撤销/重做、保存重载，
  统一功能入口与直接领域入口读到同一真实值，无两套开关互相覆盖。
- 使用已有截图/线性 HDR 捕获和 Release 验证脚本；图像是实际引擎结果，不用离线合成代替。
  对比固定条件的上游同算法输出，并单独列出已声明的 HDR、单位与 PBR 多贴图适配差异。

接入完成不等于画面已验收；实际构建、运行和图像验证结果统一补到 `docs/testing.md`。

## 实际回归记录

本轮 macOS / Metal / Release 的天空、烘焙捕获、平面反射、Hex 方向法线与开关回归已完成。
操作命令、定量结果、日志路径、平台和首期限制统一维护在
[测试文档](../testing.md)的“大气天空与 Hex-Tiling 标准 PBR 接入（2026-11-04）”一节，
不在模块计划重复统计结果。
