# 动画测试模型来源

- `assets/models/skeleton.fbx`、`assets/models/locomotions.fbx` 来源于 ozz-animation 的 `media/fbx/pab/`。
- 固定版本：`6cbdc790123aa4731d82e255df187b3a8a808256`。
- 仓库：<https://github.com/guillaumeblanc/ozz-animation/tree/6cbdc790123aa4731d82e255df187b3a8a808256/media/fbx/pab>。
- 原作者 ProAnimationBank 授权 ozz-animation 按项目 MIT 许可再分发，许可原文保留在 [models-license.md](models-license.md)。

上游目录说明原文：

> Files in this repository were downloaded from ProAnimationBank. The creator granted ozz-animation the right to redistribute them under project license.

`skeleton.fbx` 包含蒙皮人体 Mesh 和绑定骨架，其 Take 001 为静止姿态。`locomotions.fbx` 包含 jog、run、walk 三个运动片段；本项目 meta 指向同一 skeleton 子资源，避免复制人体网格。

该人体源文件的法线与大部分三角面绕序相反，造成受光面呈黑色。本项目在 `skeleton.fbx.meta` 显式设置 `normals = "generate"`，由 ufbx 按网格拓扑重新生成平滑法线；源 FBX 保持不变，其他模型默认保留源法线。

源 `walk` 动作按 30 Hz 采样一个周期的 41 个姿态时，人体网格最低 Y 在 0 秒为 `-0.017362623 m`，在约 1.133333 秒为 `-0.037193744 m`。生成缓存重建的结果分别为 `-0.017362621 m` 和 `-0.037193741 m`，表明这部分足底穿地来自源动作，而非导入后的绑定变换。样例 `assets/scripts/modules/courtyard.ts` 将 Avatar 相对 Player 上移 `0.05 m`，为近景演示补偿模型放置高度；Player 的物理胶囊、控制器、骨骼绑定和动画数据保持不变。

该固定偏移不等于足部 IK 或地面吸附，不会让每帧双脚贴地，也不保证其他动作和台阶上的脚底接触。演示可通过现有 MCP `set_animator` 选择 `project/models/locomotions.fbx/animations/walk` 或 `project/models/locomotions.fbx/animations/run`，再用 `play_animation`、`pause_animation`、`seek_animation` 控制；无需新增接口。

FBX 导入当前生成 Model、Mesh、Skeleton 和 Animation。源材质与源贴图尚未转换，Model 采用引擎内置 PBR 材质，场景可覆盖项目材质。仅支持线性/刚性蒙皮；双四元数、混合形状和几何缓存明确返回不支持。

编辑器仍可浏览全部动作子资源。示例构建清单仅引用 `locomotions.fbx/animations/walk`，不会因使用 skeleton.fbx 模型而带入其静止 Take，也不会输出 jog/run 或 locomotions.fbx 的空模型层级。各项通过独立 GUID 和真实依赖打包，源 FBX 不会被裁剪或改写。
