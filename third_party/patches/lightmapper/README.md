# lightmapper 的宿主捕获适配

上游：https://github.com/ands/lightmapper，固定提交
`4fd3bf4e2c07263f85d5d875ebdef061bc512dd4`。原文件保留 public domain 和永久复制修改许可声明。

`host-readback.patch` 保留上游保守 UV 栅格、分层插值、hemicube 相机、立体角权重和图像膨胀。
移除仅支持 OpenGL 的 framebuffer、同步读取和着色器代码；由引擎现有 bgfx 渲染捕获并异步读回一面，
通过 `lmSubmitPixels` 提交 top-left 行序的线性 RGBA。五面完成后 CPU 按相同权重积分，保存 irradiance，
因此使用 cosine 权重并将原版归一化半球平均乘 2π；接收面 albedo 不烘进图中。

RGBA alpha 表示背景或正面有效性；无效背面占比大于 10% 时保留空 texel，随后使用上游膨胀填充。
颜色为零的有效 texel 保留 FLT_MIN，沿用上游插值有效性约定。

上游检出不修改。CMake 按源和补丁哈希，将原版复制到 build 后应用补丁；未变化不重复复制或触碰文件。
运行包不链接本依赖；只在编辑器照明任务的一处源文件定义 LIGHTMAPPER_IMPLEMENTATION。
