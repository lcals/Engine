# 活动场景保存

[类图索引](../../class-diagrams/index.md)

```mermaid
classDiagram
    class ScenePersistence {
        +ScenePersistence(WorldSystem& world, filesystem_path project_root, MeshSourceWriter writer)
        +register_commands(EditorCommands& commands) bool
    }
    class WorldSystem
    class EditorCommands
    class SceneImporter
    class Node
    class MeshSourceWriter
    ScenePersistence --> WorldSystem : 借用活动场景
    ScenePersistence ..> EditorCommands : 注册 save_scene
    ScenePersistence ..> SceneImporter : 写入生成数据快照
    ScenePersistence ..> Node : 读取身份与组件
    ScenePersistence *-- MeshSourceWriter : 持有宿主注入函数
```

`MeshSourceWriter = std::function<bool(const Node&, const std::filesystem::path&, std::string&)>`，图中的名称是函数类型别名，不是新增业务类。`ScenePersistence` 属于 `engine::editor`，只使用该函数契约，不包含或链接 `ModelingCommands`；宿主创建捕获建模对象的回调，并保证该对象在回调释放前存活。
保存由编辑器驱动：以 FlatBuffers 原生数据快照记录层级、组件、启用状态及活动相机，
把当前 Mesh 与所有材质槽覆盖值写入场景相邻资源目录；邻接 meta 复用资源管线身份规则。
保存不改变运行场景、不导入、不修改运行目录；完成后由调用方停止、同步资源并重启项目。
失败恢复已覆盖的源文件与 meta；回滚失败保留 cache 暂存备份并返回其位置；成功才发布场景描述。`save_scene` 为非撤销文件命令。
运行时只读取已有 Scene/Node/Component 和资源接口，MCP 只传递命令及回执。
建模插件通过回调保存可编辑源；加载项目场景时依据 mesh 路径恢复可编辑源和编辑器身份。

当前编辑器宿主在资源管线和建模插件同时启用时装配该功能；关闭建模插件不会登记 `save_scene`。这是宿主对网格写入实现的选择，不构成编辑器基础库对插件的编译依赖。
