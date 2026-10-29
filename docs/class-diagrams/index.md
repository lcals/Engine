# 引擎类图

类图是架构评审入口，维护类的职责、所有权、公开方法和完整引用关系。构建与平台验证范围见根 [README.md](../../README.md)。

- [运行宿主](../runtime/class-diagrams/runtime-host.md)：装配具体系统，处理窗口事件、暂停与帧提交。
- [玩法运行会话](../runtime/class-diagrams/gameplay-session.md)：协调脚本、场景、GPU 与项目资源的启动和释放。
- [核心 GUID 映射](../runtime/class-diagrams/guid-map.md)：编辑器与真机复用，完整 GUID 与逻辑路径双向查询。

- [任务系统](../editor/class-diagrams/task-system.md)：Taskflow 执行器，项目默认 4 个导入线程。
- [场景导入器](../editor/class-diagrams/scene-importer.md)：TOML 场景读写与生成数据校验。
- [资源管线](../editor/class-diagrams/asset-pipeline-system.md)：meta 身份、SQLite 索引、依赖增量和 GUID 缓存发布。

## 场景与项目运行

- 场景中的全部对象必须通过 Node + Component 表达，包含相机与灯光；场景通过 GUID 组装独立 Mesh、材质与 Shader 资源。
- [场景](../runtime/class-diagrams/scene.md)：世界独占活动场景，场景独占节点。
- [节点](../runtime/class-diagrams/node.md)：轻量身份、层级及无具体类型的组件列表；泛型添加和查询。
- [组件](../runtime/class-diagrams/component.md)：Component、Transform、Camera、Light、MeshRenderer 及当前参数。
- [材质](../runtime/class-diagrams/material.md)：内置/项目路径选择材质与 Shader，按名字和数值类型匹配参数。
- [场景数据模型](../runtime/class-diagrams/scene-data.md)：FlatBuffers 定义、C++/TS 生成及组件数据所有权。
- [编辑器环境](../editor/class-diagrams/editor-environment.md)：平台路径映射和会话内置资源准备。
- [渲染](../runtime/class-diagrams/render-system.md)：同一窗口的空白/PBR 场景呈现和每次运行 GPU 资源。
- [编辑器](../editor/class-diagrams/editor-system.md)：默认待机，主线程处理 MCP 启停，停止恢复空白。
- [MCP](../plugins/class-diagrams/mcp-system.md)：start_project、stop_project 完成回执；保留同步及热重载工具。
- [脚本公开接口](../runtime/class-diagrams/gameplay.md)：Gameplay 加载场景或动态创建节点与组件，运行停止或重载后旧引用安全失效。

- [绑定接口与注册表](../runtime/class-diagrams/script-bindings.md)：逐类实现 ScriptBinding，新增绑定只需增加类和注册项。

## 脚本接口

- [App 平台路径](../runtime/class-diagrams/app.md)：缓存、包内资源和持久数据具有跨平台一致含义。
- [日志系统](../runtime/class-diagrams/log-system.md)：宿主管理的单例，默认写入磁盘。
- [项目玩法入口](../runtime/class-diagrams/gameplay.md)：项目 `Game` 继承 `Gameplay`，可承载项目业务与场景流转。
- [资源同步](../editor/class-diagrams/asset-sync-system.md)：完整批次发布到持久资源版本目录。
- [MCP 系统](../plugins/class-diagrams/mcp-system.md)：桌面 stdio 工具，向本机或 Android 调试包同步，并控制热重载与查询状态。

## 阅读入口

- [引擎与系统总图](../runtime/class-diagrams/engine-system.md)：只定义 `Engine` 和抽象基类 `System`。
- [完整引用关系](references.md)：分节列出全部继承、拥有、借用、参数、返回类型与第三方依赖。
- [配置与状态](../runtime/class-diagrams/types.md)：`EngineConfig` 和 `FrameStatus`。
- [公共路径工具](../runtime/class-diagrams/path-utils.md)：`PathUtils` 只负责 UTF-8 与文件系统路径转换。

模块说明和独立类图跟随源码职责分为三层；本页与完整引用关系保留为跨层统一入口：

| 范围 | 文档与独立类图 | 源码职责 |
| --- | --- | --- |
| 运行时 | `docs/runtime/`、`docs/runtime/class-diagrams/` | 引擎、系统、App、日志、资源、世界子系统及公共工具 |
| 编辑器 | `docs/editor/`、`docs/editor/class-diagrams/` | 编辑器、项目、资源管线和资源同步 |
| 插件 | `docs/plugins/`、`docs/plugins/class-diagrams/` | MCP 工具集成及插件接口 |

头文件和实现对应 `include/engine/<scope>/` 与 `src/engine/<scope>/`。运行时不反向依赖编辑器或插件；TS 公开声明集中在 [include/engine.d.ts](../../include/engine.d.ts)。

## 运行时根系统

RuntimeHost 独占窗口并向 Engine 注册资源、世界和渲染根系统；Engine 只通过 System 接口调度。

| 初始化顺序 | 独立类图 | 所有者及职责 |
| --- | --- | --- |
| 1 | [窗口系统](../runtime/class-diagrams/window-system.md) | RuntimeHost 独占；SDL3 窗口和事件 |
| 2 | [资源系统](../runtime/class-diagrams/resource-system.md) | Engine 独占；SDL3 同步读取、FlatBuffers、Basis Universal |
| 3 | [世界系统](../runtime/class-diagrams/world-system.md) | Engine 独占；flecs 世界、场景和子系统生命周期 |
| 4 | [渲染系统](../runtime/class-diagrams/render-system.md) | Engine 独占；bgfx 设备、空白帧和资源网格 PBR |

## 世界子系统

| 顺序 | 独立类图 | 当前依赖 |
| --- | --- | --- |
| 1 | [物理系统](../runtime/class-diagrams/physics-system.md) | Jolt |
| 2 | [导航系统](../runtime/class-diagrams/navigation-system.md) | Recast、Detour |
| 3 | [动画系统](../runtime/class-diagrams/animation-system.md) | ozz-animation |
| 4 | [界面系统](../runtime/class-diagrams/ui-system.md) | RmlUi |
| 5 | [脚本系统](../runtime/class-diagrams/script-system.md) | V8；启用时装配 |

## 宿主模块

- [Android 宿主](../runtime/class-diagrams/android-host.md)：SDL3 Activity 加载原生引擎，Activity 与 Surface 在销毁前同步暂停渲染；不要求编辑器项目。

- [项目系统](../editor/class-diagrams/project-system.md)：编辑器宿主先检查项目根目录及 settings、cache、assets；接入 toml++。
- [编辑器系统](../editor/class-diagrams/editor-system.md)：借用已就绪的编辑器环境，创建 Dear ImGui 上下文，支持编辑器子系统。
- `engine_app` 使用默认配置即可启动；`editor_app` 必须传入 `--project <path>`，先验证项目再启动窗口。编辑器默认空白待机，MCP 启动后项目占满同一窗口，停止后恢复空白；尚未绘制编辑器业务界面。
- [资源管线系统](../editor/class-diagrams/asset-pipeline-system.md)：USD、Taskflow、SQLite、efsw、meshoptimizer；由资源工具宿主单独管理。
- shaderc、texturec、geometryc 是构建提供的宿主工具；fmt 用于基础格式化。它们不引入额外引擎包装类。
- 运行时不反向依赖编辑器、离线资源管线或具体游戏。Android 打包参考 SDL3 自带 Android 工程；APK 内置资源通过 SDL 文件流读取，平台验证范围见根 [README.md](../../README.md)。

## 维护约定

- 遵守根 [AGENTS.md](../../AGENTS.md) 的 C++20 规范；不使用异常，通过返回值表达状态。
- 用户评审类、职责、所有权、关系和公开方法；助手负责实现、测试和图与代码同步。
- 每个类的方法只在所属图中定义一次。其他图出现同名类时仅表示外部引用，不重复维护接口。
- 每个具体系统及子系统独立成图。共有生命周期只在 `System` 中维护；自有头文件以 `runtime/`、`editor/`、`plugins/` 开头，构建提供统一搜索根目录。
- `+`、`#`、`-` 分别表示公开、受保护、私有；`<|--` 表示继承，`*--` 表示独占拥有，`o--` 表示持有共享底层资源的原生引用，`-->` 表示非拥有引用，`..>` 表示使用依赖。
- 方法采用 Mermaid 的“方法(参数) 返回类型”语法；静态方法用 `$`、纯虚方法用 `*`，C++ 限定符在图下注明。指针返回值的星号写为 `#42;`，避免被 Mermaid 误认成纯虚标记后省略。
- 关系标签使用普通文本；带 `::` 的 C++ 类型写在成员声明或图外说明中，避免额外冒号触发解析错误。修改后检查 Mermaid 解析和渲染。
- `library` 是第三方库，`external` 是第三方资源类型；标准库类型在方法签名中表达。拥有资源不等于拥有库本身。
- 已选依赖必须实际编译和链接；最小接入检查不表示玩法、物理、动画、UI 或编辑器业务已实现。
