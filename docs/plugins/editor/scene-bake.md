# 场景烘焙插件

[IBL 类图](class-diagrams/scene-bake.md) · [Lightmap 与探针类图](class-diagrams/scene-lighting-bake.md) · [运行时渲染](../../runtime/rendering.md) · [验证记录](../../testing.md)

`editor/scene_bake` 负责编辑器中的离线场景烘焙。调用方保存场景、选择输入并通过 MCP 发起作业；插件复用现有任务域、GPU 捕获和资源管线，运行包只加载发布结果。固定上游版本、许可和适配边界集中记录在[渲染上游来源](../../runtime/rendering-upstream-plan.md#固定来源与许可)。

## 场景 GPU IBL 烘焙

`editor/scene_bake` 插件通过 GPU 计算环境贴图的漫反射卷积、粗糙度预过滤及 BRDF LUT，参考 Ant 的 IBL Shader 和 bgfx `18-ibl` 的消费契约，来源见 [Ant 场景烘焙许可](../../../third_party/notices/ant-scene-bake.md)。运行时只读取生成的纹理；烘焙逻辑不进入运行包。

先打开并保存目标场景预览，确保没有未保存修改或待刷新版本；输入环境纹理必须已导入，有项目源文件及邻接 `.meta`。二维输入按经纬全景解释，也可使用 cube 输入。HDR 输入目前仅支持线性半浮点 KTX1，不直接导入 `.hdr` / `.exr`；普通 PNG/JPG 的 LDR/BasisU 纹理也可作为输入。烘焙要求 GPU 支持计算、半浮点图像写入和纹理读回，同一时间只接受一个作业。

全景输入先转换为线性立方图基础层，再根据立方图像素的立体角逐层滤波生成源 mip；缺少完整 mip 的立方图输入也从基础层重建。非 2 的幂尺寸在相邻层之间按像素覆盖区域的立体角加权。全景转换参考固定版本 [Filament 的自适应输出足迹采样](../../../third_party/notices/filament-panorama.md)，处理接缝并对样本按立体角加权，每像素至多 1024 次采样。它是有限采样的近似，不保证所有微小高亮均被完整保留。输入已提供完整立方图 mip 时直接使用其 mip，输入方负责保证滤波质量。

预过滤使用 [GPU Gems 3 的 mip 过滤重要性采样](https://developer.nvidia.com/gpugems/gpugems3/part-iii-rendering/chapter-20-gpu-based-importance-sampling)：样本立体角 `1/(样本数×方向 PDF)`，立方图像素平均立体角 `4π/(6×每面宽度²)`，由二者比值选择源 mip。立方图各位置的实际像素立体角并不完全相等，这是平均值近似；有限采样还可能引入模糊或噪声，精度受输入分辨率和采样数影响。此前沿用的 Khronos 公式缺少 `4π`，选取的 mip 约高 1.83 级；[相关上游修订](https://github.com/KhronosGroup/glTF-IBL-Sampler/pull/25)尚未合入。具体实测及限制见[验证记录](../../testing.md)。

已生成的 IBL 纹理不会随烘焙算法更新而改变；需要对目标场景重新执行 IBL 烘焙并发布，才能使用新结果。

| MCP 工具 | 参数与作用 |
| --- | --- |
| `bake_scene` | 必填 `scene`、`environment` 项目资源路径和 `effects: ["ibl"]`；返回作业 `job` 与当前状态 |
| `get_scene_bake` | 可选 `job`；查询状态、`completed` / `total` 进度及完成后的 `outputs` 路径/GUID |
| `cancel_scene_bake` | 必填 `job`；停止提交新任务，待在途 GPU 读回结束后取消，不发布部分资源 |

例如调用 `bake_scene` 的参数：

```json
{"scene":"scenes/example.scene","environment":"textures/sky.ktx","effects":["ibl"],"resolution":128,"samples":256,"lutSize":256}
```

三个可选参数的默认值如上；`resolution` 为 16..512、`lutSize` 为 32..512，均须为 2 的幂；`samples` 为 16..1024。该例使用项目自己的场景与天空，不代表仓库内置这些资源。

请求接受后按帧推进，返回 `running` 不表示已完成。使用 `get_scene_bake {"job":"返回的GUID"}` 查询；只有 `state = "complete"` 才表示 GPU 计算、资源发布、场景引用更新和预览刷新都完成。其余状态为 `idle`、`running`、`cancelling`、`publishing`、`cancelled`、`failed`；失败原因在 `error`。命令通道本身若返回 `activation: pending`，仍按原请求查询 `get_editor_receipt`，不要重复创建烘焙作业。

场景 `assets/scenes/example.scene` 的结果放在同名目录 `assets/scenes/example/`：

- `ibl-irradiance.ktx`：RGBA16F 漫反射 cube。
- `ibl-prefilter.ktx`：RGBA16F 粗糙度预过滤 cube，含完整 mip 链。
- `ibl-lut.ktx`：RG16F 二维 BRDF LUT。

每张图都有独立 `.meta` 和 GUID，重复烘焙保留已有身份；缓存仍为无后缀 `cache/gen/<完整GUID>`，场景通过三项 GUID 建立正常依赖并参与打包。作业暂存于 `cache/bake/<job>`。发布前检查场景、输入及目标文件是否变化，冲突时报错；不覆盖并行编辑。

后续优化计划：将与场景无关的 BRDF LUT 改为引擎内置、全局共享的一份资源，避免按场景重复生成；当前保持三份贴图的实现。见[场景烘焙设计](class-diagrams/scene-bake.md#后续优化全局共享-brdf-lut)。

完成后保留原 `ibl_enabled` 和 `ibl_intensity`，更新场景引用并立即刷新预览。原开关已开启时直接显示新结果；原来关闭时仍关闭，可用 `set_render_settings {"iblEnabled":true}` 开启并保存场景。`effects:["ibl"]` 只处理输入环境纹理；场景几何 Lightmap 与探针使用独立的 `effects:["lighting"]` 作业，见[Lightmap 与光照探针](#场景-lightmap-与光照探针烘焙)。

GPU 计算和回读按帧推进；发布后的资源导入与预览重建沿用同步流程，大场景刷新期间可能暂停响应。

## 场景 Lightmap 与光照探针烘焙

`effects:["lighting"]` 使用固定版本的 xatlas 生成独立 UV2，并用 ands/lightmapper 的半立方体采样、插值与膨胀；图形捕获改接引擎 bgfx 的逐帧异步读回。首次拉取依赖执行 `git submodule update --init third_party/xatlas third_party/lightmapper`。上游检出保持原样，兼容补丁在构建副本上应用。

保存并打开目标场景预览后，在固定建筑的 `MeshRenderer` 设置 `bake_static=true`，为探针创建普通节点并挂 `LightProbeVolumeComponent`。参与烘焙的表面使用不透明 `builtin://shaders/pbr`；蒙皮、可破坏组件、地形、角色和非静态刚体及其后代不能作为固定贡献者。未标记的物体继续按实时光照渲染。以下节点编号指已保存 `.scene` 内的节点 `id`；插件在启动时映射到当前运行节点身份。

```json
{
  "scene": "scenes/example.scene",
  "effects": ["lighting"],
  "resolution": 64,
  "hemisphereResolution": 16,
  "bounces": 2,
  "interpolationPasses": 2,
  "interpolationThreshold": 0.01,
  "lightNodeIds": [3],
  "probeVolumes": [
    {"nodeId": 12, "boundsMin": [-5, 0, -5], "boundsMax": [5, 6, 5], "grid": [3, 3, 3]}
  ]
}
```

示例路径和节点需替换为当前项目真实对象。`lightNodeIds` 显式指定参与烘焙的灯，空列表只采天空与自发光，不自动选择运行时灯；不传 `probeVolumes` 时只生成固定网格 Lightmap。`resolution` 为每实例照明图边长 16..512、`hemisphereResolution` 为每次捕获基准边长 16..128，均为 2 的幂。`bounces` 为 1..8，插值次数 0..4、阈值 0..1；默认值如例。每个探针体积的局部网格各维 2..64，总点数不超过 32768，最多 64 个体积。不同室内空间分设体积，减少跨墙插值。

继续使用 `get_scene_bake` 查询真实完成的网格轮次／探针数，以及 `cancel_scene_bake` 取消。UV2 在现有编辑器任务域准备；每一反弹完成所有网格后才交换前后照明图，避免按网格顺序重复反馈。场景切换、未保存编辑或导入版本变化使任务失效；取消等待在途读回结束，部分结果不发布。

完成后在场景同名目录生成 `lighting-<node-id>.mesh`、`lighting-<node-id>.ktx` 和 `probes-<node-id>.lightprobe`，每项具有独立邻接 meta 和 GUID。固定网格实例绑定生成的 UV2 网格、精确编译内容哈希和烘焙世界变换；Lightmap 为不含接收面 albedo 的线性 RGBA16F irradiance，运行时乘 albedo/π。探针保存世界轴 RGB radiance SH9，在运行时按 l=0/1/2 做一次 π、2π/3、π/4 余弦卷积。图、网格、探针和场景引用在同一发布事务中更新；导入失败恢复旧文件并刷新旧目录。原始美术源文件保持独立。

重烘焙保留已有输出 GUID。移动网格或探针体积、替换网格后旧绑定失效；修改材质、固定照明或建筑结构后应重新烘焙。静态 GI 不表达实时破坏后的新反弹，动态角色在包围盒中心采样一个 SH9，体积外及无效探针回退环境照明。

实际构建与 GPU 验证、已知问题及可复用验证脚本命令统一见[验证记录](../../testing.md)，不能从共享实现推断未经实测的平台已经通过。
