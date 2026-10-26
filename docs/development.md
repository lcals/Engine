# 开发与第三方依赖

## 跨平台构建

根目录 `CMakePresets.json` 是 configure、build、test 的共同入口，`debug` / `release` workflow 将三步串联。单配置生成器使用 `CMAKE_BUILD_TYPE`，多配置生成器由 build / test preset 的 `configuration` 选择同名配置。

平台专有的编译参数统一放在 `cmake/project_options.cmake`；只对本项目目标设置，不把警告参数传播给第三方依赖。公开的 C++20 要求通过目标依赖传递给使用方。

如需显式选择编译器或生成器，在一个新的构建目录配置，避免复用不同工具链的 CMake 缓存，并将 vcpkg 工具链一并传入：

```sh
cmake -S . -B build/custom -G Ninja -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE=Debug -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
cmake --build build/custom
ctest --test-dir build/custom --output-on-failure --no-tests=error
```

此示例需要本机已有 Ninja 和 Clang；使用 MSVC + Ninja 时，从 Visual Studio Developer PowerShell 执行并改用 `cl`。机器专属设置可以写入未纳入版本管理的 `CMakeUserPresets.json`。

GitHub CI 验证 Linux GCC、Linux Clang、Windows MSVC 和 macOS AppleClang 的 Debug / Release 构建与测试；可以在仓库 Actions 页面查看每次实际结果。

## 第三方依赖

第三方库由根目录的 `vcpkg.json` manifest 声明，由 `VCPKG_ROOT` 指向的 vcpkg 工具链在 CMake configure 时安装；具体版本由 `builtin-baseline` 和 `overrides` 固定。模板使用 `{fmt}`：`third_party/CMakeLists.txt` 调用 `find_package(fmt CONFIG REQUIRED GLOBAL)`，`cpp_template_core` 链接 `fmt::fmt`，并在 `greeting()` 里实际调用格式化函数。`GLOBAL` 使根目录定义的目标也能看到导入的库目标。

添加新依赖时，运行 `vcpkg add port <port>` 或编辑 `vcpkg.json`，在 `third_party/CMakeLists.txt` 查找包，再将它链接到实际使用的 CMake 目标。依赖的 CMake target 名称以该包的 vcpkg usage 输出为准。例如：

```cmake
# third_party/CMakeLists.txt
find_package(example CONFIG REQUIRED GLOBAL)

# 根 CMakeLists.txt
target_link_libraries(cpp_template_core PRIVATE example::example)
```

仅实现内部使用的依赖选择 `PRIVATE`；公开头文件需要的依赖选择 `PUBLIC`。`third_party/` 保留给确需本地维护的 vcpkg overlay ports、triplets 或补丁；默认不包含外部源码。使用本地扩展时在 `CMakePresets.json` 设置对应的 `VCPKG_OVERLAY_PORTS` 或 `VCPKG_OVERLAY_TRIPLETS`，不要把构建产物放入 Git。

## 测试与格式

`tests/` 使用 CTest 运行库单元测试和 CLI 测试。测试使用普通失败返回值，在 Release 中同样生效；新增业务通过正式接口验证。

初始化脚本测试全部在临时目录执行：

```sh
python -m unittest discover -s scripts/tests -v
```

`.editorconfig` 统一文本编码与换行，`.clang-format` 提供 C++ 格式配置。构建目录、IDE 缓存和机器专属 preset 已由 `.gitignore` 排除。
