# iOS 原生构建依赖调整

运行库沿用引擎固定的 vcpkg baseline 与 Git 子项目，不增加 iOS 专用渲染、线程或脚本框架。版本、许可证继续由各上游及 vcpkg copyright 文件维护，导出包包含所选依赖许可。

- libvpx 1.16.0 的 overlay 在 `ios-simulator.patch` 为 arm64 增加独立 iphonesimulator 目标，选择相应 Xcode SDK、编译与链接的模拟器部署参数，并保留 NEON 支持；真机继续使用 iphoneos。iOS 最低版本来自 triplet 的统一部署目标。补丁应用在 vcpkg 构建副本，不直接修改上游检出目录。来源：[libvpx v1.16.0](https://github.com/webmproject/libvpx/tree/v1.16.0)。
- QuickJS-NG 0.18.0 的 overlay 在 iOS 仅部署库，不将目标 qjs / qjsc CLI 当作可在宿主运行的工具复制；引擎继续使用正常 QuickJS 插件，不启用 JIT。来源：[QuickJS-NG v0.18.0](https://github.com/quickjs-ng/quickjs/tree/v0.18.0)。
- SDL3 的 UIKit、Metal view、事件观察与动画回调直接使用所选上游；平台资源读取与常规存储继续走既有接口。bgfx Metal 沿用当前 overlay 和设备重建链，不新增后端实现。

真机和 arm64 模拟器使用各自的 triplet 与依赖安装根；同一 CPU 架构不表示 Mach-O 平台相同。资源导出与应用签名详见 [iOS 构建](../../docs/building.md#ios-原生游戏导出)，实际覆盖及限制见 [测试](../../docs/testing.md#ios-原生运行包)。


桌面资源同步使用根 manifest 的可选 `ios-device-sync` feature，由实际选中的 `editor/mcp` 声明。固定 baseline 对应 libimobiledevice 2023-07-05#1（上游 `6fc41f57fc607df9b07446ca45bdf754225c9bd9`），许可证为 LGPL-2.1-or-later；libplist、libusbmuxd、libimobiledevice-glue 和 OpenSSL 沿 vcpkg 依赖及许可证管理。直接使用 House Arrest / AFC 的应用容器、文件读写、flock 和 rename API，不复制上游实现，不把这些库链接进 iOS、Android、Web 或原生游戏运行时。模拟器用 Xcode simctl 取得所选应用的数据容器，文件发布仍复用现有同步格式。实际能力及权限边界见[资源同步](../../docs/plugins/editor/resource-sync.md#ios-资源同步与热重载)。
