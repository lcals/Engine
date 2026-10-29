# 游戏运行时 UI 模块

`UiSystem` 接入 [RmlUi](https://github.com/mikke89/RmlUi)，初始化时调用库接口验证依赖链接，由世界系统管理生命周期。RmlUi 渲染接口、文档加载和玩家 UI 上下文尚未实现。

游戏 UI 与编辑器界面职责分开；编辑器使用 Dear ImGui，见[编辑器模块](../editor/editor.md)。
