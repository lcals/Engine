# 应用路径、日志与持久化

`App` 的静态只读路径具有以下含义：

| 脚本接口                 | 用途                                                                      |
| ------------------------ | ------------------------------------------------------------------------- |
| `App.assetsPath`  | 应用资源读取根；桌面为程序旁 `assets/`，Android 为 APK `assets/`，编辑器映射项目转换缓存                  |
| `App.persistentDataPath` | 持久数据根目录，保存同步版本、日志、控制与状态；可由 `--runtime-dir` 指定 |
| `App.cachePath`          | 平台临时缓存目录，可重建；不承载需要跨启动保留的同步版本                  |

`LogSystem` 默认将日志写入 `persistentDataPath/logs/engine.log` 并通过 SDL 输出到平台日志（桌面 stderr、Android logcat）；单文件达到 4 MiB 时轮转，保留 3 个旧文件。项目编译输出 `cache/gen/` 与平台临时 `App.cachePath` 是不同目录。

`Storage` 提供通用字符串 KV，数据位于 `persistentDataPath/userdata/kv.json`。在 Gameplay 或 UI 回调内使用 `GetString`、`SetString`、`HasKey`、`DeleteKey`，由 `Save(): boolean` 显式落盘；`isReady` 和 `lastError` 分别报告加载状态与错误。不存在的 key 返回 null 且错误为空，文件损坏或读取失败会阻止后续写入。KV 首次访问时加载，每次 Run 独占缓存，停止与热重载不自动保存。编辑器默认持久目录按项目隔离，显式 `--runtime-dir` 应使用项目专属目录。

```ts
import { Debug, Storage } from "engine";

// 在有效玩法回调内低频保存；项目维护 JSON 结构和业务数据版本。
if (!Storage.SetString("progress", JSON.stringify({ version: 1, gold: 100 })) || !Storage.Save()) {
    Debug.LogError(Storage.lastError ?? "Storage is unavailable");
}
const text = Storage.GetString("progress");
if (Storage.lastError) Debug.LogError(Storage.lastError);
// text 为 null 且 lastError 为空表示尚无该 key；JSON 解析和迁移由项目决定。
```

原生 KV 使用固定数据文件、UTF-8 字符串与同目录临时写入后替换；key 为 1–256 字节，整个序列化文件最多 4 MiB。保存失败保留旧文件，不暴露任意路径、不实现游戏字段或隐式迁移。大块世界快照继续由原生模块整理。完整契约见 [KeyValueStore](class-diagrams/key-value-store.md) 与 [脚本接口](../plugins/runtime/class-diagrams/storage-script-api.md)。
