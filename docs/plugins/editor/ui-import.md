# UI 文档导入插件

`editor/ui_import` 把 `.rml` 导入为独立 UI 资源。运行时由普通场景 `Node` 挂载 `UiComponent`，组件引用这个资源；文档内部的控件不展开为场景节点。

源文件保留 RmlUi 标记，样式使用文档内 `<style>` 或元素 `style` 属性。相邻 `.rml.meta` 声明所需字体和纹理：

```toml
guid = "0ae227a1-f729-4b3a-90ec-7e4f31f65855"
importer = "ui"
meta_version = 1

[ui]
textures = []

[[ui.fonts]]
family = "body"
asset = "project/fonts/lato-regular.ttf"
shader = "builtin/shaders/font"

[[ui.fonts]]
family = "heading"
asset = "project/fonts/lato-bold.ttf"
shader = "project/shaders/title.shader"
```

必须包含 1–64 个 `[[ui.fonts]]` 绑定，`textures` 最多 256 项且可以为空。`family` 为 RmlUi 的 `font-family` 名称，ASCII 归一化为小写，在文档内唯一。`asset` 必需；`shader` 可省略，默认使用引擎字体 Shader，也可指定遵循字体顶点和曲线纹理契约的项目 Shader。两项资源引用接受完整逻辑路径或 GUID，导入时检查存在性和类型。

同一字体可以绑定多个族并分别使用不同 Shader，多个不同字体也可同时使用；资源依赖按 GUID 去重。重复族名、重复纹理、缺失或类型不符的依赖明确失败。族名长度限制 128 字节，不能含控制字符、CSS 列表或属性分隔符，也不能有首尾空格。

UI 生成物是 `cache/gen/<源GUID>`，包含文档标记、`UiFontBinding` 列表与纹理 GUID；每个绑定保留字体族、字体 GUID 和完整 Shader 路径。目录记录全部依赖，使用现有资源图加载、共享和释放；打包、持久资源同步沿用同一目录契约。

当前范围：

- 文档最多 1 MiB；属性值需要引号。
- 支持内嵌 RCSS 和 `<img src="project/...">`；图片只能使用 meta 已声明的纹理路径或 GUID。
- 外部 `<link>`、`<script>`、`<template>`、CSS `@import`、`@font-face`、`@spritesheet`、`url()`、`fill-image` 与图片装饰器暂不接入，导入时明确失败，避免未登记的运行时文件依赖。
- 当前不接入脚本数据模型、脚本事件绑定和场景跳转业务。UI 文档中的按钮与控件可以先完成布局、输入和状态展示，业务在后续脚本组件阶段接入。

本插件只处理编辑器导入，不依赖字体导入的具体实现，也不在编辑器基础库登记自己；可执行宿主根据 CMake 插件选择完成装配。

架构入口：[UI 导入类图](class-diagrams/ui-importer.md)。
