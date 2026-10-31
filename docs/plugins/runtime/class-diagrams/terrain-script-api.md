# 地形脚本接口

[绑定实现](script-bindings.md) · [运行时地形](terrain.md)

```mermaid
classDiagram
    class Component
    class TerrainComponent {
        +DigSphere(number x, number y, number z, number radius) boolean
        +FillSphere(number x, number y, number z, number radius) boolean
        +Reset() boolean
        +Save(string slot) boolean
        +Load(string slot) boolean
        +ready boolean
        +revision bigintOrNull
        +chunkCount numberOrNull
        +rebuiltChunks numberOrNull
        +triangleCount numberOrNull
        +lastError stringOrNull
    }
    class TerrainBinding {
        <<final>>
        +name() string_view
        +source() string_view
        +register_functions(ScriptVm& vm) bool
    }
    class ScriptBinding
    class ScriptVm
    class BindingContext
    class NativeTerrain["engine::plugins::terrain::TerrainComponent"]
    class App
    Component <|-- TerrainComponent
    ScriptBinding <|-- TerrainBinding
    TerrainBinding ..> TerrainComponent : 提供脚本门面
    TerrainBinding ..> NativeTerrain : 借用并调用地形工具接口
    TerrainBinding ..> BindingContext : 校验当前回调和节点身份
    TerrainBinding ..> ScriptVm : 注册后端无关原生方法
    TerrainBinding ..> App : 读取持久数据根目录
```

- `TerrainBinding` 属于脚本插件；只有构建期启用地形插件并定义 `ENGINE_HAS_TERRAIN_PLUGIN=1` 时编译与登记，不向脚本核心加入具体地形算法。方法均为 `const override`，`name/source` 为 `noexcept`，不持有组件所有权。
- 脚本通过 `Node.GetComponent(TerrainComponent)` 取得已配置组件；`AddComponent` 只创建组件，未绑定资源时 `ready` 为 false。门面按场景与节点身份重新解析组件，不保存原生指针。
- 球形笔刷坐标为地形节点局部空间，半径须为正且有限；挖掘、填充直接调用原生 `apply_sphere`，复用块缓存局部重建。`Reset`/`Load` 从初始资源或快照重新初始化并全块重建；所有路径均整体上传网格与构建碰撞，同步返回状态，失败返回 false。
- `Save/Load` 只接受长度 `1..48` 的 ASCII `[A-Za-z0-9_-]` 槽名，定位到 `App::persistent_data_path()/terrain/<slot>.bin`。固定目录、目标文件和规范路径逐项检查，拒绝符号链接、路径越界和非普通文件；不向脚本开放任意路径。保存只创建固定 terrain 目录，加载不创建目录。
- `ready` 为布尔值；失效句柄返回 false。`revision` 保持 uint64 对应的 BigInt；块数、重建块数和三角形数为 Number，失效句柄返回 null；`lastError` 返回原生诊断，失效句柄返回 null。
