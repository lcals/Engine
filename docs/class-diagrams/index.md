# 引擎类图

状态：本轮实现引擎、统一系统生命周期、各系统最小骨架与窗口渲染。下列图是架构评审入口；具体构建、测试和平台验证结果见根 [README.md](../../README.md)。

## 阅读入口

- [引擎与系统总图](engine-system.md)：只定义 `Engine` 和抽象基类 `System`。
- [完整引用关系](references.md)：分节列出全部继承、拥有、借用、参数、返回类型与第三方依赖。
- [配置与状态](types.md)：`EngineConfig` 和 `FrameStatus`。

## 运行时根系统

| 顺序 | 独立类图 | 当前职责与依赖 |
| --- | --- | --- |
| 1 | [窗口系统](window-system.md) | SDL3 窗口和事件 |
| 2 | [资源系统](resource-system.md) | FlatBuffers、Basis Universal 接入 |
| 3 | [世界系统](world-system.md) | flecs 世界及子系统生命周期 |
| 4 | [渲染系统](render-system.md) | bgfx 初始化、最小画面和帧提交 |

## 世界子系统

| 顺序 | 独立类图 | 当前依赖 |
| --- | --- | --- |
| 1 | [物理系统](physics-system.md) | Jolt |
| 2 | [导航系统](navigation-system.md) | Recast、Detour |
| 3 | [动画系统](animation-system.md) | ozz-animation |
| 4 | [界面系统](ui-system.md) | RmlUi |
| 5 | [脚本系统](script-system.md) | V8；启用时装配 |

## 宿主模块

- [Android 宿主](android-host.md)：SDL3 Activity 加载原生引擎，Activity 与 Surface 在销毁前同步暂停渲染；不要求编辑器项目。

- [项目系统](project-system.md)：编辑器宿主先检查项目根目录及 settings、cache、assets；接入 toml++。
- [编辑器系统](editor-system.md)：借用已就绪的项目系统，创建 Dear ImGui 上下文，支持编辑器子系统。
- `engine_app` 使用默认配置即可启动；`editor_app` 必须传入 `--project <path>`，先验证项目再启动窗口。当前编辑器为基本宿主，尚未绘制编辑器界面。
- [资源管线系统](asset-pipeline-system.md)：USD、Taskflow、SQLite、efsw、meshoptimizer；由资源工具宿主单独管理。
- shaderc、texturec、geometryc 是构建提供的宿主工具；fmt 用于基础格式化。它们不引入额外引擎包装类。
- 运行时不反向依赖编辑器、离线资源管线或具体游戏。Android 打包参考 SDL3 自带 Android 工程；APK 内部资源访问尚未实现，平台验证范围见根 [README.md](../../README.md)。

## 维护约定

- 遵守根 [AGENTS.md](../../AGENTS.md) 的 C++20 规范；不使用异常，通过返回值表达状态。
- 用户评审类、职责、所有权、关系和公开方法；助手负责实现、测试和图与代码同步。
- 每个类的方法只在所属图中定义一次。其他图出现同名类时仅表示外部引用，不重复维护接口。
- 每个具体系统及子系统独立成图。共有生命周期只在 `System` 中维护；各模块目录和 include 路径由构建配置提供。
- `+`、`#`、`-` 分别表示公开、受保护、私有；`<|--` 表示继承，`*--` 表示独占拥有，`o--` 表示持有共享底层资源的原生引用，`-->` 表示非拥有引用，`..>` 表示使用依赖。
- 方法采用 Mermaid 的“方法(参数) 返回类型”语法；静态方法用 `$`、纯虚方法用 `*`，C++ 限定符在图下注明。指针返回值的星号写为 `#42;`，避免被 Mermaid 误认成纯虚标记后省略。
- 关系标签使用普通文本；带 `::` 的 C++ 类型写在成员声明或图外说明中，避免额外冒号触发解析错误。修改后检查 Mermaid 解析和渲染。
- `library` 是第三方库，`external` 是第三方资源类型；标准库类型在方法签名中表达。拥有资源不等于拥有库本身。
- 已选依赖必须实际编译和链接；最小接入检查不表示玩法、物理、动画、UI 或编辑器业务已实现。
