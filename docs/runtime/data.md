# 跨模块数据

引擎、编辑器与脚本使用 [FlatBuffers](https://flatbuffers.dev/) 定义和交换结构化资源数据。Schema 源文件统一位于 `include/schemas/runtime/`，构建时由宿主 flatc 生成 C++ 和 TypeScript；生成物不纳入版本管理。

`scene.fbs` 定义场景节点及组件，`mesh.fbs`、`model.fbs`、`skeleton.fbs`、`animation.fbs`、`material.fbs`、`shader.fbs`、`audio.fbs` 和 `asset_catalog.fbs` 分别定义网格、模型资源、骨架、动作、材质、Shader、音频 PCM 与资源目录，`common.fbs` 提供共享数值类型。行为类使用生成数据，不另写一套持久化模型。场景中的资源引用使用 GUID，持久节点 ID 与运行实体 ID 分开。物理和音频组件只保存设置；Jolt/SDL 句柄、冲量请求和播放游标不序列化。

编辑器将源资源转换为 `cache/gen/<完整GUID>` 二进制。核心 `VirtualFileSystem` 提供同步与异步范围读取，`ResourceSystem` 对单项资源进行解码和校验；目录只保存元数据。ResourceGraphSystem 插件实现统一 ResourceLoader 接口，组织必要依赖、共享与引用计数。业务先加载，再使用已加载数据，并显式释放获取；运行实例独立保活，未加载时使用失败。内存映射读取尚未实现。

源资源描述及 meta 使用 TOML，增量索引使用 SQLite，脚本及未转换文件保留各自内容格式。SQLite 和源 meta 不进入运行资源包，详见[资源管线](../editor/asset-pipeline.md)。

FBX/USD 格式插件只存在于编辑器，按各自已支持的范围输出统一运行数据：FBX 使用 Model/Mesh/Skeleton/Animation Schema，当前 USD 使用 Model/Mesh。运行时对应 ResourceCodec 负责单项解码与声明引用校验，Scene 和 AnimationSystem 消费生成数据，依赖闭包仍由资源加载插件组织。几何、骨架和动画可以独立 GUID 读取，不要求把整个模型一次解析成常驻资源。

美术源文件的虚拟包分组与子资源 meta 属于编辑器。运行时 Model 版本 2 只保存节点层级及 Mesh/Material 引用；同源动画不再是 Model 的隐式依赖。旧 Model 版本 1 缓存和运行包必须重新导入、构建；废弃字段保留原 FlatBuffers 槽位，不复用。

## 生成与组件扩展

`include/schemas/runtime/scene.fbs` 只描述场景节点和组件，网格/材质通过 GUID 引用。`mesh.fbs`、`model.fbs`、`skeleton.fbs`、`animation.fbs`、`shader.fbs`、`material.fbs`、`audio.fbs`、`asset_catalog.fbs`、`resource_header.fbs` 独立维护对应模型，`common.fbs` 提供共享数值类型。构建使用宿主 flatc 生成 C++ 到 `build/<preset>/generated/include/engine/runtime/schema/`，生成 TS 到 `build/<preset>/generated/typescript/`；不提交生成物。可用 `ENGINE_FLATC_EXECUTABLE` 指定能在宿主运行的 flatc。FlatBuffers npm 依赖归项目 settings；场景以 `.scene` TOML 文件保存，导入为 FlatBuffers；运行时校验并读取 GUID 二进制。

Component 统一提供生成数据的 `build_data` / `restore_data`，各具体组件在自己的文件处理恢复与校验，通过 `ComponentRegistry::add<T>()` 登记创建函数。Scene 只组织节点和层级；编辑器 TOML 读写位于资源管线的 `components/`，运行时资源实体解码位于 `runtime/resource/codecs/`。
