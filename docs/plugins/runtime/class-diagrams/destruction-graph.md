# 建筑破坏连通图

`DestructionGraph` 只桥接 NVIDIA Blast 5.1 官方 lowlevel 与 ExtDamageShaders。核心、场景和 Jolt 不进入图算法。生成数据由 FlatBuffers 提供，运行态只保存 Blast 内存，不把其 ABI 内存写入资源。

```mermaid
classDiagram
class DestructionGraph {
  +validate(DestructionAssetDataT, string) bool
  +initialize(DestructionAssetDataT, string) bool
  +clone(DestructionGraph, string) bool
  +damage_sphere(uint32, Vec3, float, float, string) bool
  +restore_health(span~float~, string) bool
  +healths() vector~float~
  +data() DestructionAssetDataT
  +groups() vector~DestructionGroup~
}
class DestructionGroup {
  +uint32 id
  +vector~uint32~ chunks
  +bool anchored
}
class DestructionAssetDataT
class BlastFamily
class BlastAsset
DestructionGraph ..> DestructionAssetDataT : 输入
DestructionGraph *-- BlastFamily : 独占 / 实现私有
DestructionGraph o-- BlastAsset : 克隆之间共享不可变资产
DestructionGraph *-- DestructionGroup : 输出当前连通组
```

组 ID 为最小碎块索引，不把 Blast 可重用 actor index 当稳定标识。损伤针对调用方指定的当前组，中心已经变换回该组原始资产局部坐标。同一次爆炸由调用方固定旧组列表，避免新组重复受伤。

血量顺序为资源内部 bonds，随后按碎块顺序的 world bonds；锚点初始血量来自资源的 `anchor_health`。恢复时先创建健康连接，再显式断开已为零的 bonds，确保 Blast 的连通图与血量一致。候选图通过官方 family 可重定位内存克隆，调用方统一决定发布或丢弃。
