# AGENTS.md

本文件是本仓库的协作约定，适用于根目录及所有子目录。

- 解释和文档默认中文；代码标识、文件名、命令保持英文。
- 所有受版本管理的目录使用小写英文，可用连字符或下划线；保留 `CMakeLists.txt` 等工具要求的标准文件名。
- 先查看源码、当前分支和工作区，再修改；保护已有改动。
- 使用 C++20 和目标级 CMake 配置；公开头文件放 `include/`，实现放 `src/`，测试放 `tests/`。
- 不为测试修改正式接口可见性，不提交构建产物、密钥和本机路径。
- 构建与测试使用根目录的 CMake Presets，具体命令以 [README.md](README.md) 为准。
- 初始化脚本改动运行 `python -m unittest discover -s scripts/tests -v`；C++ 改动运行受影响的 Debug / Release 构建与 CTest。
- 新增依赖需有明确用途；新增 CI Action 使用完整提交 SHA，由 Dependabot 维护更新。
- 第三方库统一在 `vcpkg.json` 声明；`third_party/` 只放需要版本管理的本地 vcpkg 扩展或补丁。
- 快速开始和维护说明统一写入根 `README.md`；如实说明实际运行的验证及限制。
