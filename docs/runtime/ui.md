# 游戏运行时 UI 模块

普通场景 `Node` 挂载 `UiComponent`，组件绑定一个独立 UI 资源。一个 UI 文档可包含多个控件；控件树由 [RmlUi](https://github.com/mikke89/RmlUi) 管理，不展开成场景节点，不给 Node 增加专用 UI 数据。场景依赖目录自动包含 UI 引用的字体、图片及项目字体 Shader。

`UiSystem` 由世界系统管理，借用窗口、场景与核心 `FontSystem`。RmlUi 负责文档布局和控件交互；字体测量、字距和 GPU 字形渲染共用[字体系统](font.md)。字体导入位于编辑器 `font_import` 插件，UI 文档导入位于 `ui_import` 插件；这些转换库不进入运行时 UI 模块。

UI 绘制使用独立、有序的 bgfx 视图，在场景后提交。普通控件和字体均使用预乘透明混合，字体提交保留正常 RmlUi 绘制顺序、变换和矩形裁剪。每个 UI 文档的 `[[ui.fonts]]` 分别绑定字体族别名、字体资源与 Shader，`textures` 声明多张图片；字体族在文档内隔离。同一字体可经不同别名使用不同 Shader。RmlUi 对外的字体纹理句柄关联字体、Shader 资源及字号；实际 GPU 曲线纹理由 FontSystem 共享，多个字号不生成多份字体资源。

窗口事件由宿主交给 UiSystem。控件事件包含所属节点身份、控件标识和事件类型，由调用方消费；UI 系统不决定项目场景。showcase 的入口菜单用于展示已有场景入口，本阶段保留事件出口，项目的真正场景跳转和脚本组件绑定后续接入。

停止场景时先销毁文档和控件引用，再释放 UI 绘制数据与字体 GPU 缓存，最后关闭渲染设备。缺少字体或文档、无效资源和 GPU 分配失败通过返回状态报告。

类与公开方法见[场景 UI 类图](class-diagrams/ui-system.md)，导入边界见 [UI 导入](../plugins/editor/class-diagrams/ui-importer.md)。具体构建、实际运行结果与已知限制统一记录在根 README。

游戏 UI 与编辑器界面职责分开；编辑器使用 Dear ImGui，见[编辑器模块](../editor/editor.md)。
