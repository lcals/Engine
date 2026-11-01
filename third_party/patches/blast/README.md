# Blast 5.1 可移植性补丁

上游固定 `NVIDIAGameWorks/Blast@636e7eb43b8d41e44c40eb1b4799bfd027f17ba7`。

`portable-authoring.patch` 仅修正平台/标准库兼容问题：

- `NvBlastExtApexSharedParts.cpp` 的 Vec3 → Vec4 批量转置使用上游已有 `Vec4V_From_Vec3V`。SSE 下这两种类型是同一寄存器类型，Apple ARM 标量实现中则不同；显式转换保留原有三分量转置算法。
- `NsArray.h` 使用标准 `<type_traits>`，移除 Android NDK libc++ 不提供的 TR1 头。非 MSVC 的 POD 判断保留 `is_trivial && is_standard_layout` 语义；MSVC 原有 `is_trivially_copyable` 判断不变。
- `NvBlastExtAuthoringBondGeneratorImpl.h` 显式包含 `NvBounds3.h`，其 `vector<NvBounds3>` 内联构造需要完整类型；不再依赖其他头的间接包含顺序。

- VHACD `FloatMath.cpp` 移除多余的非标准 `<malloc.h>`；已有 `<stdlib.h>` 提供该文件和 `FloatMath.inl` 所需的 `malloc/free`，不使用 Windows 专属的分配接口。

CMake 只将 Foundation 头和受影响的 Authoring 文件复制到构建目录应用补丁，以输入哈希避免重复写入，官方子模块不修改。补丁没有添加 PhysX、CUDA 或另一套破碎算法。原许可随子模块 `PACKAGE-LICENSES/` 保留。

## ExtStress 标量适配（步骤 4）

`portable-stress.patch` 采用社区固定快照
[Glavin001/blast-stress-solver@230003d9a640927f1bd61d9cf4b88c84d51c1cc1](https://github.com/Glavin001/blast-stress-solver/tree/230003d9a640927f1bd61d9cf4b88c84d51c1cc1/blast/source/shared/stress_solver)
的必要兼容改动，数值算法仍为 Blast 5.1 自带 `CGNR_SISD` / `Float_Scalar`：

- `solver_types.h`、`anglin6.h`、`inertia.h`、`coupling.h` 和 `math/cgnr.h` 在
  `STRESS_SOLVER_NO_SIMD` 下隔离 x86 特化，保留原始标量运算与内存对齐。
- `stress.cpp` 在 `STRESS_SOLVER_FORCE_SCALAR` 下固定原标量路径；
  `STRESS_SOLVER_NO_DEVICE_QUERY` 避开 x86 CPU 指令探测。
- `NvBlastExtStressSolver.cpp` 同样隔离未使用的指令探测头及 x86 SIMD 浮点保护。
  该处是对本仓 5.1 文件布局的最小适配，不替换求解、损伤或图拆分算法。
- 没有带入社区后续性能优化、额外诊断行为、Rust/Rapier 包装或另一套物理后端。

三个宏在所有平台统一启用，不按 CPU 静默关闭应力功能，也不引入 AVX/FMA 编译选项。
标量版本面向跨平台正确性基线，并不意味着已完成所有移动平台实机测试或性能认证。
全部受影响文件复制到 `build/` 后打补丁，未改官方子模块；原有哈希机制同时包含
两份补丁及全部 staged 输入，未改变时不重写产物。所引用源码继续保留 NVIDIA
BSD-3-Clause 头，完整许可见子模块 `PACKAGE-LICENSES/blast-sdk-LICENSE.md`。

### ExtStress 的 world-bond 原点问题

正式 Mac Debug 回归发现：上游 `updateBondStress` 把无限质量 external world node 的
占位位置 `(0,0,0)` 当作实体端点，与碎块质心相减得到 `nodeDist`。当锚定碎块质心
恰在资产原点时，扭转/弯曲压力除以零，产生无穷损伤；改变资产原点也会错误改变应力。
这是本仓重现的上游行为，不声称有官方修复版本或 issue。

补丁仅把压力计算中的静态端点替换成**该连接自己的实际接触中心**。端点规则直接沿用
同一官方文件 `fillDebugRender` 已使用的 `mass > 0 ? localPos : bond.centroid`，并与
原有 `StressProcessor::prepare` 对静态连接使用接触中心的做法一致。没有给距离添加
任意 epsilon，没有修改 CGNR、惯性、损伤公式或动态连接。新增资产原点平移不变性
回归，锚定质心恰在原点时仍得到相同损伤比例；旋转/缩放实例由 runtime 回归覆盖。
