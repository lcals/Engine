# 渲染模块

[bgfx](https://github.com/bkaradzic/bgfx) 提供跨平台渲染。图形后端按平台固定：macOS / iOS 使用 Metal，Windows 使用 DirectX 12，Android / Linux 使用 Vulkan。所选后端初始化失败时返回失败，不接受 Noop 或其他后端回退。当前最小实现可创建窗口、清屏和绘制调试文字。引擎使用 PBR 渲染；渲染效果优先参考并复用 [bgfx 官方示例](https://bkaradzic.github.io/bgfx/examples.html)中的做法。

引擎实现自己的材质系统，参考 Unity 和 Unreal Engine，并内置一套默认材质；bgfx 负责底层渲染。

光照首版覆盖环境光、主光源与阴影；局部光源按游戏需求扩展，全局光照后续考虑。效果与方案可参考 `references/` 目录。

以 Blender 作为当前 DCC 视觉制作模板。USD 导入后，在约定支持的材质与渲染效果范围内，追求与 Blender 预览一致；最终效果以引擎预览为准。

渲染流程采用渲染图组织，描述各渲染阶段的输入、输出与依赖；渲染图负责编排，实际渲染由 bgfx 执行。首版先覆盖 PBR 所需的固定流程，再按需求扩展。

后期效果与渲染优化优先复用 bgfx 的现有能力和官方示例方案，按项目需求接入渲染流程，避免重复实现。

渲染侧必须支持 GPU 蒙皮，动画模块见[动画模块](animation.md)。

渲染模块支持大规模单位同时呈现。GPU 蒙皮和 ECS 是基础，渲染侧还需支持大规模单位的提交与可见性管理；导航、物理和行为更新也要能支撑同一规模。

渲染支持材质、贴图与 Shader 热重载，主要用于编辑器 Play 模式下边运行边调整视觉效果，无需重启场景。资源变化、缓存更新与替换失败时的处理见[资源管线](../editor/asset-pipeline.md)。
