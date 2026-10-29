# 节点组件

[类图索引](../../class-diagrams/index.md) · [节点](node.md)

```mermaid
classDiagram
    class Component {
        <<abstract>>
        +~Component()
        +node() Node&
        +node() const Node&
        +type() ComponentType
        +enabled() bool
        +set_enabled(bool enabled) void
    }
    class Camera {
        +settings() const CameraSettings&
        +set_settings(const CameraSettings& settings) bool
    }
    class Light {
        +settings() const LightSettings&
        +set_settings(const LightSettings& settings) bool
    }
    class MeshRenderer {
        +mesh() MeshKind
        +material() const Material&
        +set_material(const Material& material) bool
    }
    class Node
    class ComponentType {
        <<enumeration>>
        camera
        light
        mesh_renderer
    }
    class CameraSettings {
        <<struct>>
        +float fov_degrees
        +float near_clip
        +float far_clip
    }
    class LightSettings {
        <<struct>>
        +Color color
        +float intensity
    }
    class Material {
        <<struct>>
        +Color base_color
        +float metallic
        +float roughness
        +bool unlit
    }
    class Color {
        <<struct>>
        +float r
        +float g
        +float b
        +float a
    }
    class MeshKind {
        <<enumeration>>
        cube
    }
    Component <|-- Camera
    Component <|-- Light
    Component <|-- MeshRenderer
    Node "1" *-- "0..*" Component : 独占
    Component --> Node : 借用所属节点
    Component ..> ComponentType : 返回类型
    Camera *-- CameraSettings : 相机参数
    Light *-- LightSettings : 平行光参数
    MeshRenderer *-- Material : 材质参数
    MeshRenderer ..> MeshKind : 内置网格类型
    LightSettings *-- Color : 光颜色
    Material *-- Color : 基础色
```

- Component 使用公共虚析构、禁止复制和移动，具体组件只能由 Node 创建；共同状态为所属节点、显式类型和启用状态。
- Camera 位置和朝向来自节点变换。渲染器用世界 -Z 与 +Y 正交化相机基，忽略缩放大小；视场角大于 0 小于 180 度，近裁面大于 0，远裁面大于近裁面。
- Light 当前是平行光，方向取节点世界矩阵的 -Z，位置不影响平行光；颜色各分量为 [0,1]，强度为非负有限数。
- MeshRenderer 当前网格为内置 cube，材质颜色、金属度与粗糙度均为 [0,1]，所有参数必须有限；unlit 支持先验证基础绘制。
- 设置方法失败不改变原参数。只禁用组件不改变所有权；渲染遍历忽略未启用组件。
- GPU 缓冲、Shader 和管线归 RenderSystem 的本次运行资源管理，不放进节点或脚本句柄。
