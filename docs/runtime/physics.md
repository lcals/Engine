# 物理模块

`PhysicsSystem` 接入 [Jolt Physics](https://github.com/jrouwe/JoltPhysics)，初始化时验证链接库的版本与 ABI，由世界系统统一管理生命周期。

碰撞世界、刚体组件、物理步进及场景同步尚未实现。物理能力属于跨平台运行时，不依赖编辑器或项目导入器，见[运行时模块](runtime.md)。
