# 编辑器配置

配置按职责区分：`EngineConfig` 保存窗口等运行时配置；项目 `settings/` 保存 TypeScript 工程、依赖锁文件和资源导入设置；界面语言等编辑器偏好不属于运行时启动条件。

`settings/import.toml` 的 `import_threads` 控制 Taskflow 导入线程数，默认 4，允许 1–64。缺少该文件时使用默认值，内容或范围非法时导入失败。线程设置归项目资源管线，不是编辑器全局配置。

独立编辑器本地配置文件及默认值写入尚未实现。已实现配置与状态契约见[配置与状态类图](../runtime/class-diagrams/types.md)，项目目录约束见[项目与出包](project.md)。
