# 游戏运行时 UI 模块

普通场景 `Node` 挂载 `UiComponent`，组件绑定一个独立 UI 资源。一个 UI 文档可包含多个控件；控件树由 [RmlUi](https://github.com/mikke89/RmlUi) 管理，不展开成场景节点，不给 Node 增加专用 UI 数据。场景依赖目录自动包含 UI 引用的字体、图片及项目字体 Shader。

`UiSystem` 由世界系统管理，借用窗口、场景与核心 `FontSystem`。RmlUi 负责文档布局和控件交互；字体测量、字距和 GPU 字形渲染共用[字体系统](font.md)。字体导入位于编辑器 `font_import` 插件，UI 文档导入位于 `ui_import` 插件；这些转换库不进入运行时 UI 模块。

UI 绘制使用独立、有序的 bgfx 视图，在场景后提交。普通控件和字体均使用预乘透明混合，字体提交保留正常 RmlUi 绘制顺序、变换和矩形裁剪。每个 UI 文档的 `[[ui.fonts]]` 分别绑定字体族别名、字体资源与 Shader，`textures` 声明多张图片；字体族在文档内隔离。同一字体可经不同别名使用不同 Shader。RmlUi 对外的字体纹理句柄关联字体、Shader 资源及字号；实际 GPU 曲线纹理由 FontSystem 共享，多个字号不生成多份字体资源。

窗口事件由宿主交给 UiSystem。控件事件包含场景、节点、文档及控件身份、事件类型与输入值快照，由调用方消费；UI 系统不决定项目场景。脚本插件自动派发 click/change/input，项目无需逐帧轮询。showcase 入口菜单按 MVC 组织：普通脚本 Model 保存选择，Controller 订阅按钮并更新 Model，Refresh 修改 View。当前菜单执行选择预览；真正的场景导航与通用脚本组件属于后续接入。

停止场景时先销毁文档和控件引用，再释放 UI 绘制数据与字体 GPU 缓存，最后关闭渲染设备。缺少字体或文档、无效资源和 GPU 分配失败通过返回状态报告。

类与公开方法见[场景 UI 类图](class-diagrams/ui-system.md)，导入边界见 [UI 导入](../plugins/editor/class-diagrams/ui-importer.md)。具体构建、实际运行结果与已知限制统一记录在根 README。

游戏 UI 与编辑器界面职责分开；编辑器使用 Dear ImGui，见[编辑器模块](../editor/editor.md)。

文档准备与绘制分离，玩法 OnInitialize 中即可查找控件。按节点独立同步，新增其他文档不会重建现有 View；控件用文档代次和内部弱引用校验，文档替换、节点或组件删除、控件子树移除后旧句柄失效。图片、字体和字体 Shader 保持所属文档的资源快照，内部缓存按文档资源代际隔离并在最后一个对应 View 关闭后释放；FontSystem 的 GPU 缓存仍按场景释放。完整脚本绑定及控制器公开方法见 [UI MVC 类图](../plugins/runtime/class-diagrams/ui-script-api.md)。
