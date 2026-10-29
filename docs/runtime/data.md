# 跨模块数据

引擎、编辑器与脚本使用 [FlatBuffers](https://flatbuffers.dev/) 定义和交换结构化资源数据。Schema 源文件统一位于 `include/schemas/runtime/`，构建时由宿主 flatc 生成 C++ 和 TypeScript；生成物不纳入版本管理。

`scene.fbs` 定义场景节点及组件，`mesh.fbs`、`model.fbs`、`skeleton.fbs`、`animation.fbs`、`material.fbs`、`shader.fbs`、`audio.fbs` 和 `asset_catalog.fbs` 分别定义网格、对象包、骨架、动作、材质、Shader、音频 PCM 与资源目录，`common.fbs` 提供共享数值类型。行为类使用生成数据，不另写一套持久化模型。场景中的资源引用使用 GUID，持久节点 ID 与运行实体 ID 分开。物理和音频组件只保存设置；Jolt/SDL 句柄、冲量请求和播放游标不序列化。

编辑器将源资源转换为 `cache/gen/<完整GUID>` 二进制。运行时通过 `ResourceSystem` 的 SDL3 同步文件接口读取，校验数据结构、目录及引用后使用。资源目录只保存元数据；核心按需读取独立实体或包成员。ResourceGraphSystem 插件通过统一 ResourceLoader 接口组织必要依赖和共享缓存，实际使用者持有对象及其依赖闭包。内存映射读取尚未实现。

源资源描述及 meta 使用 TOML，增量索引使用 SQLite，脚本及未转换文件保留各自内容格式。SQLite 和源 meta 不进入运行资源包，详见[资源管线](../editor/asset-pipeline.md)。

FBX/USD 格式插件只存在于编辑器，输出相同 Model/Mesh/Skeleton/Animation Schema。运行时对应 ResourceCodec 负责单项解码与声明引用校验，Scene 和 AnimationSystem 消费生成数据，依赖闭包仍由资源加载插件组织。几何、骨架和动画可以独立 GUID 读取，不要求把整个源对象包一次解析成常驻资源。
