# 特效插件

`runtime/effects` 提供独立 `EffectSystem` 和场景节点上的 `EffectComponent`。系统管理模拟时间、共享特效资源与每个实例的播放句柄，组件保存资源、速度、循环和自动播放设置。核心 `RenderSystem` 只提供通用 `RenderExtension` 绘制和释放接口；特效在场景几何之后、HDR/Bloom 之前绘制，使用当前场景镜头及深度附件。Effekseer、efkbgfx 类型不进入核心或插件公开头。

`editor/effects` 提供 `.efkefc` 导入、组件 TOML 编解码、可撤销属性编辑和六个 MCP 工具：`set_effect`、`get_effect`、`play_effect`、`pause_effect`、`stop_effect`、`get_effect_state`。工具使用已有场景与节点身份参数；`set_effect` 的 `effect` 参数为逻辑资源路径，空字符串清除引用。依赖的 `.efkmodel` 保留官方模型字节，贴图继续走标准图片导入与 meta 参数。

默认清单选入这两个插件；显式选择时将 `effects` 分别加入 `ENGINE_RUNTIME_PLUGINS`、`ENGINE_EDITOR_PLUGINS`。编辑器插件声明依赖运行时插件；两边都未选时不创建特效或 SDK 目标。依赖固定为 Effekseer 1808 和 efkbgfx 子模块，初始化方式为：

```sh
git submodule update --init third_party/effekseer third_party/efkbgfx
```

资源缓存由 `effect.fbs` 定义：原始官方效果数据加路径到 GUID 的依赖绑定，不改写效果内部算法数据。默认按源文件相对路径收集依赖，也可在 `.efkefc.meta` 配置通用映射，适用于外部资源路径调整：

```toml
[[resources]]
path = "Texture/Particle01.png"
resource = "effects/laser/textures/particle01.png"
```

LDR 颜色贴图要求 `srgb = true`，HDR/EXR 颜色贴图及法线、扭曲贴图要求线性空间；依赖缺失、路径越界、冲突映射或色彩空间冲突明确报错，失败不替换已发布资源。每个效果独立映射，不能因不同效果使用相同原始文件名而串用依赖。

当前范围为官方预定义材质和普通 sprite、ribbon、ring、track、model 效果。自定义材质、背景扭曲、软粒子深度采样、GPU 粒子、音频和外部曲线尚未接入，导入时明确拒绝。复用上游不代表全部 Effekseer 功能已经完成。架构见[特效系统与组件](class-diagrams/effects.md)和[特效导入](../editor/class-diagrams/effect-importer.md)。


公开展台及平台验证见[测试与范例](../../testing.md)。
