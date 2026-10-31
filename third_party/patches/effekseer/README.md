# Effekseer 依赖清单校验

上游：<https://github.com/effekseer/Effekseer>，MIT。
固定版本 `1808`：`b5700082b49bc5f919610880dafddc90211a9a4e`。

`strict-effect-dependencies.patch` 沿用官方 `EfkEfcProperty` 解析器，仅补齐严格性：

- 拒绝官方 `ExporterVersion` 枚举之外的格式版本。
- 拒绝未知依赖类型、未知纹理颜色空间标志，以及未声明颜色空间的纹理。
- 拒绝非纹理依赖的非零保留标志与未解释的 INFO 尾部数据。
- 重新读取前清空上一次属性；失败实例由调用者丢弃。

补丁仅应用到构建目录中的单个源码副本，官方 CMake 继续管理其余 runtime 与
RendererCommon 源码列表。上游检出目录保持原样。
