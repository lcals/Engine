# 插件目录与构建登记

插件按运行时与编辑器划分，独立选择、独立编译。引擎基础接口留在 `runtime/`，项目环境、通用编辑命令、Task 和资源管线留在 `editor/`；插件通过这些接口提供具体策略或扩展能力。

| 范围 | 源码与头文件下的相对目录 | 提供的能力 |
| --- | --- | --- |
| 运行时资源依赖图 | `plugins/runtime/resource_graph/` | 必要依赖加载、共享缓存及按引用反向释放，实现核心加载接口 |
| 运行时脚本 | `plugins/runtime/script/` | V8、JS 模块与玩法执行、逐类绑定，实现核心 ScriptRuntime |
| 编辑器建模 | `plugins/editor/modeling/` | 纯几何、拓扑、UV、网格编译、模型与节点映射、选择、撤销、源资源保存及本模块 MCP 适配 |
| 编辑器 MCP | `plugins/editor/mcp/` | MCP 协议、工具注册、参数检查、项目/场景/渲染工具和固定目标通道 |
| 编辑器资源构建 | `plugins/editor/asset_build/` | 项目打包清单、增量散文件/合并包及精简运行清单 |
| 编辑器资源同步 | `plugins/editor/asset_sync/` | 向本机或 Android 调试包发布完整资源版本 |

头文件以完整作用域引用，例如 `plugins/editor/modeling/editable_mesh.hpp`、`plugins/editor/mcp/editor_mcp_tools.hpp`。纯运行时插件不得引用编辑器基础或编辑器插件；编辑器插件可以组合运行时插件和编辑器基础。核心通过自己的抽象接口使用可选实现，不 include 或链接具体插件。

## 插件清单

`cmake/plugins/runtime/<name>.cmake` 与 `cmake/plugins/editor/<name>.cmake` 分别登记插件。**一插件一份 CMake**，同一文件声明本插件目标、源码、依赖和创建入口；中央注册器只读取登记、解析选择和依赖，不替插件维护源码列表。

- `ENGINE_RUNTIME_PLUGINS`：请求启用的运行时插件列表，运行时依赖开启时默认 `resource_graph`，脚本运行开启时再加入 `script`；显式设置优先。
- `ENGINE_EDITOR_PLUGINS`：请求启用的编辑器插件列表，默认根据已启用依赖能力选择 `modeling`、`asset_build`、`asset_sync`、`mcp`；可显式设置为空。
- 登记器解析依赖闭包后创建目标；依赖先于使用者建立，未选且不在闭包内的插件不创建目标。
- 明确移除一个插件时，同时核对有无已选插件依赖它；不能一边依赖它、一边声称它已禁用。
- 宿主只装配已创建的插件目标，核心基础构建不能被未选业务插件的源文件、依赖或头文件阻塞。

清单在 `project()` 前解析，已选插件所需的 vcpkg feature 在工具链安装依赖前加入。当前默认和依赖如下；显式缓存值优先，修改已有构建配置时应同时传入需要的清单值。

| 插件 | 默认加入条件 | 声明的插件依赖 |
| --- | --- | --- |
| `runtime/resource_graph` | `ENGINE_ENABLE_RUNTIME_DEPS=ON` | 无 |
| `runtime/script` | `ENGINE_ENABLE_SCRIPT_RUNTIME=ON` | 无；需要运行时依赖与已准备的 V8 |
| `editor/modeling` | 非设备构建，编辑器或资源管线依赖开启 | 无；需要运行时及建模第三方依赖 |
| `editor/asset_build` | 非设备构建，资源管线依赖开启 | 无；需要资源管线基础 |
| `editor/asset_sync` | 非设备构建，脚本运行开启，且编辑器或资源管线依赖开启 | 无；显式选择只要求运行时依赖 |
| `editor/mcp` | 非设备构建，脚本运行及资源管线依赖均开启 | `editor/asset_build`、`editor/asset_sync` |

Android/iOS 构建拒绝编辑器插件。未知插件、依赖环、缺少前置配置或运行时插件反向依赖编辑器均在配置时失败。`modeling` 插件提供几何目标；编辑器基础存在时再提供建模命令目标，导入管线存在时接入源网格编译。MCP 与建模同时启用时建立建模工具适配目标；几何计算不因此依赖 MCP。

这是构建期插件装配；不据此声明已提供 DLL 热插拔、C++ 模块热重载或状态迁移。插件 CMake 开关与 V8 玩法热重载是不同机制。具体配置命令及实际验证范围见根 [README](../../README.md)。

## 核心与调用方

命名空间分别为核心 `engine`、编辑器 `engine::editor`、插件 `engine::plugins::<插件名>`。插件可向下调用核心与适用的编辑器接口；编辑器基础库不引用插件。具体装配只在宿主进行，不能由插件 CMake 给核心或编辑器追加反向链接。

建模的权威源网格由 `ModelingMeshImporter` 处理，宿主通过 `AssetPipelineSystem::set_importer` 将它交给编辑器注册表。基础 `MeshImporter` 保留普通网格导入；未启用插件时，遇到建模权威源明确失败。导入器身份参与增量指纹，移除插件后不能复用此前插件生成的缓存绕过检查。详见[建模资源导入](editor/class-diagrams/modeling-mesh-importer.md)。

资源核心保留目录、GUID/VFS、单项读取与解析。`ResourceLoader` 定义加载与查询接口；运行时 `resource_graph` 插件通过相同接口组合单项能力、加载依赖并共享资源。业务只请求顶级对象，依赖策略不进入 Engine、资源文件底层或 MCP。

MCP 插件中的协议目标只拥有描述、输入模型与回调。编辑器业务模块自行注册工具，场景命令通过文件通道进入编辑器主线程；组件继续提供普通 C++ 方法。超时返回原请求身份，客户端查询完成回执，不重复执行未确认操作。

V8 玩法和绑定位于 `plugins/runtime/script/`。核心 [ScriptRuntime](../runtime/class-diagrams/script-runtime.md) 只声明执行与状态契约；宿主通过工厂创建所选插件，世界统一管理生命周期。RuntimeHost 和 GameplaySession 不链接具体脚本插件。原生绑定实现统一接口并显式登记，公开声明维护于 [include/engine.d.ts](../../include/engine.d.ts)。Gameplay 是场景外项目入口；场景对象使用 Node + Component。热重载由玩法会话驱动，MCP 同步资源并控制开关，不直接重载 V8。

## 运行包与调试工具

建模纯算法虽然不依赖编辑器状态，仍属于编辑器建模插件：当前用途是生产模型源资源和运行 Mesh，正式运行包只读取编译结果。MCP 整体属于编辑器工具，通用协议与业务适配在插件内分目标保持依赖边界，不需要为了目录分层把协议装入游戏。

当前 Android 同步由电脑上的 MCP 经 adb 完成；设备不运行 MCP 服务。未来如需设备端调试插件，必须明确区分正式运行与调试运行的构建用途，正式包排除调试目标、接口和依赖。本轮不预建不存在的设备调试插件。

## 评审入口

- [脚本运行契约](../runtime/class-diagrams/script-runtime.md)与[V8 脚本插件](runtime/class-diagrams/script-system.md)。
- [资源加载接口](../runtime/class-diagrams/resource-loader.md)与[资源依赖图插件](runtime/class-diagrams/resource-graph.md)。
- [建模功能与边界](editor/modeling.md)、[建模编辑业务](editor/class-diagrams/modeling-commands.md)。
- [MCP 协议](editor/class-diagrams/mcp-system.md)、[编辑器 MCP](editor/class-diagrams/editor-mcp-tools.md)。
- [资源构建](editor/resource-build.md)、[资源同步](editor/class-diagrams/asset-sync-system.md)。
- [跨层完整引用关系](../class-diagrams/references.md)。
