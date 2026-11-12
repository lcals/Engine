# vcpkg 本地补丁

此目录用于覆盖有问题的 vcpkg port。平时不放复制的上游 port；需要修复时，以 port 名称建立小写目录，并从仓库锁定的 vcpkg baseline（或明确记录的新版上游）复制该 port 的 `portfile.cmake`、`vcpkg.json` 及其引用文件，再将补丁保存在该目录中，由 portfile 的 `PATCHES` 参数应用。`vcpkg-configuration.json` 会让命令行和 CMake Preset 都使用此目录。

本地 port 文件和补丁纳入版本管理。修改后重新配置；vcpkg 会按 port 内容重新计算包 ABI 并安装更新后的包。CMake 构建也会监视此目录，变动时先重新配置。不要直接修改 vcpkg 安装目录或上游源码。

port 的基线来源、版本迁移和 MIT 许可全文见 [vcpkg 许可](../notices/vcpkg.md)。

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

原生后端继续使用 Metal、DirectX 12 和 Vulkan；Web 运行产物使用 WebGPU。普通 Shader 工具默认关闭 Tint；显式 `web` feature 为宿主 bgfx 启用 `wgsl`，通过 `spirv-tools[internal-headers]` 使用同版本内部头文件。浏览器后端、回读和工具链补丁的固定来源及许可见 [WebGPU 接入说明](../notices/webgpu.md)。

SPIRV-Cross 使用新 baseline 提供的 `vulkan-sdk-1.4.363.0`。该正式版已包含 bgfx shaderc 需要的 `CompilerMSL::get_writes_to_point_size()` 及实际 PointSize store 跟踪，因此删除旧 `spirv-cross/` overlay 和兼容补丁，直接复用 registry port。来源和许可见 [SPIRV-Cross 说明](../notices/spirv-cross.md)。

新运行时需要同步使用本 overlay 安装的 shaderc。旧 Shader 二进制必须通过正常导入指纹和内置 Shader 构建重新生成；不能混用旧工具生成物。所有补丁只由 portfile 在解包后应用，不直接修改安装库或上游检出目录。实际平台构建与回归结果见[测试文档](../../docs/testing.md)。

## 2026-11-04 稳定版本检查

整体 baseline 升至 `8450b1326545e5e933c36d698c74df072ffca6d0`；以下 overlay 用固定正式版补齐 baseline 尚未包含的稳定更新，不跟踪浮动分支：

| port | 固定版本 | 保留或迁移的本地处理 |
| --- | --- | --- |
| `sdl3` | `release-3.4.20` | 采用新版官方 port 的平台选项及完整许可清单；保留 Windows 读文件共享删除、原子替换补丁和 Android 同版 Java glue 部署。 |
| `tbb` | `v2023.2.0` | 正式源码已包含具体 task 类型销毁修复，移除 `task-handle-finalize-size.patch`；macOS 继续让 USD 多个动态库共享同一个 TBB 调度器。 |
| `quickjs-ng` | `v0.18.0` | 保留且迁移无副作用模块预检及 NUL 路径校验；采用新版 `lib/cmake/qjs` 导出目录，安装 Unicode 许可。 |
| `usd` | `v26.11.1` | 迁移包依赖、安装目录与 Windows 静态插件处理；OpenImageIO 3 依赖及 `ImageBuf` 接口已由上游适配，删除 `013-openimageio-3.patch` 和重复 CMake 处理。 |
| `flecs` | `v4.2.0` | 使用官方 port 构建方式并固定正式版归档。 |
| `efsw` | `1.7.3` | 使用官方 port 构建方式并固定正式版归档。 |
| `flatbuffers` | `v25.12.19-2026-02-06-03fffb2` | 使用正式发布页标为 latest 的维护快照；库与 host `flatc` 同版生成和安装。 |
| `tinyexr` | `v3.3.0` | 使用官方 port 构建方式并固定正式版归档。 |

`basisu v2_50`、bgfx.cmake `v1.167.9605-606` 和 MikkTSpace `3e895b49d05ea07e4c2133156cfa94369e19e409` 经上游 release/tag/HEAD 检查仍为当前稳定版本；保留既有必要补丁。MikkTSpace 无新 tag 或提交，不以其他 fork 替换原算法。

归档 SHA512 在各 portfile 中固定。升级验证不以包登记代替实际链接：受影响 Debug/Release 构建以及 Release 测试结果记录在[测试文档](../../docs/testing.md)，未实测平台不标记通过。

`third_party/triplets/arm64-android-engine.cmake` 固定 arm64/API 28，使用静态 PIC 库和静态 C++ 运行库；SDL3 与所选 VM 静态合入唯一 `libmain.so`，`SDLActivity` Java glue 由 Gradle 编译，Java 只加载 `main`。首次支持范围和实际验证以[测试文档](../../docs/testing.md)为准。

iOS 的 libvpx arm64 模拟器 SDK 修正和 QuickJS 纯库部署见 [iOS 依赖说明](../notices/ios.md)。
