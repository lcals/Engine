# 动画测试模型来源

- `assets/models/skeleton.fbx`、`assets/models/locomotions.fbx` 来源于 ozz-animation 的 `media/fbx/pab/`。
- 固定版本：`6cbdc790123aa4731d82e255df187b3a8a808256`。
- 仓库：<https://github.com/guillaumeblanc/ozz-animation/tree/6cbdc790123aa4731d82e255df187b3a8a808256/media/fbx/pab>。
- 原作者 ProAnimationBank 授权 ozz-animation 按项目 MIT 许可再分发，许可原文保留在 [models-license.md](models-license.md)。

上游目录说明原文：

> Files in this repository were downloaded from ProAnimationBank. The creator granted ozz-animation the right to redistribute them under project license.

`skeleton.fbx` 包含蒙皮人体 Mesh 和绑定骨架，其 Take 001 为静止姿态。`locomotions.fbx` 包含 jog、run、walk 三个运动片段；本项目 meta 指向同一 skeleton 子资源，避免复制人体网格。

该人体源文件的法线与大部分三角面绕序相反，造成受光面呈黑色。本项目在 `skeleton.fbx.meta` 显式设置 `normals = "generate"`，由 ufbx 按网格拓扑重新生成平滑法线；源 FBX 保持不变，其他模型默认保留源法线。

FBX 导入当前生成 Model、Mesh、Skeleton 和 Animation。源材质与源贴图尚未转换，Model 采用引擎内置 PBR 材质，场景可覆盖项目材质。仅支持线性/刚性蒙皮；双四元数、混合形状和几何缓存明确返回不支持。
