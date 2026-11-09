# 角色与镜头范例

[项目入口](index.md) · [引擎角色控制](../../../docs/plugins/runtime/character-controller.md) · [动画状态机](animation-state-machines.md)

庭院、射击场、地形和破坏综合场景共用参数化角色与镜头配置。菜单选择角色动画庭院可观察行走、快跑、跳跃、蹲下及第一／第三人称切换；动画继续使用 InPlace，由物理驱动角色位移。

## 默认控制

| 操作 | 键鼠 | 标准游戏手柄映射 |
| --- | --- | --- |
| 移动 | W / A / S / D | 左摇杆 |
| 观察 | 按住鼠标右键拖动 | 右摇杆 |
| 跳跃 | Space | A／南侧键 |
| 快跑 | 按住左 Shift | 按住左摇杆按键 |
| 蹲下 | 按住左 Ctrl | 按住 B／东侧键 |
| 第一／第三人称 | Tab | Y／北侧键 |

键位及设备分配属于 [CharacterControls](../assets/scripts/character_controls.ts) 的项目策略。鼠标位移作为 lookDelta，摇杆作为 lookRate；输入每帧提交一次，由角色组件处理固定步消费。手柄映射说明不代表本轮已做实体手柄验收；触屏当前只配置移动、观察和跳跃区域。

## 场景配置

角色根节点组合 Transform、CharacterMotor 与 CharacterController。控制器显式声明 jump、sprint、crouch 动作及对应能力；蹲姿优先于跳跃和快跑，起身受阻时保持实际矮胶囊。每个角色独立接收对象形式输入，不依赖全局玩家选择。

相机根节点组合 Camera、CameraController 与 CharacterCamera。相机状态图使用 first_person 参数切换两套 Orbit 方案；yaw、pitch 与 height 驱动观察方向和眼高，Collision 在最终输出阶段处理视线遮挡。CharacterCamera 将实际胶囊高度映射到 height；角色身体和第一人称部件通过 CharacterVisual 控制每镜头可见性。

[control_templates.py](../tools/control_templates.py) 维护当前 Schema 对应的源配置；[庭院生成器](../tools/create_animation_courtyard.py) 与[综合场景生成器](../tools/create_showcase_destruction_integration.py) 复用这些配置。它们是源数据制作入口，不是旧格式转换工具。现有四个场景已使用同一结构；场景和预制体缓存为版本 3，正常导入会根据 Schema 指纹重新生成缓存，旧二进制不参与运行。

## 检查入口

从仓库根执行项目脚本检查；TypeScript staging 目录必须为空或不存在：

```sh
node scripts/build_scripts.mjs --project projects/showcase --output projects/showcase/cache/control-script-check
node projects/showcase/tests/launcher_test.mjs .
```

实际画面检查使用同一 Release 构建的 editor_app 与 mcp_app；按[编辑器文档](../../../docs/editor/editor.md)启动本项目后，从菜单进入角色动画庭院。第一人称应隐藏自身身体、保留自身阴影，其他角色保持可见；第三人称恢复身体显示。原始截图与回执留在忽略的 cache/ 和 build/，已执行的平台、测试和限制统一见[测试文档](../../../docs/testing.md#镜头与角色模块验证)。
