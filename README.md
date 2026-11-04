# Engine

C++20 跨平台游戏引擎，使用 CMake、vcpkg 和成熟开源库。运行时、编辑器与插件分层；场景数据存入 Flecs ECS，`Node / Component` 提供轻量访问句柄；项目玩法使用 TypeScript，通用引擎能力由 C++ 提供。

## 功能与模块

| 模块 | 当前能力与文档 |
| --- | --- |
| 核心与场景 | 系统生命周期、ECS 场景、组件值句柄、场景加载、预制体与实例覆盖、常驻节点、世界暂停。[运行时](docs/runtime/runtime.md) · [场景](docs/runtime/scene.md) · [预制体](docs/editor/prefabs.md) |
| 渲染 | bgfx、标准 PBR、Forward+、阴影与反射、水、HDR、大气天空、视锥剔除、静态实例化与 LOD。[渲染模块](docs/runtime/rendering.md) |
| 光照烘焙 | 环境 IBL、多反弹 Lightmap、动态对象 SH9 光照探针。[场景烘焙](docs/plugins/editor/scene-bake.md) |
| 资源 | GUID、独立资源、增量导入、依赖加载、异步读取、运行包与完整版本同步。[加载](docs/runtime/resource-loading.md) · [导入](docs/editor/asset-pipeline.md) · [打包](docs/plugins/editor/resource-build.md) |
| 脚本 | TypeScript → JavaScript、Gameplay、逐类绑定、热重载；构建时选择 QuickJS 或 V8。[脚本](docs/plugins/runtime/scripting.md) · [SDK](include/engine.d.ts) |
| 输入与角色 | 键鼠、手柄、触摸、映射与重绑定、胶囊角色运动、第一/第三人称相机。[输入](docs/runtime/input.md) · [角色](docs/plugins/runtime/character-controller.md) |
| 物理 | Jolt 刚体、碰撞、射线/扫掠、范围查询及接触事件。[物理模块](docs/runtime/physics.md) |
| 导航 | Recast/Detour 寻路、局部瓦片更新、群体移动与避让。[导航模块](docs/runtime/navigation.md) |
| 动画 | ozz 采样与混合、GPU 蒙皮、InPlace 动作。[动画](docs/runtime/animation.md) · [FBX/USD 导入](docs/plugins/editor/model-import.md) |
| UI 与字体 | RmlUi 文档、TS MVC、多字体/图片、事件与输入、UI/3D 曲线文字。[UI](docs/runtime/ui.md) · [字体](docs/runtime/font.md) |
| 音频与特效 | SDL3 音源与空间衰减；Effekseer 粒子和模型特效。[音频](docs/runtime/audio.md) · [特效](docs/plugins/runtime/effects.md) |
| 地形与破坏 | 体积地形挖填、土石塌方、Blast 分块破坏、Jolt 碎块碰撞。[地形](docs/plugins/runtime/terrain.md) · [破坏](docs/plugins/runtime/destruction.md) |
| 并行与观测 | oneTBB 共享任务池、分域预算与优先度、异步委托、Tracy。[并行](docs/runtime/parallelism.md) · [性能分析](docs/runtime/performance.md) |
| 编辑器与建模 | 项目与资源管线、场景预览、MCP 编辑、撤销/重做、建模与资源保存。[编辑器](docs/editor/editor.md) · [MCP](docs/plugins/editor/mcp.md) · [建模](docs/plugins/editor/modeling.md) |

编辑器目前以 MCP 与场景预览为主要操作入口，完整图形编辑界面尚未完成。各模块文档列出具体支持范围；静态 GI 不随破坏实时重算，SSR 仍受屏幕信息限制。

## 快速开始

先按[构建指南](docs/building.md)准备 C++20 工具链、CMake、Ninja、编译缓存与 vcpkg，并设置 `VCPKG_ROOT`。完整编辑器还需要 Node.js、npm 和 Python 3；命令均从仓库根目录执行。

```sh
git submodule update --init
npm ci --prefix projects/showcase/settings
cmake --preset ninja-engine-debug
cmake --build --preset ninja-engine-debug-prepare
cmake --build --preset ninja-engine-debug
./build/ninja-engine-debug/editor_app --project projects/showcase --runtime-dir build/editor-runtime
```

Windows 在 Developer PowerShell 中执行，程序使用对应的 `.exe`。编辑器启动后默认空白待机；在另一终端启动范例菜单：

```sh
python3 scripts/mcp_client.py --server build/ninja-engine-debug/mcp_app --project projects/showcase --runtime-dir build/editor-runtime start
```

`projects/showcase` 是公开技术范例项目，项目说明集中在[项目文档](projects/showcase/docs/index.md)。CTest/GPU 验证命令、实际通过范围及已知问题见[测试与范例](docs/testing.md)。环境准备、Release、平台预设、导出与构建选项见[构建指南](docs/building.md)；资源修改后的更新流程见[同步与热重载](docs/plugins/editor/resource-sync.md)。

## 平台

| 平台 | 固定图形后端 | 入口 |
| --- | --- | --- |
| macOS | Metal | 原生桌面运行时与编辑器，可选 `.app` |
| Windows | DirectX 12 | 桌面运行时与编辑器，提供静态交付预设 |
| Linux | Vulkan | 桌面构建预设，按功能记录实际验证范围 |
| Android ARM64 | Vulkan | [APK 构建与运行](docs/runtime/android.md)，桌面 MCP 同步调试包 |
| iOS | Metal | 共享后端约定，完整宿主与交付尚未完成 |

共享实现不代表所有功能均已通过各平台实测。最终产物位于 `output/<platform>/<arch>/<debug|release>/<editor|runtime>/`；构建缓存位于 `build/`，项目缓存位于项目 `cache/`，均不提交 Git。

## 文档

- [架构总览](docs/architecture.md) · [类图评审入口](docs/class-diagrams/index.md) · [插件清单与选择](docs/plugins/plugins.md)
- [构建与交付](docs/building.md) · [开发与依赖维护](docs/development.md) · [协作约定](AGENTS.md)
- [项目设置](docs/editor/configuration.md) · [资源制作与导入](docs/editor/asset-pipeline.md) · [场景效果巡览](docs/editor/visual-review.md)
- [测试项目、验证记录与限制](docs/testing.md) · [渲染上游来源](docs/runtime/rendering-sources.md)

私有项目、付费素材、商业参考与构建产物保持本地隔离。CI 仅手动触发。
