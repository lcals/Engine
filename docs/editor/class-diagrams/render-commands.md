# 渲染编辑命令

[类图索引](../../class-diagrams/index.md)

```mermaid
classDiagram
    class RenderCommands {
        +RenderCommands(RenderSystem& renderer, WorldSystem& world, Path capture_root)
        +register_commands(EditorCommands& commands) bool
    }
    class RenderSystem
    class WorldSystem
    class Scene
    class EditorCommands
    class EditorEdit
    class SettingsEdit {
        +affects_scene() bool
    }
    RenderCommands --> RenderSystem : 借用渲染接口
    RenderCommands --> WorldSystem : 借用当前场景
    RenderCommands ..> EditorCommands : 注册显示与截图命令
    RenderCommands ..> EditorEdit : 渲染设置撤销记录
    EditorEdit <|-- SettingsEdit
    SettingsEdit --> RenderSystem : 校验并提交当前场景参数
    SettingsEdit ..> Scene : 创建或撤销缺少的参数组件
```

编辑器决定截图目录并检查相对路径；渲染器只接收明确的文件路径。渲染设置支持撤销；截图是异步请求，回执中的 pending 不代表文件完成，get_frame_capture 返回真实完成状态。

`set_render_stats` 要求唯一的布尔参数 `enabled`，调用渲染系统的统计面板开关并返回保存的状态。该命令跨待机、场景预览及玩法生效，不进入撤销历史，不改变场景保存状态。

`set_render_settings` 有活动场景时编辑 EnvironmentComponent、活动相机节点上的 PostProcessComponent 与方向光参数，修改进入撤销历史并标记场景未保存。环境参数缺少组件时由编辑器创建普通环境节点，相机后处理缺少组件时附加到活动相机；阴影参数要求场景已有方向光。撤销同时移除本次自动创建的节点或组件。设置由场景保存、导入和重载。

无活动场景时仅修改渲染默认设置，`SettingsEdit::affects_scene()` 返回 false；不创建场景。编辑器不把场景参数写到项目脚本或核心全局覆盖值。

宿主独占 RenderCommands，命令注册和撤销历史释放之后销毁它；渲染系统与世界的有效期覆盖命令对象。

AO 字段 `aoEnabled`、`aoRadius`、`aoStrength`、`aoPower`、`aoFadeStart`、`aoFadeEnd` 属于后处理域，与曝光共用活动场景相机的 PostProcessComponent、快照和撤销记录。编辑器沿用已有后处理组件准备流程，缺少活动相机时拒绝；运行时核心不创建组件。AO 参数由同一场景保存链落盘，不添加独立编辑器覆盖状态。

`hdrEnabled`、`bloomEnabled`、`bloomThreshold`、`bloomIntensity`、`bloomRadius` 同属后处理域。一次 `set_render_settings` 可原子提交 HDR/Bloom 的两个开关；最终状态必须满足 Bloom 依赖 HDR，不因参数遍历顺序不同产生部分修改，也不自动改变未提交开关。失败回滚本次新建组件，成功进入同一撤销、重做和保存链。

级联阴影使用同一 `set_render_settings` / `get_render_settings`，字段为 `shadowCascades`、`shadowDistance`、`shadowSplitLambda`、`shadowCascadeBlend`。修改归方向灯域，实际变更要求已有启用方向灯，缺少时拒绝而不创建灯；阴影关闭且全部值未变化的空操作沿用原有成功语义。撤销和重做保存整份对应 Light 组件快照。级联数只接受 JSON 整数 1、2、4，其他数值遵循 Light 契约。
