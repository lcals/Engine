# vcpkg 本地补丁

此目录用于覆盖有问题的 vcpkg port。平时不放复制的上游 port；需要修复时，以 port 名称建立小写目录，并从仓库锁定的 vcpkg baseline（或明确记录的新版上游）复制该 port 的 `portfile.cmake`、`vcpkg.json` 及其引用文件，再将补丁保存在该目录中，由 portfile 的 `PATCHES` 参数应用。`vcpkg-configuration.json` 会让命令行和 CMake Preset 都使用此目录。

本地 port 文件和补丁纳入版本管理。修改后重新配置；vcpkg 会按 port 内容重新计算包 ABI 并安装更新后的包。CMake 构建也会监视此目录，变动时先重新配置。不要直接修改 vcpkg 安装目录或上游源码。

BasisU 新版 port、SPIRV-Cross 同版 port 与补丁的来源和 MIT 许可全文见 [vcpkg 许可](../notices/vcpkg.md)。

Android 使用仓库锁定的库版本；单项版本升级由对应 overlay 明确固定，相关修复为：

- `basisu`：使用官方稳定版 2.50（`v2_50`，提交 `9bebe16726b3a61c8c213eeee3b7cffb462ef34e`），以 [vcpkg 2.50#1 port](https://github.com/microsoft/vcpkg/tree/149fdb9c7b51a03cd54faf1766774b43366b0534/ports/basisu) 为基线并保留 MIT 许可的 `export-cmake-config.diff`。原有 1.50 CMake 补丁已替换；保留完整编码器与转码器静态库，复用 vcpkg `zstd`。本地 `android-library.patch` 在 Android/iOS 关闭命令行工具，以 `Threads::Threads` 表达线程依赖，避免硬链接 Android 不提供的独立 `pthread` 库。2.50 上游已修复普通模式/完成日志的 `m_status_output` 判断；`quiet-library-output.patch` 只将余下 HDR 等警告送往 stderr，避免污染 MCP stdio。当前官方 port 仍标记 `!android`，因此继续保留此 overlay。
- `bgfx`：为导出的 Android 静态库传递 `android`、`EGL`、`GLESv2`、`dl` 系统依赖。原有 Android Vulkan 后端继续参与编译。
- `sdl3`：安装本次原生库同版本的 Java 源码到 `share/sdl3/java`，由应用的 Gradle 工程编译，避免依赖临时构建目录或另一版本的 Java JNI 接口。
- `mikktspace`：修复极细长三角形的角度加权精度，避免浮点点积舍入为 1 后把真实夹角和切线权重清零；常规角度沿用上游计算。

## bgfx API 167

`bgfx/vcpkg.json` 固定 [bgfx.cmake `v1.167.9605-606`](https://github.com/bkaradzic/bgfx.cmake/releases/tag/v1.167.9605-606)，发布归档包含下列上游源码；安装不跟踪 `master`，也不额外套用尚未发布提交：

| 项目 | 固定提交 |
| --- | --- |
| bgfx | `b2513038f4b753ebeced40821c193978daa0492f`（API 167） |
| bx | `da13683750ff8df8f44a5a15f637b6ae9460a04b` |
| bimg | `64a34f7441af8f06e40ccab08d76bbf5e59e55b1` |

归档 `bgfx.cmake.v1.167.9605-606.tar.gz` 的 SHA512 为
`c4563a147e5aa734e24f43212e1e3c8a0e084799f2e6a7ff1fb078efdb6e209542c618277441a6026ddd3f518b52d11f9389fe302d7fa44ab8cffc083bed3a12`。

从 API 129 迁移时逐项处理原有六项补丁：

| 原有补丁 | API 167 处理 |
| --- | --- |
| `fix-dependencies.patch` | 迁移到新版工具源文件；复用 vcpkg 的 lodepng、miniz、tinyexr、stb、libsquish、meshoptimizer、glslang、SPIRV-Cross 与 SPIRV-Tools。新版 bx Settings 已自带实现，删除不再使用的 mgnlibs 依赖。 |
| `android-link-libraries.patch` | 保留，Android 静态库继续传递 `android`、`EGL`、`GLESv2`、`dl`。 |
| `windows-tool-utf8.patch` | 保留，工具继续使用 Windows UTF-8 manifest。 |
| `d3d12-compressed-mip-update.patch` | 删除；上游 `TextureD3D12::update` 已按压缩块向上取整宽高、行距与复制区域。 |
| `metal-uniform-buffer-segments.patch` | 删除旧容量管理；上游 `ChunkedScratchBufferMtl` 已支持分块及延迟释放。仅保留 `metal-uniform-bounds.patch`，对 CPU stage scratch 的写入长度使用 64 位计算和边界检查，不另建容量管理器。 |
| `framebuffer-sampling-caps.patch` | 删除本地 MSAA 数组，使用上游逐格式的 `BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER_MSAA_X2/_X4/_X8/_X16`。仅保留 `max-anisotropy-caps.patch`，在 C++、C99 和 IDL 中同步暴露实际后端最大各向异性；未报告后端保持 1。 |

当前引擎选择 Metal、DirectX 12 和 Vulkan；Shader 工具保留相应 GLSL/SPIR-V/MSL/DXIL/HLSL 支持，显式禁用未使用的 WGSL/Tint，避免依赖 SPIRV-Tools 未导出的私有头或编译另一份同名库。升级不接入 WebGPU、硬件视频解码、HDR10 或新天空效果。

`spirv-cross/` 保持 baseline 的 `vulkan-sdk-1.4.309.0`（本地修订 `#1`），仅回移新版 shaderc 所需的 `CompilerMSL::get_writes_to_point_size()` 和实际 PointSize store 跟踪，保留生成 MSL 点精灵输出时的判断。接口来自正式上游提交 `f170de532cc4d2583e6b26dbff5d0c81106c6f78`，跟踪实现来自正式上游 `8b91a447a78f51f841119573f6f99a2d8c955c22`，均与本次已选 bgfx `b2513038f4b753ebeced40821c193978daa0492f` 的固定副本核对；不升级整套 vcpkg baseline。来源、适配内容和完整许可见 [SPIRV-Cross 说明](../notices/spirv-cross.md)。

新运行时需要同步使用本 overlay 安装的 shaderc。旧 Shader 二进制必须通过正常导入指纹和内置 Shader 构建重新生成；不能混用旧工具生成物。所有补丁只由 portfile 在解包后应用，不直接修改安装库或上游检出目录。实际平台构建与回归结果见[测试文档](../../docs/testing.md)。

`third_party/triplets/arm64-android-engine.cmake` 固定 arm64/API 28，默认使用静态 PIC 库和共享 C++ 运行库；SDL3 单独使用共享库以供 `SDLActivity` 加载。首次支持范围和实际验证以[测试文档](../../docs/testing.md)为准。
