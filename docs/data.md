# 跨模块数据规划

本文记录引擎主体、编辑器与运行时之间的数据约定；总体关系见[引擎架构总览](architecture.md)。

跨模块结构化数据统一使用 [FlatBuffers](https://flatbuffers.dev/) 定义和交换。

适合直接读取的文件化运行缓存支持内存映射访问。内存映射能力需要兼顾 Windows、macOS 和 Linux；具体使用范围由数据用途决定。

原始资源及其 TOML meta、资源管线的 SQLite 数据、贴图等专用缓存仍按各自模块的格式处理，见[资源管线](asset-pipeline.md)。
