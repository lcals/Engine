# 资源同步与热重载

## 桌面脚本与同步

桌面编辑器、Android 和 iOS 共用运行时脚本和热重载实现。宿主需有可执行的 Node.js、npm 和 Python 3；Node.js 仅用于编译工具，不进入玩法虚拟机。先安装项目锁定的 TypeScript，再构建完整引擎预设：

```sh
npm ci --prefix projects/showcase/settings/typescript
cmake --workflow --preset ninja-engine-release
```

从仓库根目录启动编辑器，指定一个持久运行目录，让编辑器和 MCP 工具访问同一份缓存与状态：

```sh
./build/ninja-engine-release/editor_app --project projects/showcase --runtime-dir build/editor-runtime
```

Windows 将可执行文件写为 `build/ninja-engine-release/editor_app.exe`；Debug 仅作编译验证。可加 `--frames 120` 做有限帧运行。不传 `--runtime-dir` 时使用按项目区分的系统应用数据目录，启动日志会打印实际路径。

编辑器先导入 `assets/`：读取 `.meta` 身份、解析资源描述、编译 TS 与目标平台 Shader，并生成 `cache/gen/<完整 GUID>`。SQLite 在 `cache/assets.sqlite` 记录输入哈希、依赖、工具版本与生成版本；再次导入只转换变化及受影响资源，失败保留上一版完整输出。随后建立本会话不变的内置资源快照并发布持久资源版本，全部引擎系统就绪后保持空白，等待 MCP 的 `start_project`。启动时示例 Gameplay 调用 `Scene.Load("scenes/launcher.scene")`，显示项目场景菜单，停止时释放场景及本次运行 GPU 资源。

编辑器运行时，若在另一终端修改了项目源脚本或其他资源，可执行：

```sh
python3 scripts/mcp_client.py --server build/ninja-engine-release/mcp_app --project projects/showcase --runtime-dir build/editor-runtime status
python3 scripts/mcp_client.py --server build/ninja-engine-release/mcp_app --project projects/showcase --runtime-dir build/editor-runtime start
python3 scripts/mcp_client.py --server build/ninja-engine-release/mcp_app --project projects/showcase --runtime-dir build/editor-runtime sync
python3 scripts/mcp_client.py --server build/ninja-engine-release/mcp_app --project projects/showcase --runtime-dir build/editor-runtime hot-reload on
python3 scripts/mcp_client.py --server build/ninja-engine-release/mcp_app --project projects/showcase --runtime-dir build/editor-runtime status
python3 scripts/mcp_client.py --server build/ninja-engine-release/mcp_app --project projects/showcase --runtime-dir build/editor-runtime hot-reload off
python3 scripts/mcp_client.py --server build/ninja-engine-release/mcp_app --project projects/showcase --runtime-dir build/editor-runtime stop
```

Windows 的 `--server` 使用对应 `mcp_app.exe`。客户端通过 stdio 启动 `mcp_app`，完成 MCP 初始化并调用一次工具；也可将这个可执行文件及相同参数配置到其他 MCP 客户端。`sync` 同时导入项目资源和同步完整版本；热重载开关只写控制请求，切换由引擎在帧边界执行。同步成功或返回 `pending engine decision` 不等于新资源版本已经启用，应通过 `status` 的 `activeRevision`、`availableRevision`、`running` 和 `lastError` 确认。运行资源版本是引擎最近一次上报；编辑器另有带心跳的项目状态，超过 5 秒无更新会标记离线。

热重载默认关闭；同一持久目录已有控制文件时读取其设置。关闭期间仍可同步新版本，当前实例和模块快照保持不变；开启后应用最新完整版本。候选脚本编译或静态链接失败时旧实例继续运行；新模块执行或 `OnInitialize` 失败会停止此次玩法并报告错误，不承诺撤销副作用或保留任意 JS 状态。`Update` 异常会记录错误、结束本次 Gameplay 并清理场景，编辑器回到待机。停止项目后，同步或开启热重载不会自动启动项目。


## 版本发布与完整目录

持久版本保存在 `persistentDataPath/assets/versions/<revision>/`，只含 `catalog.bin` 和其引用的散文件或合并包，SQLite 不进包。全部内容核对大小及 FNV-1a 64 哈希后，才原子发布 `assets/available.json`。运行开始先选择完整持久版本，未发布时使用内置版本；运行模块和场景持有实际加载资源的共享引用；核心目录只保存元数据，不预读所有实体。TS 相对 import 通过清单中的模块路径映射到 GUID，已删除模块不会回退到旧包体。绝对路径、越界路径和越界符号链接会被拒绝。

MCP 同步整个资源目录，包括场景、Mesh、材质、Shader、脚本及其他普通资源；按 GUID 将完整版本发布到本机、Android 调试包或 iOS 应用容器的持久目录，通过 `script-control.json` / `script-status.json` 控制引擎；不提供独立网络服务或差量同步。`ENGINE_ENABLE_SCRIPT_RUNTIME` 在完整桌面和 Android 预设均开启；它决定 `runtime/script` 的默认选择，显式插件清单优先；关闭插件后核心和宿主不链接脚本虚拟机。单独开启 V8 依赖时由独立测试验证上游库，不创建伪脚本系统。Android 传输使用 adb，具体命令见下节。

## Android 资源同步与热重载

| App 接口             | Android 指向                                                 |
| -------------------- | ------------------------------------------------------------ |
| `assetsPath`  | `apk:/assets`，由 ResourceSystem 读取 APK 的逻辑路径 |
| `persistentDataPath` | 应用私有 `files/`，保存日志、版本和控制状态，重启仍保留      |
| `cachePath`          | 应用私有 `cache/`，存放可重建的临时数据                   |

安装 Debug APK 后即可首次同步完整资源，无需先启动应用导出清单；查询运行状态或使用热重载时再启动应用。电脑上的 MCP 使用明确的设备序列号与包名，拒绝隐式选择设备：

```sh
python3 scripts/mcp_client.py --server build/ninja-engine-release/mcp_app --project projects/showcase --android-serial emulator-5580 --android-package org.engine.minimal status
python3 scripts/mcp_client.py --server build/ninja-engine-release/mcp_app --project projects/showcase --android-serial emulator-5580 --android-package org.engine.minimal hot-reload on
# 修改项目 assets 下的脚本、场景、材质等资源后同步；无需重新打 APK。
python3 scripts/mcp_client.py --server build/ninja-engine-release/mcp_app --project projects/showcase --android-serial emulator-5580 --android-package org.engine.minimal sync
python3 scripts/mcp_client.py --server build/ninja-engine-release/mcp_app --project projects/showcase --android-serial emulator-5580 --android-package org.engine.minimal status
```

adb 不在 PATH 时添加 `--adb <adb可执行文件>`。真机使用 `adb devices` 中的实际序列号；需要设备已经授权 adb 调试。MCP 运行在电脑，通过 `run-as` 写入该调试包的私有持久目录，手机不安装 MCP 服务。此通路仅支持 debuggable APK；Release 保持不可调试，同步请求会返回明确错误。

设备同步先上传暂存版本、读回验证大小与哈希，再改名为不可变版本，最后原子发布 `available.json`。发布过程使用设备目录锁；发布前失败保留旧的可用版本。最终发布回执丢失会报告未确认，此时应查询设备状态。每个资源版本必须包含完整的 `catalog.bin` 及其定位信息引用的散文件或合并包；运行时优先使用持久版本，没有持久版本时读取 APK 内置目录。同一次玩法运行固定使用一份完整 catalog，不能把不同版本的场景、材质和脚本混用；重载校验失败时保留原活动版本。删除资源在下一份完整 catalog 中不再出现，不会从 APK 意外恢复。可写版本在 `persistentDataPath`，不会因临时缓存清理而丢失。

热重载由引擎逐帧决定是否执行；关闭开关仍可同步文件，开启后加载最新版本。用 `status` 确认 `activeRevision` 与 `availableRevision` 相同且 `running=true`，同时检查 `lastError`。APK 更新或普通重启不会覆盖现有可用资源版本；要恢复包内资源，应先停止应用并由开发者明确清理对应同步数据。


## iOS 资源同步与热重载

iOS 使用同一 catalog、不可变版本、发布清单和 GameplaySession；场景、Mesh、材质、Shader、脚本及其他资源一起同步。传输只在桌面 editor/mcp 中实现，正式游戏不因此增加网络权限或运行态 MCP 服务。

- 真机显式传 `--ios-device`，通过已配对、受信任设备的 House Arrest / AFC 访问选定开发应用容器；权限由设备检查，不退回 Documents 或其他应用。该通路需要应用已安装并允许开发容器访问。
- 模拟器显式传 `--ios-simulator`；仅 macOS 宿主可用。先启动指定模拟器并安装应用，不接受 `booted` 或自动选择设备。
- 两种目标都必须提供 `--ios-bundle-id` 和与编译配置相同的 `--ios-application-name`。默认公开范例分别为 `org.engine.engine-showcase`、`engine-showcase`，持久目录是所选容器内的 `Library/Application Support/engine-showcase/`。

先用 `xcrun simctl list devices` 查明实际 UDID，将其赋给 `IOS_UDID`；下例从仓库根执行：

```sh
python3 scripts/mcp_client.py --server build/ninja-engine-release/mcp_app --project projects/showcase --ios-simulator "$IOS_UDID" --ios-bundle-id org.engine.engine-showcase --ios-application-name engine-showcase build --target ios
python3 scripts/mcp_client.py --server build/ninja-engine-release/mcp_app --project projects/showcase --ios-simulator "$IOS_UDID" --ios-bundle-id org.engine.engine-showcase --ios-application-name engine-showcase sync --source cache/build/ios
python3 scripts/mcp_client.py --server build/ninja-engine-release/mcp_app --project projects/showcase --ios-simulator "$IOS_UDID" --ios-bundle-id org.engine.engine-showcase --ios-application-name engine-showcase hot-reload on
python3 scripts/mcp_client.py --server build/ninja-engine-release/mcp_app --project projects/showcase --ios-simulator "$IOS_UDID" --ios-bundle-id org.engine.engine-showcase --ios-application-name engine-showcase status
```

真机把 `--ios-simulator` 换为 `--ios-device` 并使用真机 UDID；自定义包标识和产品名须同步替换。模拟器工具默认 `xcrun`，必要时通过 `--xcrun` 指定完整路径。设备目标的 `build` 默认使用对应平台；显式 `--target` 仍可构建其他目标，但 iOS 同步拒绝非 iOS catalog。省略 sync 的 `--source` 时先按 iOS 增量导入再同步完整目录；预构建合并包可减少传输文件数。源设置、meta、SQLite 和未引用缓存不进入设备版本。

每次同步校验选定应用、获得发布锁、上传本次暂存版本并按最多 1 MiB 分块读回校验大小和哈希，然后原子提交不可变版本及 `available.json`；发布前失败保留旧清单，不修改运行版本。锁使用主机或 AFC 的 flock，持有连接关闭时释放；常驻 `assets/.sync-lock/owner.lock` 文件不表示仍被占用。最终改名结果不明时回执明确报告未确认，应查询设备，不自动宣称回滚或重复发布。

`status` 的设备 UDID、bundle identifier、application name 和运行目录来自选定目标；`availableRevision` 来自设备发布清单，`activeRevision`、`running`、`runId`、`lastError` 来自设备最后一次引擎上报。`state=lastReportedRunning` 不是实时进程心跳。只有新版本被引擎接受后，运行版本才与可用版本一致。重启同样保留发布版本身份，不把 catalog 内容版本误报为发布版本。运行版本在整个场景和脚本切换期间保持完整快照，失败不混用旧包资源。

状态查询不创建运行目录；未上报返回 unreported / running=null，最近停止返回 lastReportedStopped，不据此判断应用在线。详细字段见[状态与只读查询](class-diagrams/project-mcp-tools.md#ios-状态与只读查询)。同步成功、最近运行记录和实时测试端点是不同的完成边界；候选预检失败保留旧运行，进入替换后新入口初始化失败会停止玩法，不承诺完整回滚。

设备目标不提供本机编辑器的 start_project/stop_project；应用安装和启动继续使用 Xcode/平台工具。已完成的实际验证范围见[测试文档](../../testing.md#ios-原生运行包)，职责及方法见[项目 MCP 类图](class-diagrams/project-mcp-tools.md#ios-应用容器同步)。
