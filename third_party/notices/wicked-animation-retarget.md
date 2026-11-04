# Wicked Engine 动画重定向参考

本引擎的 `plugins/runtime/animation_retarget/animation_retarget.cpp` 参考以下固定开源实现的模型空间参考姿态转换关系：

- 仓库：<https://github.com/turanszkij/WickedEngine>
- 固定提交：`c433ae7b9d433df106fd53970b625a710744232b`
- 准备参考变换：[Scene::RetargetAnimation](https://github.com/turanszkij/WickedEngine/blob/c433ae7b9d433df106fd53970b625a710744232b/WickedEngine/wiScene.cpp#L9329)
- 运行时转换：[Retargeting transfer](https://github.com/turanszkij/WickedEngine/blob/c433ae7b9d433df106fd53970b625a710744232b/WickedEngine/wiScene.cpp#L2610)
- 许可：MIT，原文如下。

适配说明：上游使用 DirectXMath 的行向量矩阵与场景骨骼实体；本引擎使用既有 bx 四元数数学、完整源／目标参考模型姿态和目标父模型姿态，按列向量约定转换。普通目标骨骼保留目标参考平移与缩放，避免源动画改变目标肢体长度；hips 位移使用显式策略和比例。输入通过 GUID 资源、完整骨路径与 schema 局部 TRS 校验，准备结果不可变且可以并行共享，不复制上游场景容器、Humanoid 枚举或资源管理流程。

当前适配要求单根骨架、有限正向均匀缩放及显式参考姿态；它不提供全身 IK、接触保持、脚锁或 RootMotion。源／目标参考模型基的旋转参与对齐，源／目标父链分别求值；并非逐关节局部旋转差或动作数据的简单重排。

```text
The MIT License (MIT)

Copyright (c) 2026 Turánszki János

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```
