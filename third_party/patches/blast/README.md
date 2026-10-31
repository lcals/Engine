# Blast 5.1 可移植性补丁

上游固定 `NVIDIAGameWorks/Blast@636e7eb43b8d41e44c40eb1b4799bfd027f17ba7`。

`portable-authoring.patch` 仅修正平台/标准库兼容问题：

- `NvBlastExtApexSharedParts.cpp` 的 Vec3 → Vec4 批量转置使用上游已有 `Vec4V_From_Vec3V`。SSE 下这两种类型是同一寄存器类型，Apple ARM 标量实现中则不同；显式转换保留原有三分量转置算法。
- `NsArray.h` 使用标准 `<type_traits>`，移除 Android NDK libc++ 不提供的 TR1 头。非 MSVC 的 POD 判断保留 `is_trivial && is_standard_layout` 语义；MSVC 原有 `is_trivially_copyable` 判断不变。
- `NvBlastExtAuthoringBondGeneratorImpl.h` 显式包含 `NvBounds3.h`，其 `vector<NvBounds3>` 内联构造需要完整类型；不再依赖其他头的间接包含顺序。

- VHACD `FloatMath.cpp` 移除多余的非标准 `<malloc.h>`；已有 `<stdlib.h>` 提供该文件和 `FloatMath.inl` 所需的 `malloc/free`，不使用 Windows 专属的分配接口。

CMake 只将 Foundation 头和受影响的 Authoring 文件复制到构建目录应用补丁，以输入哈希避免重复写入，官方子模块不修改。补丁没有添加 PhysX、CUDA 或另一套破碎算法。原许可随子模块 `PACKAGE-LICENSES/` 保留。
