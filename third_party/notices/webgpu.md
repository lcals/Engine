# WebGPU 浏览器接入来源

浏览器运行包复用 bgfx API 167，基础版本和许可证见 [bgfx](bgfx.txt) 与 [overlay 说明](../overlay-ports/README.md)。不整体升级其他原生后端。

`webgpu-external-device.patch` 仅回移下列 bgfx 提交的 WebGPU 后端改动，最终固定到 `f5ce45e39d6381093e577e80adb1abb2c611302c`：

- [a4f43eb7f671dc8f1f0443050c28f5eb14661983](https://github.com/bkaradzic/bgfx/commit/a4f43eb7f671dc8f1f0443050c28f5eb14661983)
- [1370d093a8df6bcf62442b91bd028a085b417aa4](https://github.com/bkaradzic/bgfx/commit/1370d093a8df6bcf62442b91bd028a085b417aa4)
- [a9f89bf751ac6927a12371b49c5b7a096b09f95b](https://github.com/bkaradzic/bgfx/commit/a9f89bf751ac6927a12371b49c5b7a096b09f95b)
- [102a549d34615da5009ce2ecc681b2a2e895b15e](https://github.com/bkaradzic/bgfx/commit/102a549d34615da5009ce2ecc681b2a2e895b15e)
- [f5ce45e39d6381093e577e80adb1abb2c611302c](https://github.com/bkaradzic/bgfx/commit/f5ce45e39d6381093e577e80adb1abb2c611302c)

外部设备引用、实际功能与限制查询、浏览器表面和采样器等修正沿用上述实现。`webgpu-emscripten.patch` 连接官方 Emdawnwebgpu port，并在 Web 目标只编译 WebGPU 后端。`webgpu-readback-yield.patch` 是本仓库的浏览器适配：回读使用浏览器可交付的回调模式，并通过 Emscripten Asyncify 让出应用 worker，保留上游同步回读完成及数据生命周期；原生回读路径不变。机制说明见 [Emscripten Asyncify](https://emscripten.org/docs/porting/asyncify.html)。

`wgsl-tools.patch` 启用当前 bgfx 自带的 Tint，使用同版本 SPIRV-Tools 库和其内部头文件，以及 glslang 公开查询接口。asm.js 目标按 WGSL 宏预处理；不再编译一份不匹配的 SPIRV-Tools。

工具链固定为 Emscripten 7.0.0、emsdk 提交 `1693ce7359fc9694f198694f902a2cc061239b86`，配套 Emdawnwebgpu 的 Dawn 提交为 `31e25af254ab572c77054edec4946d2244e184dd`。版本和 IDBFS 原文件校验值集中在 [toolchain.json](../../platforms/web/toolchain.json)。[IDBFS 补丁](../emscripten/idbfs-atomic-sync.patch) 在事务中止后报告失败，避免错误回执先于事务终态；SDK 原目录保持不变。许可证分别见 [Emscripten](../emscripten/LICENSE) 与 [Dawn](../emscripten/emdawnwebgpu-license.txt)。

配套可移植修正还包括 Quill 的 Wasm 时钟与不支持的 CPU affinity、libvpx 保留 triplet 线程选项、Effekseer 的标准对齐分配、Blast 的 Wasm 平台识别和标准库包含。版本由各 port 或 Git 子模块固定；修改均在构建副本应用。复制的 vcpkg port 来源与 MIT 许可见 [vcpkg](vcpkg.md)。
