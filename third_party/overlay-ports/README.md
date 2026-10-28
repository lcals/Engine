# vcpkg 本地补丁

此目录用于覆盖有问题的 vcpkg port。平时不放复制的上游 port；需要修复时，以 port 名称建立小写目录，并从仓库锁定的 vcpkg baseline 复制该 port 的 `portfile.cmake`、`vcpkg.json` 及其引用文件，再将补丁保存在该目录中，由 portfile 的 `PATCHES` 参数应用。`vcpkg-configuration.json` 会让命令行和 CMake Preset 都使用此目录。

本地 port 文件和补丁纳入版本管理。修改后重新配置；vcpkg 会按 port 内容重新计算包 ABI 并安装更新后的包。CMake 构建也会监视此目录，变动时先重新配置。不要直接修改 vcpkg 安装目录或上游源码。

Android 使用仓库锁定 baseline 的库版本，相关修复为：

- `basisu`：保留完整编码器与转码器静态库，Android 不构建命令行工具；线程依赖使用 `Threads::Threads`，避免硬链接 Android 不提供的独立 `pthread` 库。
- `bgfx`：为导出的 Android 静态库传递 `android`、`EGL`、`GLESv2`、`dl` 系统依赖。原有 Android Vulkan 后端继续参与编译。
- `sdl3`：安装本次原生库同版本的 Java 源码到 `share/sdl3/java`，由应用的 Gradle 工程编译，避免依赖临时构建目录或另一版本的 Java JNI 接口。

`third_party/triplets/arm64-android-engine.cmake` 固定 arm64/API 28，默认使用静态 PIC 库和共享 C++ 运行库；SDL3 单独使用共享库以供 `SDLActivity` 加载。首次支持范围和实际验证以根 `README.md` 为准。
