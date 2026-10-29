# 可编辑多边形模型

持久数据只由 `include/schemas/runtime/modeling.fbs` 生成。模型区分共享几何顶点与每面角 UV、颜色，支持多边形与洞、稳定身份、多材质和平滑组。核心不依赖编辑器或 MCP。

```mermaid
classDiagram
    class EditableMesh {
        +validate(EditableMeshDataT data, string error) bool
        +compile(EditableMeshDataT data, string_view path, MeshDataT output, string error) bool
        +from_mesh(MeshDataT data, EditableMeshDataT output, string error) bool
        +serialize(EditableMeshDataT data, ByteVector output, string error) bool
        +deserialize(ByteSpan bytes, EditableMeshDataT output, string error) bool
        +find_vertex(EditableMeshDataT data, uint64 id) ModelingVertex
        +find_face(EditableMeshDataT data, uint64 id) ModelingFaceT
        +selected_vertices(EditableMeshDataT data, MeshSelection selection, IdVector output, string error) bool
        +triangulate_face(EditableMeshDataT data, ModelingFaceT face, IndexVector output, string error) bool
    }
    class EditableMeshDataT
    class ModelingVertex
    class ModelingFaceT
    class ModelingLoopT
    class MeshDataT
    class MeshSelection
    class MeshEdge
    EditableMeshDataT *-- ModelingVertex : 共享位置与稳定ID
    EditableMeshDataT *-- ModelingFaceT : 面与稳定ID
    ModelingFaceT *-- ModelingLoopT : 孔洞与每角属性
    MeshSelection *-- MeshEdge : 边端点ID
    EditableMesh ..> EditableMeshDataT : 验证与序列化
    EditableMesh ..> MeshDataT : 编译与导入
    EditableMesh ..> MeshSelection : 解析选区
```

所有函数为静态普通 C++ 接口。输入借用，输出由调用方拥有；失败清空输出并返回错误。验证限制有限数值、唯一身份、合法环、共面与无自交；编译采用 Earcut 三角化并保留每角 UV、材质和平滑组。空模型可编辑与保存，但不能编译成渲染网格。

`from_mesh` 按位置建立共享顶点，并保持每角 UV、颜色和材质槽。平滑组由共享边两端的输入法线一致性恢复，后续编译按面面积重算法线；硬边不因位置焊接而自动平滑。任意人工指定法线不作为独立持久数据逐值保留。

验证和编译每次建立一次顶点身份索引，多个面共享该索引；不向 Node 或运行时渲染网格加入编辑拓扑缓存。
