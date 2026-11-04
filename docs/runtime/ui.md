# 游戏运行时 UI 模块

普通场景 `Node` 挂载 `UiComponent`，组件绑定一个独立 UI 资源。一个 UI 文档可包含多个控件；控件树由 [RmlUi](https://github.com/mikke89/RmlUi) 管理，不展开成场景节点，不给 Node 增加专用 UI 数据。场景依赖目录自动包含 UI 引用的字体、图片及项目字体 Shader。

`RuntimeHost` 将 `UiSystem` 注册为 `WorldSystem` 的子系统，由世界统一管理生命周期；`UiSystem` 借用窗口、世界与核心 `FontSystem`。RmlUi 负责文档布局和控件交互；字体测量、字距和 GPU 字形渲染共用[字体系统](font.md)。字体与 UI 文档分别由统一编辑器 `asset_import` 插件内的 FontImporter、UiImporter 转换；这些转换库不进入运行时 UI 模块。

UI 绘制使用独立、有序的 bgfx 视图，在场景后提交。普通控件和字体均使用预乘透明混合，字体提交保留正常 RmlUi 绘制顺序、变换和矩形裁剪。每个 UI 文档的 `[[ui.fonts]]` 分别绑定字体族别名、字体资源与 Shader，`textures` 声明多张图片；字体族在文档内隔离。同一字体可经不同别名使用不同 Shader。RmlUi 对外的字体纹理句柄关联字体、Shader 资源及字号；实际 GPU 曲线纹理由 FontSystem 共享，多个字号不生成多份字体资源。

窗口事件由宿主交给 UiSystem。控件事件包含场景、节点、文档及控件身份、事件类型与输入值快照，由调用方消费；UI 系统不决定项目场景。脚本插件自动派发 click/change/input，项目无需逐帧轮询。showcase 入口菜单按 MVC 组织：普通脚本 Model 保存选择，Controller 订阅按钮并更新 Model，Refresh 修改 View。项目 Controller 已通过 Gameplay 执行场景导航；通用节点脚本组件尚未接入。

普通场景切换只移除不再存在的 UI 文档，常驻文档和其有效控件引用继续保留。停止整个玩法 Run 或显式 `release_scene()` 时释放全部文档、控件引用及 UI 绘制数据，并释放对应字体 GPU 缓存；共享渲染设备保持到宿主关闭。缺少字体或文档、无效资源和 GPU 分配失败通过返回状态报告。

类与公开方法见[场景 UI 类图](class-diagrams/ui-system.md)，导入边界见 [UI 导入](../plugins/editor/class-diagrams/ui-importer.md)。构建见[构建指南](../building.md)，公开范例与验证记录见[测试与范例](../testing.md)。

游戏 UI 与编辑器界面职责分开；编辑器使用 Dear ImGui，见[编辑器模块](../editor/editor.md)。

文档准备与绘制分离，玩法 OnInitialize 中即可查找控件。按节点独立同步，新增其他文档不会重建现有 View；控件用文档代次和内部弱引用校验，文档替换、节点或组件删除、控件子树移除后旧句柄失效。图片、字体和字体 Shader 保持所属文档的资源快照，内部缓存按文档资源代际隔离并在最后一个对应 View 关闭后释放；FontSystem 在 tick 中按资源弱引用逐项回收不再使用的 GPU 缓存；普通场景切换保留仍被常驻文档引用的字体，整 Run 或设备清理时才执行全量释放。完整脚本绑定及控制器公开方法见 [UI MVC 类图](../plugins/runtime/class-diagrams/ui-script-api.md)。

## MVC 与支持范围

UI 脚本使用 MVC：项目普通对象作为 Model，`UiComponent` / `UiElement` 提供 View，派生 `UiController<T>` 处理事件和显式刷新。`Bind()` 执行 `OnBind → OnRefresh`，`Listen()` 自动登记订阅，`Unbind()` 释放订阅；文档、控件或脚本上下文销毁后旧引用失效。支持 click/change/input 及输入值快照，事件按目标到祖先冒泡，`target` 保留目标，`currentTarget` 表示当前订阅控件，业务无需逐帧轮询。控件支持纯文本、表单值、显隐、禁用、样式、类名及已登记纹理切换。API 与职责见 [MVC 类图](../plugins/runtime/class-diagrams/ui-script-api.md)，示例拆分在 `assets/scripts/models/` 与 `controllers/`。

当前 UI 支持内嵌 RCSS、基础控件几何、显式图片引用、矩形裁剪、变换、鼠标、滚轮、键盘和文本输入。字体支持导入字符范围内的 UTF-8、换行、legacy kerning 和缺字回退；尚未接入复杂文字整形、自动字体回退、RmlUi 高级 mask/filter/layer，以及节点脚本组件。项目已通过 Gameplay 和 UI MVC 实现场景切换。外部样式、模板、脚本和未登记图片引用在导入时拒绝，避免运行包遗漏依赖。

## 常驻文档

常驻 UI 根通过 `Node.SetPersistent(true)` 连同子树保留，切换玩法场景复用原文档与 Controller；停止和热重载完整清理。详细约定见[常驻节点](class-diagrams/persistent-nodes.md)，操作范例见[测试与范例](../testing.md)。
