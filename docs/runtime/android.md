# Android 宿主与 APK

Android 平台工程位于 `platforms/android/`，沿用 [SDL3 的 Activity / JNI 框架](https://wiki.libsdl.org/SDL3/README-android)。原生部分仍由根 CMake Presets 编译，Gradle 只处理 Java、Manifest 和 APK；SDLActivity 源码从同版本 SDL3 的 vcpkg overlay 安装结果取得。APK 直接启动引擎，不包含编辑器项目目录、项目校验或编辑器 UI。

准备 Python 3、JDK 17、Android SDK Platform 35、Build Tools 37.0.0、NDK 30.0.16248370，以及[构建指南](../building.md)要求的 CMake / Ninja / 编译缓存 / vcpkg。设置 `ANDROID_HOME`、`ANDROID_NDK_HOME`、`JAVA_HOME` 和 `VCPKG_ROOT` 指向已有安装；可复用其他工程的安装目录。Android Gradle Plugin 固定 9.5.0，wrapper 固定 Gradle 9.8.2 并核对官方 SHA-256；也可通过 `GRADLE_HOME` 复用相同版本，脚本会检查版本。

```sh
npm ci --prefix projects/showcase/settings
python3 scripts/build_android.py --config debug --project projects/showcase --asset-builder build/ninja-engine-debug/asset_build_app
python3 scripts/build_android.py --config release --project projects/showcase --asset-builder build/ninja-engine-debug/asset_build_app
```

Android 默认使用 QuickJS；脚本依次调用 `android-arm64-debug` / `android-arm64-release` 的根 configure、显式 `-prepare`（`engine_script_prepare`）、build Preset，再打包。传入 `--backend v8` 使用独立 `android-arm64-v8-debug` / `android-arm64-v8-release` 预设，不混用后端缓存。V8 的 Android 构建入口支持 macOS ARM64 / x64 和 Linux x64 宿主，Windows 宿主的 Android V8 工具链尚未接入。产物为 `output/android/arm64/debug/runtime/engine-showcase.apk` 和 `output/android/arm64/release/runtime/engine-showcase.apk`；最低 Android 9 / API 28，ABI 为 `arm64-v8a`，需要 Vulkan。两种 APK 均使用本机调试签名，供安装测试；发布签名尚未配置，签名密钥不进入仓库。

2026-11-04 工具依赖升级采用 Gradle 官方 `versions/current` 和 Google Android SDK 官方 `repository2-3.xml` 的稳定 `channel-0`，不采用 preview：Gradle 9.8.2、AGP 9.5.0、NDK r30（30.0.16248370）、Build Tools 37.0.0。`compileSdk` / `targetSdk` 继续为 35，最低 API 继续为 28；升级构建工具不提升应用的平台要求。

wrapper 的分发 SHA-256 为 `21bd5da2de49b642904077f38c0efdc7778e92b3ed0889392dca61b97e389607`，wrapper JAR SHA-256 为 `3b8a25775a69158b5ad2b1d17a80a88dc7b40a352a73eb27d06c612a7ce68e98`，JAR 与启动脚本来自 Gradle `v9.8.2` 正式 tag。SDK 包通过官方 sdkmanager 安装并校验发布归档；NDK 与 V8 GN 使用同版 r30，不能混用其他 NDK 的 libc++ 头文件或静态运行库。

两种 APK 内均只有 `lib/arm64-v8a/libmain.so`，SDL3、所选 QuickJS 或 V8、引擎及 NDK C++ 运行库静态链接在其中；Java 只加载 `main`，链接器显式保留 SDL 的 `JNI_OnLoad`。Gradle 去除原生库调试符号以控制安装体积；Debug 保留断言与可调试属性。完整的 Debug 原生符号保存在对应构建目录的 `libmain.so`（默认 `build/android-arm64-debug/libmain.so`），可用 APK 原生库的 ELF Build ID 与之对应，无需把符号放进设备。

脚本检查 NDK 版本，并在打包前仅移除该配置的旧 APK，避免增量 ZIP 更新留下旧原生库空洞导致安装包膨胀；原生库、Java、Gradle 和依赖编译缓存继续复用。`--project` 指定打包项目，默认 `projects/showcase`，必须具有 `settings`、`assets`、`cache` 三个目录并安装 settings 中锁定的 TypeScript。根 CMake 的 bundle 目标通过指定的宿主 asset_build_app 增量构建资源：使用与桌面相同的 `settings/resource-build.toml`，通过 `--package --target android` 构建运行包并读取构建回执输出目录；未配置清单默认全量。随后只校验并打包运行 catalog 引用的散文件或合并包；不复制 SQLite、TS/meta 或陈旧产物。`--asset-builder` 必须是已构建的桌面工具（Windows 文件名加 `.exe`），路径经 `ENGINE_ANDROID_ASSET_BUILDER` 传给 CMake；省略时只接受已有的 Android 目标运行包 catalog，拒绝桌面 Metal 缓存和旧 `cache/assets`。原生库与上游缓存继续复用。`--skip-native` 仅重打包已生成的原生库及资源，不能同时指定 `--project` 或 `--asset-builder`。

Android 运行时包含 SDL3、bgfx、FlatBuffers、Basis Universal、flecs、Jolt、Recast / Detour、ozz、RmlUi 和所选脚本后端的实际编译与链接。编辑器、toml++ 项目模块和离线资源管线不进入 APK。依赖共享安装在 `build/dependencies/android/`，与桌面安装目录隔离，并保留 vcpkg 二进制缓存。BasisU 的 Android overlay 只关闭宿主命令行工具，保留完整编码 / 转码库。

选择 V8 时使用独立的 `build/v8/android-arm64/` 工作副本和 Debug / Release 缓存，目标平台、NDK、API 与工具链均参与缓存身份，不复用桌面归档。宿主 Clang / Rust / GN 版本由 V8 固定，Android 使用同一 NDK 的 sysroot、compiler-rt 和 libc++ ABI，由 `c++_static` 将 C++ 运行库合入最终产物；关闭 ThinLTO，最终将完整嵌入依赖合并链接进 `libmain.so`。兼容补丁保存在 `third_party/overrides/v8/`，上游子模块保持只读。NDK r30 的 libc++ 原生提供 C++20 `std::atomic_ref`，V8 和 simdutf 直接使用上游实现，已删除旧 NDK 原子指令兼容分支；不混用其他版本的 C++ 标准库。

APK 与 Windows 共用 `assets/catalog.bin` 及其定位的 `packs/`、`gen/` 资源布局；默认 QuickJS 及裁剪 V8 均不包含 `icudtl.dat`；不包含项目设置、TS 源码或 Node.js。正常启动初始化 App 与 LogSystem，再由 RuntimeHost 初始化系统并启动项目 Gameplay；无需在手机上传入编辑器项目目录。GameplaySession 直接读取 catalog 并向资源核心提交目录，包内版本使用 catalog 修订；JS 直接通过 SDL 读取 APK，不在启动时解压运行资源。

V8 构建才链接可选嵌入测试，在新的 SDL 进程验证 JavaScript、SharedArrayBuffer 原子操作及十六进制 / Base64 转换，并确认默认关闭的 Intl、Temporal、WebAssembly 不存在。QuickJS 包不包含 V8 测试主体；请求该测试时明确报告后端不匹配。这不增加脚本系统的公开接口。

复用已有 ARM64 Android 模拟器，明确指定其序列号后安装和启动：

```sh
adb -s emulator-5580 install -r output/android/arm64/debug/runtime/engine-showcase.apk
adb -s emulator-5580 shell am start -n org.engine.minimal/.EngineActivity
# 有限帧运行，完成后正常退出 Activity。
adb -s emulator-5580 shell am start -n org.engine.minimal/.EngineActivity --ei frames 120
# 仅在安装 --backend v8 构建的 APK 后运行；EngineV8 日志应逐项报告 PASS。
adb -s emulator-5580 shell am force-stop org.engine.minimal
adb -s emulator-5580 shell am start -n org.engine.minimal/.EngineActivity --ez v8_smoke true
```

`emulator-5580` 为命令示例；使用前通过 `adb devices` 确认实际序列号并替换。已有进程要改用不同启动参数时，先用 `adb -s <serial> shell am force-stop org.engine.minimal` 停止本应用，再重新启动；不需要清除模拟器或应用数据。模拟器必须提供 Vulkan；软件 Vulkan 也可验证功能，但不能用来衡量真机 GPU 性能。

资源同步、应用目录和热重载见[资源同步](../plugins/editor/resource-sync.md#android-资源同步与热重载)。其他平台构建见[构建指南](../building.md)。
