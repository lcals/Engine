# 字体运行时

字体是独立运行资源。编辑器 `font_import` 插件把 TTF 轮廓与度量转换为 FlatBuffers；运行时只加载生成资源，不解析源字体、不依赖 FreeType。

`FontSystem` 提供共享字体加载、码点查询、字距、测量、文字几何生成和 GPU 绘制。UI 的 RmlUi 字体接口与普通节点上的 `TextComponent` 使用同一字体数据和渲染路径。字体大小与颜色属于使用者，同一字体资源不按字号复制。

GPU 后端采用公开 bgfx `51-gpufont` / Slug 的二次 Bézier 曲线覆盖率计算，不使用固定分辨率字形图片或 SDF。所有曲线紧凑存放在共享 `RGBA32F` 纹理，每条曲线占两个 texel；没有每个字形独占一整纹理行的固定开销。纹理按实际曲线数量创建，并验证平台格式与尺寸支持。当前遍历单个字形的曲线，不为尚无压力证据的场景增加分带索引模型。

Shader 默认路径为 `builtin/shaders/font`。项目可提供自己的 Shader 资源，但须保留字体顶点和 Uniform 契约：

- Position：三个 float，节点或 UI 的局部位置。
- Color0：归一化 RGBA8，预乘透明度。
- TexCoord0：两个 float，em 单位字形坐标。
- TexCoord1：两个 float，曲线起始索引和数量。
- TexCoord2：四个 float，向外扩张方向和 XY 字号的倒数；顶点 Shader 按透视与视口扩张像素覆盖边界。
- `s_font_curves`：stage 0，最近点采样的 RGBA32F 曲线纹理。
- `u_font_texture`：vec4，纹理宽、高、0、0；连续两 texel 分别存 `(x0,y0,x1,y1)` 与 `(x2,y2,0,0)`。

创建程序时反射校验上述两项自有 Uniform 的名称、类型和数量；误用其他绘制路径的 Shader 会返回失败。bgfx 提供的视图和模型矩阵仍按标准约定使用。当前字体 Shader 可自定义覆盖率与着色源码，不提供额外材质 Uniform 参数绑定。

路径选择保存在 UI 字体绑定或 Text 组件的持久数据中。使用者加载并持有字体和 Shader，单次 `FontDraw` 直接传入已持有的 Shader 资源；绘制阶段不按路径重新查询目录，已存在文档和组件保持资源代际一致。

UI 在 RmlUi 正常渲染阶段提交字体几何，沿用视图顺序、模型矩阵、scissor 与预乘透明混合。3D 文字沿用场景相机和节点 Transform，测试深度但不写透明四边形深度。场景停止由宿主先停止 UI/场景使用者，再释放字体 GPU 缓存，最后关闭渲染设备。

首版支持导入字符集内的 UTF-8 码点、换行、英文 kerning 和字间距；缺字使用字体 notdef。复杂文字整形、跨字体自动回退和竖排尚未实现，不将 GPU 曲线渲染等同于完整文字排版引擎。

算法来源为公开 MIT 授权的 [bgfx 示例](https://github.com/bkaradzic/bgfx/tree/a7016487e5970c5299bb837f1af42dcb24909a67/examples/51-gpufont) 与 [Slug](https://github.com/EricLengyel/Slug)。完整 MIT 版权和许可保留在 `fs_font.sc`；实现没有引用或复制受限 C4 源文件。
