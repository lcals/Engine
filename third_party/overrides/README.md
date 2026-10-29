# Git 子项目本地覆盖

仅在上游依赖有明确问题时，在本目录按 `<dependency>/<upstream-relative-path>` 放置完整的替换文件。例如，`ozz-animation/src/example.cc` 对应 `third_party/overrides/ozz-animation/src/example.cc`。

配置时，CMake 将固定 Git 提交复制到构建目录并应用这些覆盖文件；上游检出的未提交修改不参与副本。原始子项目检出保持不变。后续仅在固定提交、覆盖文件、准备脚本变化或副本缺失时重新准备；无变化的构建不执行准备命令。删除覆盖文件会恢复上游内容，内容相同的文件始终保留时间戳，避免引发无关重编译。

每个覆盖文件应附带说明上游问题、原版本和移除条件的变更记录，避免长期维护不明来源的分叉。vcpkg 依赖的补丁放入对应 overlay port，由 vcpkg 在安装依赖时应用。
