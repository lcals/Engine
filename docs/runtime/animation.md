# 动画模块

`AnimationSystem` 接入 [ozz-animation](https://github.com/guillaumeblanc/ozz-animation)，通过运行时骨架对象的构造与释放验证依赖编译、链接和生命周期。它由世界系统管理，不依赖编辑器。

动画资源加载、采样、混合及骨骼组件尚未实现；GPU 蒙皮归[渲染模块](rendering.md)，动画系统负责提供对应姿态数据。
