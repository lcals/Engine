# 参数驱动的共享动画状态机

从现有菜单打开 **Character, animation and camera**（courtyard）。主角继续使用现有物理控制和镜头，另外七个角色在不同时间阶段停驻、走路和跑步。

- `assets/scripts/animation/locomotion_machine.ts`：一份共享定义、状态和条件，内部读取 `speed` 并转换；移动状态按参考步速调整播放倍率。
- `assets/scripts/modules/courtyard.ts`：业务读取主角 CharacterMotor 的实际速度，只提交参数；七个展示角色提交不同阶段的速度值，机器由宿主统一驱动。
- `settings/resource-build.toml`：显式包含 walk/run；控制器代码通过普通静态 import 进入既有脚本依赖闭包。

观察一个角色的转换和相位是否影响其他角色；返回菜单再进入应创建全新的组件与机器上下文。停驻暂使用 walk 首帧，尚未新增美术待机动作；七个展示角色仅演示姿态，根节点不由动画移动。

此场景验证共享定义、参数隔离、自动驱动和现有移动链路，不是敌人性能基准，也不是跨骨架、IK 或动画事件的美术验收。对应原生／脚本／资源自动化用例和实测范围见[引擎测试文档](../../../docs/testing.md)；接口与限制见[动画模块](../../../docs/runtime/animation.md)。
