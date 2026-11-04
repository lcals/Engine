"""Check host shaderc against the imported target bgfx headers during configuration."""

import argparse
from pathlib import Path
import platform
import re
import subprocess
import tempfile


def cmake_path(path):
    return str(path.resolve()).replace("\\", "/")


def configure_case(arguments, temporary, name, target_api, success, cross_target=False):
    source = temporary / name
    includes = source / "include/bgfx"
    includes.mkdir(parents=True)
    (includes / "defines.h").write_text(
        f"#define BGFX_API_VERSION UINT32_C({target_api})\n", encoding="utf-8"
    )
    (includes / "bgfx_shader.sh").write_text("// Configure-only fixture.\n", encoding="utf-8")
    (source / "fixture.cpp").write_text("int shader_toolchain_fixture;\n", encoding="utf-8")
    (source / "CMakeLists.txt").write_text(
        "cmake_minimum_required(VERSION 3.25)\n"
        "project(shader_toolchain_fixture LANGUAGES CXX)\n"
        "add_library(bgfx::bgfx INTERFACE IMPORTED)\n"
        f'set_target_properties(bgfx::bgfx PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "{cmake_path(source / "include")}")\n'
        f'set(ENGINE_SHADERC_EXECUTABLE "{cmake_path(arguments.shaderc)}" CACHE FILEPATH "")\n'
        f'set(ENGINE_BGFX_SHADER_INCLUDE_DIR "{cmake_path(includes)}" CACHE PATH "")\n'
        f'include("{cmake_path(arguments.source / "cmake/core/render_shaders.cmake")}")\n'
        "add_library(render STATIC fixture.cpp)\n"
        "engine_embed_render_shaders(render)\n",
        encoding="utf-8",
    )
    command = [str(arguments.cmake), "-S", str(source), "-B", str(source / "build")]
    if cross_target:
        sysroot = source / "empty/sysroot"
        sysroot.mkdir(parents=True)
        # 显式设置目标系统会启用 CMake 交叉配置；只模拟查找根，不要求安装另一套 SDK。
        command.extend(
            [
                f"-DCMAKE_SYSTEM_NAME={platform.system()}",
                "-DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY",
                f"-DCMAKE_FIND_ROOT_PATH={cmake_path(sysroot)}",
                "-DCMAKE_FIND_ROOT_PATH_MODE_INCLUDE=ONLY",
            ]
        )
    result = subprocess.run(command, capture_output=True, text=True, timeout=60, check=False)
    output = result.stdout + result.stderr
    if (result.returncode == 0) != success:
        raise AssertionError(f"Unexpected {name} result {result.returncode}:\n{output}")
    if not success and f"Host shaderc must match target bgfx API {target_api}" not in output:
        raise AssertionError(f"{name} did not reject the tool/target API mismatch:\n{output}")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--cmake", required=True, type=Path)
    parser.add_argument("--source", required=True, type=Path)
    parser.add_argument("--shaderc", required=True, type=Path)
    arguments = parser.parse_args()
    result = subprocess.run(
        [str(arguments.shaderc), "--version"], capture_output=True, text=True, timeout=10, check=True
    )
    match = re.search(
        r"shaderc, bgfx shader compiler tool, version \d+\.\d+\.(\d+)\.",
        result.stdout + result.stderr,
    )
    if match is None:
        raise AssertionError("Host shaderc did not report its bgfx API")
    api = int(match.group(1))
    with tempfile.TemporaryDirectory(prefix="engine-shader-toolchain-") as directory:
        temporary = Path(directory)
        configure_case(arguments, temporary, "matching-api", api, True)
        configure_case(arguments, temporary, "different-api", api + 1, False)
        configure_case(arguments, temporary, "matching-cross-target-api", api, True, True)
        configure_case(arguments, temporary, "different-cross-target-api", api + 1, False, True)
    print("Host shaderc API matches imported target headers; native/cross-target mismatches rejected")


if __name__ == "__main__":
    main()
