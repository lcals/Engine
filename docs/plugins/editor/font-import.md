# 字体导入插件

`editor/font_import` 使用 FreeType 在编辑器导入字体。运行时核心消费独立 `FontData`，不解析源字体；UI 与三维文字共享字体资源，同一份轮廓支持不同字号和颜色。

源文件旁保存 `.meta`：

```toml
guid = "d5dc034b-395d-4c74-b514-d2d4f116b09b"
importer = "font"
meta_version = 1

[font]
face_index = 0
unicode_ranges = ["0020-007E", "00A0-00FF"]
kerning = true
```

- `face_index` 是字体集合中的索引，接受 0–255 的整数；普通 TTF 使用 0。
- `unicode_ranges` 使用 4–6 位十六进制 Unicode 标量范围。范围不能重叠、包含代理项或零码点；零码点保留给缺字字形。最多请求 16384 个码点，最多生成 4096 个字形；每字形最多 4096 条曲线。字体不包含的请求码点不复制缺字轮廓，运行时统一回退零号字形；整个范围无任何字符时导入失败。
- `kerning` 控制导入 FreeType 提供的原生 `kern` 字距，不代表完整 OpenType 整形。复杂文字、连字、GPOS 整形不属于该开关的实现范围。
- 参数类型错误、未知字段、无效字体、越界范围均返回失败。资源管线只在整批转换完成后发布；失败不替换已发布缓存。

导入器支持 `.ttf`、`.ttc`、`.otf` 中的 Unicode TrueType 可缩放二次轮廓。CFF 三次轮廓、位图字体与可变字体轴选择尚不支持；三次轮廓明确失败，不采用未经误差控制的近似。

生成物为 `cache/gen/<源GUID>`，FlatBuffers 标识 `EFNT`。所有坐标与度量归一化到 em。字形按码点排序，轮廓保持原方向，曲线数组紧凑连续；直线作为控制点在中点的二次曲线。只保存非零字距对。源字体、meta、导入器版本、FreeType 版本及 schema 共同参与增量判断。

GPU 字形数据和文字排布由核心字体系统按需创建。字体资源不固定字号、不保存 UI 控件、不依赖某个 Shader。字体默认 Shader 属于引擎内置定义，UI 与三维文字使用者可以指定遵循相同契约的项目 Shader。

示例字体为 Lato Regular 2.015，原始来源、固定提交、SHA-256 与 OFL 许可保存在 `projects/showcase/assets/fonts/`。公开 FreeType API 用于解析；本实现不包含 C4 授权源码或移植实现。

架构入口：[字体导入类图](class-diagrams/font-importer.md)。
