# 导航模块

`NavigationSystem` 接入 [Recast Navigation](https://github.com/recastnavigation/recastnavigation)，在初始化时创建 Recast 高度场与 Detour 导航网格，关闭时释放。系统由世界系统管理，失败通过返回值报告。

导航资源导入、寻路请求、代理移动及场景组件尚未实现。运行时依赖方向见[运行时模块](runtime.md)。
