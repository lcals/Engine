# 法线对照方法与适用边界

本页说明 PBR 法线对照的输入约束和诊断方法。已记录的图像结果见[对照结果](current-results.md)，旧中间截图和数值表由 Git 保存。

## 输入与语义

- 两侧使用相同位置、法线、索引、UV 和纹理；Blender 原生球与引擎导入网格不能直接视为相同切线空间。
- glTF `normalTexture.scale` 缩放解码后的 XY，再归一化；Blender 原生 Normal Map Strength 的定义不同，参考应显式使用同一规则。
- 基础色按 sRGB，法线与 ORM 按线性数据。正式导入的压缩纹理需要单独核对解码值；CPU 转码相同不等于 GPU 采样、插值或切线相同。
- 参考必须对齐观察矩阵、坐标/手性、灯光条件和显示转换；先检查这些输入，再分析 BRDF 差异。

## 诊断步骤

1. 比较同源网格的切线和手性，覆盖 UV 接缝、镜像 UV、退化三角形及非均匀/负缩放；普通球体上的一致不能外推到所有网格。
2. 隔离材质和灯光，输出世界法线检查实际 GPU 数据；CPU 复现、8 位编码图和生产 GPU buffer 是不同证据。
3. 按几何法线与光线夹角观察残差，区分背光限制、终止线校正和纹理/切线错误。关闭参考校正只用于定位，不替代正式对照。
4. 使用线性 HDR 检查直接光，再独立检查 IBL；显示编码 MAE 不能单独证明 BRDF 正确。

工具位于 `projects/showcase/tools/pbr-comparison/`，包括 `compare_tangent_frames.py`、`diagnose_bump_correction.py` 和 `analyze_normal_residual.py`；输入与生成参考的方法见[工具说明](tools.md)。这些诊断按具体问题选用，不作为每次渲染修改的必跑矩阵。

## 终止线处理与来源

引擎采用固定版 Ray Tracing Gems 第 12 章 MIT 配套实现，仅对直接漫反射及对应烘焙捕获应用遮蔽近似，保留上游系数；镜面、金属高光和预卷积 IBL 不乘此项。来源、版本和完整许可见[第三方声明](../../../../third_party/notices/ray-tracing-gems-terminator.md)。

该处理不是 glTF 强制的唯一算法。Blender 凹凸校正、几何终止线偏移和引擎的近似具有各自适用范围；不通过调法线强度、曝光或关闭参考校正掩盖差异。
