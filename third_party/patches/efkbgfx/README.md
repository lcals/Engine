# efkbgfx 适配

上游：<https://github.com/cloudwu/efkbgfx>，固定提交
`7d89c3c31a8d8d544217b431f9400b2b50003321`，MIT。
配合 Effekseer 官方 `1808`，提交 `b5700082b49bc5f919610880dafddc90211a9a4e`。
bgfx 使用 `1.167.9605-606`（API 167），提交
`b2513038f4b753ebeced40821c193978daa0492f`。

`effekseer-1808.patch` 在 CMake 构建目录的副本上应用，保持 Git 子项目原样：

- 跟随官方 `RenderStateBase` 成员重命名 `next_` / `active_`。
- 跟随 bgfx API 167 的 C99 `create_texture_2d` 契约，为适配器创建的代理纹理
  显式传入 `external = 0`，由 bgfx 创建并管理原生纹理。
- 每次绘制前将宿主提供的深度纹理及重建参数提交给官方 `Renderer::Impl`；
  `RendererCommon` 通过此实现读取深度，不经过旧适配器的虚函数覆写。
- 深度与背景附件是宿主借用资源，适配器释放时不销毁这些 GPU 纹理；
  无效附件视为缺失，避免将无效句柄送入 bgfx。
- 反向深度配置同步到官方渲染状态。
- 贴图回调通过标准库转换 UTF-16 路径，保留非 BMP 字符并避免固定缓冲区截断。
  调用方先验证 UTF-16 有效性及官方运行时路径长度限制。
- 模型顶点布局按官方 `GetModelRendererVertexLayout` 保留 `UV2`，与 1808 的
  `Model::Vertex` 对齐为 68 字节；颜色位于偏移 64。编译期检查各字段偏移和结构大小，
  避免更新 SDK 后静默使用旧版 60 字节布局。
- 模型常量缓冲容量与上游六个模型 Shader 数组以及官方 `EffekseerRendererLLGI`
  保持一致，均为 40 个实例；避免反射要求上传 40 项时读取旧版 20 项数组之外的数据。
- Lit / Unlit 最终输出通过 `u_effects_output.x` 区分线性 HDR 与 gamma SDR；
  `SetOutputGamma` 逐次绘制设置该 uniform，作者参数与上游颜色计算保持原样。
- 场景线性目标的 alpha 保存曝光权重，特效沿用原非 HDR 的不曝光贡献。
  保持所有 RGB 混合：Opacity / Wireframe 的 alpha 以 ADD 和 ZERO/ZERO 清零；
  Blend 使用 ZERO/INV_SRC_ALPHA 衰减已有权重；Add、Sub、Mul 使用 ZERO/ONE
  保留目标权重，避免上游 ONE/ONE 累加污染权重。HDR 开启时最终映射忽略该权重。

预定义 Sprite、Ribbon、Ring、Track 和 Model 渲染复用上游实现。
Shader 使用子项目 `shaders/` 已生成的 bgfx 源码，由项目现有 shaderc 编译为
目标平台代码：macOS / iOS 为 Metal，Windows 为 DirectX 12 使用的 shader profile，
Android / Linux 为 SPIR-V。生成头仅在插件实现内部使用。

当前不提供自定义 `.efkmat` 的编译工具，也不装配官方 1.8 GPU 粒子后端；
依赖导入应拒绝未支持的功能，不能静默替换为普通材质。
