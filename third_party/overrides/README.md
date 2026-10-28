# Git 子项目本地覆盖

仅在上游依赖有明确问题时，在本目录按 `<dependency>/<upstream-relative-path>` 放置完整的替换文件。例如，`ozz-animation/src/example.cc` 对应 `third_party/overrides/ozz-animation/src/example.cc`。

配置时，CMake 将固定版本的 Git 子项目复制到构建目录并应用这些覆盖文件；每次构建前再次按内容覆盖。原始子项目检出保持不变。使用 CMake 构建命令时，覆盖目录变化和子项目版本切换会触发自动重新配置。删除覆盖文件后，重新配置会从原始版本重建副本。

每个覆盖文件应附带说明上游问题、原版本和移除条件的变更记录，避免长期维护不明来源的分叉。vcpkg 依赖的补丁放入对应 overlay port，由 vcpkg 在安装依赖时应用。
