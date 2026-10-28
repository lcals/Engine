# vcpkg 本地补丁

此目录用于覆盖有问题的 vcpkg port。平时不放复制的上游 port；需要修复时，以 port 名称建立小写目录，并从仓库锁定的 vcpkg baseline 复制该 port 的 `portfile.cmake`、`vcpkg.json` 及其引用文件，再将补丁保存在该目录中，由 portfile 的 `PATCHES` 参数应用。`vcpkg-configuration.json` 会让命令行和 CMake Preset 都使用此目录。

本地 port 文件和补丁纳入版本管理。修改后重新配置；vcpkg 会按 port 内容重新计算包 ABI 并安装更新后的包。CMake 构建也会监视此目录，变动时先重新配置。不要直接修改 vcpkg 安装目录或上游源码。
