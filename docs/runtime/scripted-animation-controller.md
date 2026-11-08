# 脚本动画状态机与资源重定向

动画资源保存动作与骨架，状态机脚本组织转换，业务提交参数，C++ 动画系统执行姿态计算。公开类型和方法以[动画脚本接口](../plugins/runtime/class-diagrams/animation-script-api.md)及 [include/engine.d.ts](../../include/engine.d.ts) 为准。

## 职责与所有权

| 层级 | 职责 | 状态归属 |
| --- | --- | --- |
| 动画资源 | Model、Skeleton、Animation、重定向 profile、IK 定义、挂点与事件轨 | 正式 GUID 资源；不包含项目状态转换规则 |
| TS 状态机 | 状态、同步条件、优先级、打断和播放请求 | 同一 Run 共享只读定义，每个 Animator 独立参数、状态、时钟和触发器 |
| 业务 TS | 提交速度、落地、受伤等参数与一次性信号 | 项目业务；不自行逐帧驱动状态机 |
| AnimationSystem | Ozz 采样、可选重定向、混合、IK、最终矩阵与事件收集 | 原生实例状态和姿态输出 |

共享定义与条件闭包不得保存某个角色的可变状态或原生借用。角色临时值进入实例上下文；停止或重载销毁旧 Run 的定义、上下文与订阅，不跨虚拟机保存脚本对象。实例隔离不等于已完成大规模角色的性能验收。

## 状态机使用

1. 通过 `AnimationStateMachine`、状态和同步条件建立共享定义，登记参数与转换；定义完成后保持只读。
2. 先加载动作、骨架和实际使用的适配资源，再调用 `Animator.SetStateMachine(definition, actionAliases)` 绑定。动作别名使用正式资源路径。
3. 业务通过 `SetParameters`、`Trigger` 提交输入，ScriptSystem 自动推进。转换使用 `PlayAnimation` 原子请求，失败保留旧动作。
4. 状态机取得播放控制权时，业务直接 `Play/CrossFade/SetSpeed` 返回失败；调用 `ClearStateMachine` 解绑后恢复手动控制。暂停、停止与旧句柄按组件和 Run 身份处理。

定义、转换顺序和失败语义见[状态机类图](../plugins/runtime/class-diagrams/animation-state-machine.md)。公开用例见[共享动画状态机](../../projects/showcase/docs/animation-state-machines.md)。

## 动画资源重定向

`.retarget` profile 是独立 GUID 资源，显式关联源骨架、目标骨架、关节映射和参考姿态。源动作与源骨架保持独立，目标保留自己的绑定姿态和蒙皮权重；运行时转换姿态，不覆盖源资源。

核心声明 [AnimationRetargeter 契约](class-diagrams/animation-retargeting.md)，宿主通过可空工厂选择提供者，由 AnimationSystem 独占。默认实现为可选 [animation_retarget 插件](../plugins/runtime/class-diagrams/animation-retarget.md)，采用固定版 Wicked Engine 的参考姿态转换，来源及 MIT 许可见[第三方声明](../../third_party/notices/wicked-animation-retarget.md)。无提供者时普通动画仍可播放；显式请求重定向返回不可用。配置或初始化失败不静默降级。

不可变绑定按资源及其代际共享，任务只借用稳定输入并独占输出，释放前完成在途计算。profile 导入由编辑器 `animation_tools` 负责；离线角色准备见[角色制作规划](../editor/character-production.md)。

## IK 与骨骼挂点

原生顺序为源采样、可选重定向、两动作混合、有序 Aim/TwoBone IK、最终模型矩阵、挂点更新和 GPU palette。约束目标、参考空间和顺序由调用方明确提供；不建立跨 Actor 的自动依赖求解。

`ik_enabled` 与 `BoneAttachment.follow_enabled` 默认关闭。`GetSocketPose` 返回最终挂点变换副本，失效时返回空值；挂件跟随使用 BoneAttachment，不把派生骨姿态写回源动画。目标和身份、同帧顺序及失效语义见[IK 与挂点类图](class-diagrams/animation-rigging.md)。

## 动画事件与注入

源事件轨与 sockets、IK 定义保存在 FBX 邻接 meta；制作命令见[动画制作接口](../plugins/editor/class-diagrams/animation-authoring.md)。`reimportPending` 只表示等待导入，不表示资源已经发布。

TS 使用 `SetEventMarkers` 添加实例事件轨，使用 `SubscribeEvents/UnsubscribeEvents` 管理监听。核心按播放区间收集纯值事件，宿主在动画和挂点发布后的主线程安全点分发；VM 对象、订阅和项目业务留在脚本插件。Seek 不补发经过的事件，跨循环、过渡和去重以[事件契约](class-diagrams/animation-events.md)为准。

Run、角色、组件、播放和资源身份参与失效检查。解绑或销毁接收端前停止分发并清理订阅，不从采样 worker 调用项目回调，不以状态进入/退出回调代替时间事件。

## 能力范围

支持普通状态与两动作过渡、显式参考姿态重定向、Aim/TwoBone IK、命名 socket 和标量时间事件。层级状态机、混合树、遮罩/加法层编辑、接触脚锁、全身 IK、RootMotion 驱动及跨 Actor 目标依赖不在当前范围。场景移动仍由物理、导航或业务负责。

编辑器通过 MCP 和配置文件制作，窗口提供预览/运行；未提供时间线或状态机图编辑器。公开范例 Idle 使用 walk 首帧停播，仅用于流程演示。

## 参考来源

以下固定源码用于结构参考，不是新增构建依赖，也不代表已经编译或运行对应上游。实际采用代码另有第三方声明。

| 来源 | 固定提交 | 主仓库许可 | 实际可参考范围 |
| --- | --- | --- | --- |
| [ezEngine](https://github.com/ezEngine/ezEngine) | `59ca5986e0cf07d94fc360e426e102a60ca60d81` | MIT | 状态/转换扩展、脚本状态生命周期、共享定义与实例隔离、姿态命令及 Ozz 执行 |
| [RavEngine](https://github.com/RavEngine/RavEngine) | `f5853bcb50a5b80f79fcfae515f06cc2c7a4db48` | Apache-2.0 | 跨状态淡入淡出、Mask、普通与加法层的 Ozz 接法 |
| [The Forge](https://github.com/ConfettiFX/The-Forge) | `cd5046893faba2dc7869243873bf01f02a6f0df9` | Apache-2.0 | ClipController、相位/速度同步、多层、Mask、Aim/TwoBone IK 与姿态重算 |
| [Ant](https://github.com/ejoy/ant) | `f32fbd4811e90c2433bf159aa40fb8f8aceb8b58` | MIT | 脚本控制、ECS 播放/采样阶段、Ozz 绑定和蒙皮衔接 |

若后续移植具体实现，须另外保留对应文件的版权、来源和适用许可；不把主仓库许可概括为其所有依赖和美术资产的许可。
