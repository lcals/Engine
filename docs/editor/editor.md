# 编辑器模块

编辑器使用 C++ 与 Dear ImGui docking，依赖运行时能力；运行时不反向依赖编辑器。项目路径由宿主先校验，`EditorEnvironment` 集中配置 App 路径并准备会话内置资源快照，`EditorSystem` 借用已就绪的环境和玩法会话。

编辑器建立 SDL3 窗口及 ImGui 上下文，默认空白待机。MCP `start_project` 启动项目，在同一窗口显示场景；`stop_project` 结束玩法并恢复空白。会话负责释放场景和项目 GPU 资源，窗口与图形设备保持存活。项目控制在编辑器主线程执行，状态通过持久运行目录对外发布，详见[项目控制](#渲染与项目控制)。

编辑器功能通过 MCP 提供资源导入、完整版本同步、玩法启停、热重载开关和状态查询。脚本及资源重载由运行时执行，MCP 不实现重载。模型、业务控制和界面呈现须分别归属对应模块，界面调用已有能力，不复制业务实现。

完整编辑器界面、多窗口布局、C++ 编辑器模块与 TypeScript 编辑器插件热重载尚未实现。桌面目标覆盖 macOS、Windows 和 Linux；Dear ImGui Multi-Viewports 的 Wayland 限制与部分 X11 窗口管理器兼容性须在接入多窗口时验证。

## 窗口与编辑器

```sh
cmake --workflow --preset ninja-debug
cmake --workflow --preset ninja-release
```

也可以逐步执行：

```sh
cmake --preset ninja-debug
cmake --build --preset ninja-debug
ctest --preset ninja-debug
```

Ninja 预设写入独立的 `build/ninja-debug/`、`build/ninja-release/`，避免与旧生成器的构建目录冲突。`debug` / `release` 预设采用平台默认生成器，保留在 `build/debug/`、`build/release/`；Windows 通常为 Visual Studio 工程，Linux / macOS 通常为 Makefiles。

程序目标名为 `engine_app`。Linux / macOS 的 Ninja 预设下运行：

```sh
./build/ninja-debug/engine_app
# 渲染 120 帧后自动退出，适合冒烟验证。
./build/ninja-debug/engine_app --frames 120
```

Windows 的 Ninja 预设对应 `.\build\ninja-debug\engine_app.exe`；Visual Studio 预设对应 `.\build\debug\Debug\engine_app.exe`。macOS / iOS 使用 Metal，Windows 使用 DirectX 12，Android / Linux 使用 Vulkan；所选后端不可用时返回失败，不以 Noop 或其他后端回退代替成功。

编辑器宿主由 `editor-deps` 或 `ninja-engine-debug` 等启用编辑器的预设提供，启动时传入项目：

```sh
./build/ninja-engine-release/editor_app --project projects/showcase
```

该入口验证项目并建立引擎、ImGui 上下文；完整引擎预设还会构建并同步项目脚本，等待 MCP `start_project` 后运行。尚未绘制完整编辑器界面。

## 渲染与项目控制

- 同一编辑器窗口在空白与项目画面间整体切换。场景、节点和组件归当前运行；共享窗口与图形设备保留到编辑器关闭。停止会清除最后一帧，重复启动不会创建第二套实例。
- 桌面 MCP 提供无参数 `start_project`、`stop_project`（客户端简写 `start`、`stop`）。目标固定为当前 `--project` 和 `--runtime-dir`，须先启动编辑器。Android 工具提供同步、热重载和状态查询；Android 由平台宿主启动 Gameplay，不提供编辑器项目启停。
- `editor-command.json` / `project-status.json` 位于持久运行目录。请求带会话与编号，由编辑器主线程执行；状态提供 `projectState`、`session`、`lastRequestId`、`lastRequestSucceeded`。场景和 GPU 数量查询分别属于场景与渲染系统，不从 Engine 转发。
- 启停等待最多 10 秒；超时的 `activation: pending` 表示尚未确认，**不是取消**。查询同一请求的最终状态后再操作。旧编辑器会话请求不会在新会话重放。
- 一个运行目录供一个编辑器实例使用；并行编辑器使用不同 `--runtime-dir`。MCP 通过 `editor-command.lock` 的操作系统文件锁串行化提交，进程退出自动释放；请求完成后保留按会话和请求编号归档的回执，使用 `get_editor_receipt` 查询。

## 编辑器场景预览

编辑器可直接预览已导入的 `.scene` 资源，不启动 Gameplay、角色控制、物理模拟或音频播放；窗口显示场景，不添加编辑器 UI。调用 `open_scene_preview` 后，既有 MCP 节点、组件和材质命令直接编辑该预览，下一帧显示结果。

| 工具 | 参数与作用 |
| --- | --- |
| `open_scene_preview` | `path` 为完整逻辑资源路径；可选 `camera` 对象含 `position`、`rotation`、`speed`；替换当前场景并丢弃未保存内存编辑 |
| `get_scene_preview` | 无参数；查询 active、path、sceneId、camera、modified、reloadPending、lastError |
| `set_preview_camera` | 至少一项 position、rotation、speed；修改独立镜头，不改变场景内容 |
| `refresh_scene_preview` | 可选 `discardChanges`，默认 false；先增量导入源资源再重建预览，丢弃未保存编辑须显式传 true |
| `close_scene_preview` | 无参数；丢弃未保存内存编辑，释放预览场景和 GPU 资源，恢复空白窗口 |

镜头默认复制场景活动相机的世界姿态和投影；无活动相机时使用位置 `[0,2,5]`、旋转 `[0,0,0]`。它由编辑器独立持有，不进入项目场景节点、`get_scene` 或 `save_scene`。旋转为 `[pitch,yaw,roll]` 度，移动速度为 0.01..1000 米/秒，默认 5；按住鼠标右键后转动视角，WASD 移动，Q/E 世界竖直升降，Shift 四倍加速。移动镜头不标记场景已编辑。环境光、雾、清屏色从场景环境组件读取，曝光、色调映射、AO、HDR 与 Bloom 从场景活动相机的后处理组件读取；隐藏预览镜头只改变观察视角，不覆盖这些参数。无需先执行 Gameplay，预览和运行使用同一份场景设置。

源资源或项目设置变化后，编辑器监听并增量导入；外部变化在无未保存编辑时自动刷新预览。有未保存的 MCP 编辑时保留当前画面并标记 `modified`、`reloadPending`，调用方可明确 `refresh_scene_preview` 并传 `discardChanges=true` 接受外部版本。`save_scene` 保存到当前源路径后，预览校验本次源文件、meta 摘要及目录变化，仅由自身保存引起的导入直接更新资源目录，保留当前场景、镜头和撤销/重做历史；保存后继续编辑也不会被这次导入清除。

同一场景代次内，已成功保存的场景及导出资源若被外部修改或丢失 meta，再次保存会报告冲突并保留外部文件；需刷新接受新版本，或恢复原文件后再保存。显式打开其他场景或关闭预览会丢弃未保存内存编辑，不会自动保存；回执的 `discardedChanges` 说明是否丢弃了修改。刷新、关闭和切换场景后，继续编辑前重新查询 sceneId/nodeId。

```sh
python3 scripts/mcp_client.py --server build/ninja-engine-release/mcp_app --project projects/showcase --runtime-dir build/editor-runtime call open_scene_preview --arguments '{"path":"scenes/apartment.scene"}'
python3 scripts/mcp_client.py --server build/ninja-engine-release/mcp_app --project projects/showcase --runtime-dir build/editor-runtime call set_preview_camera --arguments '{"position":[0,2,5],"rotation":[0,0,0],"speed":5}'
python3 scripts/mcp_client.py --server build/ninja-engine-release/mcp_app --project projects/showcase --runtime-dir build/editor-runtime call get_scene_preview
python3 scripts/mcp_client.py --server build/ninja-engine-release/mcp_app --project projects/showcase --runtime-dir build/editor-runtime call close_scene_preview
```

这些工具只在桌面编辑器目标注册，沿用主线程命令通道及原执行回执；超时仍按 pending 查询，MCP 不直接创建场景、驱动镜头或执行导入。类图见[场景预览 MCP](../plugins/editor/class-diagrams/scene-preview-mcp-tools.md)。

效果调试使用 `scripts/preview_visual_review.py`，按项目 `settings/visual-review.json` 的固定路线移动隐藏预览镜头、等待真实截图完成，并保存机位、渲染参数、源文件哈希和回执。`--compare` 可生成相同路线的前后滑动对比单文件报告，产物位于项目 `cache/visual-reviews/`。工具仅使用 Python 3.11+ 标准库，不启停编辑器，也不修改场景；路线格式、运行命令和分析顺序见[场景效果巡览](visual-review.md)。
