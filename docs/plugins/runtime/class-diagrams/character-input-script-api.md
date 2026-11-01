# 角色输入脚本接口

[角色原生类图](character-controller.md) · [统一输入](input-script-api.md) · [绑定注册](script-bindings.md)

```mermaid
classDiagram
    class CharacterController {
        <<script facade>>
        +SetInput(number moveX, number moveY, number lookX, number lookY, bool jump, bool toggleView) bool
        +ClearInput() bool
    }
    class Component
    class CharacterControllerBinding {
        +name() string_view
        +source() string_view
        +register_functions(ScriptVm& vm) bool
    }
    class ScriptBinding
    class ScriptVm
    class BindingContext
    class NativeCharacterController
    class CharacterInput
    Component <|-- CharacterController
    ScriptBinding <|-- CharacterControllerBinding
    CharacterControllerBinding ..> ScriptVm : 注册参数
    CharacterControllerBinding ..> BindingContext : 校验活动Run与节点身份
    CharacterControllerBinding ..> CharacterController : 定义脚本外观
    CharacterControllerBinding ..> NativeCharacterController : 查询并提交普通意图
    CharacterControllerBinding ..> CharacterInput : 参数数据
```

图中 NativeCharacterController 引用 `engine::plugins::character_controller::CharacterController`，不重复定义其方法。绑定只在角色插件已选时登记，沿用普通 Component / Node 句柄，不使用全局系统查找。节点所属场景结束后旧引用失效。

`SetInput` 将有限数值与布尔值转换为 CharacterInput，成功表示接受下次原生控制器更新的意图，不立即移动角色；输入每次更新消费一次，没有新提交就回零。`ClearInput` 同时清理组件待提交意图及它最近实际驱动的同实例 Motor 的主动移动和未消费跳跃，保留外部速度与待消费速度增量；不清未驱动角色或替换后的 Motor。脚本只决定何时取消，不必另外查询并清理 Motor。日常消费组件输入不会取消已排队跳跃。固定步、运动、相机和碰撞仍由原生负责。

示例 [CharacterControls](../../../../projects/showcase/assets/scripts/character_controls.ts) 在 TS 加载默认键位及 KV 覆盖、选择初始手柄、决定再次配对，提交通道后只查询少量动作并调用 SetInput。保存覆盖是显式操作，没有停止时自动保存。
