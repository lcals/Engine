# 资源管线规划

本文记录已确认的资源处理流程；项目整体架构见[引擎架构规划](architecture.md)。资源部分采用引擎自有格式，具体格式后续确定。

## 资源与 meta

每个资源由原始资源文件及对应的 meta 文件组成。meta 统一使用 TOML 格式，字段结构按原始资源的文件类型设计。

## 管线与运行缓存

原始资源及其 meta 经资源管线生成运行缓存；运行时读取缓存，缓存文件以哈希值命名。

资源管线使用 [Taskflow](https://github.com/taskflow/taskflow) 高度并行地处理资源任务。管线核心编译为供引擎使用的库，同时提供复用该库的独立离线工具。

资源管线使用单机 SQL 数据库记录管线数据，资源 meta 仍保存在 TOML 文件中。管线监听项目文件变化，相关文件变更时触发并行处理，生成或更新运行缓存。

## 跨平台贴图

贴图保留原始文件与 meta。资源管线离线生成多种 GPU 压缩格式的运行缓存：桌面目标优先使用 BC 系列，支持 ASTC 的设备可大量使用 ASTC，同时保留未压缩格式作为兜底。运行时根据 bgfx 查询到的纹理格式能力选择相应缓存，ASTC 不作为唯一格式。

贴图编译优先复用 [bgfx texturec](https://bkaradzic.github.io/bgfx/tools.html)。如果需要独立的 ASTC 编码工具，再考虑 [Arm astcenc](https://github.com/ARM-software/astc-encoder)；如果需要单份中间纹理跨平台转码，再评估 [KTX-Software](https://github.com/KhronosGroup/KTX-Software) 与 [Basis Universal](https://github.com/BinomialLLC/basis_universal)。
