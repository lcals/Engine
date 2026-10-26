# cpp-template

可直接复用的 C++20 项目模板，使用 CMake + vcpkg，支持 Windows、Linux 和 macOS。包含静态库、命令行程序、CTest 测试和 GitHub Actions CI；示例通过 vcpkg 安装并实际使用 `{fmt}`。

## 创建新项目

在 GitHub 点击 **Use this template → Create a new repository**，然后克隆新仓库。在新仓库根目录执行一次：

```sh
python scripts/init_project.py my-app
```

脚本将项目名设为 `my-app`，CMake 目标和 C++ 命名空间使用 `my_app`，并更新头文件目录、引用、构建选项、CI 和文档。项目名只使用小写字母、数字和连字符；初始化需要 Python 3.10+。随后按项目需要更新本文件和 `LICENSE` 中的版权归属。

## 构建与测试

需要 CMake 3.25+、Git、Python 3.10+、vcpkg 和支持 C++20 的编译器：Windows 安装 Visual Studio 2022+ 的 C++ 桌面开发工作负载，Linux 使用 GCC 或 Clang，macOS 安装 Xcode Command Line Tools。首次构建需要网络获取 manifest 指定的依赖。

把 vcpkg 克隆到任意位置并运行该系统对应的 `bootstrap-vcpkg.bat`（Windows）或 `bootstrap-vcpkg.sh`（Linux/macOS）。随后把 `VCPKG_ROOT` 设置为 vcpkg 的绝对路径；CMake Presets 会据此加载工具链。PowerShell 示例：

```powershell
git clone https://github.com/microsoft/vcpkg.git ..\vcpkg
& ..\vcpkg\bootstrap-vcpkg.bat
$env:VCPKG_ROOT = (Resolve-Path ..\vcpkg).Path
cmake --workflow --preset debug
```

Linux / macOS 的 shell 示例：

```sh
git clone https://github.com/microsoft/vcpkg.git ../vcpkg
../vcpkg/bootstrap-vcpkg.sh
export VCPKG_ROOT="$(cd ../vcpkg && pwd)"
cmake --workflow --preset debug
```

vcpkg 的依赖版本由仓库中的 `vcpkg.json` 固定；已有安装可直接设置 `VCPKG_ROOT`，无需重复克隆。首次配置会自动安装依赖。

```sh
cmake --workflow --preset debug
cmake --workflow --preset release
```

也可以逐步执行：

```sh
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

默认采用当前平台的 CMake 生成器，无需额外安装 Ninja。Windows 通常生成 Visual Studio 工程，Linux / macOS 通常使用 Makefiles。Debug 和 Release 分别写入 `build/debug/`、`build/release/`；Visual Studio 等多配置生成器还会在其中建立对应配置的输出目录。

示例程序的目标名为 `cpp_template_app`。未初始化时，在 Linux / macOS 的默认生成器下可运行 `./build/debug/cpp_template_app Alice`，在 Windows 的 Visual Studio 生成器下运行 `.\build\debug\Debug\cpp_template_app.exe Alice`，输出 `Hello, Alice!`。

## 目录

所有受版本管理的目录均使用小写；`CMakeLists.txt` 等标准文件名保持原样。

```text
.
├── .github/workflows/       # 三平台、四种编译器的 Debug / Release CI
├── cmake/                  # 目标级编译设置
├── docs/                   # 开发与依赖接入说明
├── include/cpp_template/   # 库的公开头文件
├── references/             # 本地参考文件，内容不纳入 Git
├── scripts/                # 项目初始化等辅助脚本
│   └── tests/              # 脚本测试
├── src/                    # 库实现和示例入口
├── tests/                  # C++ 与命令行测试
└── third_party/            # vcpkg 依赖的本地补丁/扩展接入点
```

## 构建选项

| 选项 | 顶层默认值 | 用途 |
| --- | --- | --- |
| `CPP_TEMPLATE_BUILD_APP` | `ON` | 构建示例命令行程序 |
| `CPP_TEMPLATE_BUILD_TESTS` | `ON` | 构建本项目测试，同时受 `BUILD_TESTING` 控制 |
| `CPP_TEMPLATE_WARNINGS_AS_ERRORS` | `OFF` | 本项目目标将编译警告视为错误，CI 开启 |

作为 `add_subdirectory()` 子项目使用时，示例和本项目测试默认关闭；链接库目标 `cpp_template::core` 即可。Preset 用于独立开发，会显式开启示例与测试。

`references/` 用于本地放置参考文件，其全部内容均被 Git 忽略，仅保留 `.gitkeep` 以便克隆后创建目录。

详细说明见 [开发与第三方依赖](docs/development.md)，协作约定见 [AGENTS.md](AGENTS.md)。项目采用 [MIT License](LICENSE)。
