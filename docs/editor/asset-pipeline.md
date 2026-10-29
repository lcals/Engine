# 资源管线规划

本文记录已确认的资源处理流程；总体关系见[引擎架构总览](../architecture.md)，资源所在的项目结构见[项目与出包](project.md)。

## 资源与 meta

DCC 制作的资源在项目资源目录中采用 [USD](https://openusd.org/) 格式，贴图也由 DCC 侧处理。预留 DCC 实时同步插件，使创作修改能够同步到项目资源并在引擎中实时预览。

DCC 专有数据到引擎资源和材质的转换尽量由 DCC 侧适配插件负责，引擎主体只处理统一的项目资源和运行缓存。

每个资源由原始资源文件及对应的 meta 文件组成。meta 统一使用 TOML 格式，由 [toml++](https://github.com/marzer/tomlplusplus) 读写；字段结构按原始资源的文件类型设计。

每个资源的 meta 保存稳定 ID，场景、预制体等通过该 ID 引用资源。哈希文件名只标识生成的运行缓存，不替代资源 ID。

## 管线与运行缓存

原始资源及其 meta 经资源管线生成运行缓存；运行时读取缓存，缓存文件以哈希值命名。

结构化运行缓存的跨模块格式及内存映射约定见[跨模块数据](../runtime/data.md)。

管线使用 DCC 提供的资源依赖分析，维护资源 ID、依赖关系与当前缓存的映射；依赖变化时更新受影响的缓存。运行时通过资源 ID 定位并加载缓存，项目出包生成供运行时查找缓存的资源清单。

资源管线使用 [Taskflow](https://github.com/taskflow/taskflow) 高度并行地处理资源任务。管线核心编译为供引擎使用的库，同时提供复用该库的独立离线工具。

资源管线使用 [SQLite](https://www.sqlite.org/about.html) 记录管线数据，资源 meta 仍保存在 TOML 文件中。管线通过 [efsw](https://github.com/SpartanJ/efsw) 监听项目文件变化，相关文件变更时触发并行处理，生成或更新运行缓存。

项目中的 Mesh 由资源管线使用 [meshoptimizer](https://github.com/zeux/meshoptimizer) 处理后生成运行时缓存。

## 视觉资源热重载

编辑器 Play 模式下，材质、贴图或 Shader 及其 meta 发生变化时，资源管线更新受影响的缓存，并通知运行时通过稳定资源 ID 加载新版本。渲染层安全替换正在使用的材质、贴图或 Shader，场景引用保持有效；处理失败时继续使用上一个有效版本，并向编辑器报告错误。

## 跨平台贴图

贴图由 DCC 侧处理后进入项目资源，跨平台贴图数据采用 [Basis Universal](https://github.com/BinomialLLC/basis_universal)。引擎将其纳入运行缓存，并在运行时适配目标设备支持的贴图格式；ASTC 不作为唯一目标格式。
