# 编辑器配置

配置按职责区分：`EngineConfig` 保存窗口等运行时配置；项目 `settings/` 保存 TypeScript 工程、依赖锁文件和资源导入设置；界面语言等编辑器偏好不属于运行时启动条件。

`settings/import.toml` 的 `import_threads` 控制编辑器 oneTBB 任务域的并发上限，默认 8，允许 1–64。缺少该文件时使用默认值，内容或范围非法时导入失败。该值不是独占线程数，也受进程预算限制；本轮在导入前、任务域空闲时配置，忙碌时改额度明确失败。项目参数归资源管线，进程总并行预算由宿主决定。

界面偏好管理与默认值写入尚未实现；项目级限帧、导入与运行预算已由相应 TOML 配置。已实现配置与状态契约见[配置与状态类图](../runtime/class-diagrams/types.md)，项目目录约束见[项目与出包](project.md)。

## 帧率上限

核心 `Engine` 提供整帧限速，运行时默认 `-1` 表示不限帧。正整数为每秒帧数上限，`0`、小于 `-1` 的值或脚本非整数输入被拒绝，原设置保持不变。宿主在完整帧开始和结束调用限帧接口，休眠只补足剩余帧预算，超预算不额外等待；下一帧真实时间差包含等待时间，物理固定步长不变。

项目编辑器配置放 `settings/editor.toml`，文件或字段缺失时默认 60，格式或数值错误拒绝启动：

```toml
[editor]
target_frame_rate = 60
```

该上限用于编辑器待机和场景预览。启动游戏前暂存当前上限并切到 `-1`，再执行项目入口；业务可通过脚本设置自己的上限。停止游戏、启动失败或运行异常停止后恢复暂存值，业务修改不写回编辑器配置。独立运行包不读取编辑器配置。

脚本在 `OnInitialize`、`Update` 或组件生命周期回调中使用：

```ts
import { Engine, Gameplay } from "engine";

export default class Game extends Gameplay {
    override OnInitialize(): boolean {
        return Engine.SetTargetFrameRate(45);
    }
}
```

模块顶层、构造与热重载候选预检阶段禁止修改上限；查询在这些阶段返回 `null`。限帧是上限而非保底帧率，设备负载和 VSync 仍可能使实际帧率更低。C++ 接口与所有权见[帧率限制](../runtime/class-diagrams/frame-limiter.md)。

## 运行设置与并行预算

`settings/runtime.toml` 与 `settings/parallel.toml` 由编辑器和构建工具读取，转换为核心配置；运行包消费汇总的 `settings/runtime.json`，不引入 TOML。默认值、平台档位与覆盖顺序见[运行并行配置](../runtime/parallelism.md)。
