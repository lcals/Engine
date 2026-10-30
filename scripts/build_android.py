"""使用根 CMake Presets 构建原生库，再按 SDL3 的 Activity 框架打包 APK。"""

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys
import zipfile

from artifact_output import staged_output, validate_output

ROOT = Path(__file__).resolve().parents[1]


def require_directory(name):
    value = os.environ.get(name, "")
    if not value or not Path(value).is_dir():
        raise ValueError(f"请将 {name} 设置为现有目录，再重新执行。")
    return Path(value).resolve()


def gradle_command(java):
    # 可复用已安装的 Gradle，包括 Unity 提供的纯 lib 分发；不记录本机路径。
    configured = os.environ.get("GRADLE_HOME", "")
    if configured:
        gradle_root = Path(configured)
        executable = gradle_root / "bin" / ("gradle.bat" if os.name == "nt" else "gradle")
        if executable.is_file():
            return [str(executable)]
        launcher = gradle_root / "lib" / "gradle-launcher-9.1.0.jar"
        if launcher.is_file():
            return [str(java), "-classpath", str(launcher), "org.gradle.launcher.GradleMain"]
        raise ValueError("GRADLE_HOME 中没有 Gradle 9.1.0，请修正路径或取消该变量使用 wrapper。")
    wrapper = ROOT / "platforms/android" / ("gradlew.bat" if os.name == "nt" else "gradlew")
    return [str(wrapper)]


def run(command):
    print("执行：", subprocess.list2cmdline(command), flush=True)
    subprocess.run(command, cwd=ROOT, check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", choices=("debug", "release"), default="debug")
    parser.add_argument("--backend", choices=("quickjs", "v8"), default="quickjs")
    parser.add_argument("--jobs", type=int, default=min(os.cpu_count() or 1, 8))
    parser.add_argument(
        "--project", type=Path, help="要编译并放入 APK 的项目目录，默认 projects/showcase"
    )
    parser.add_argument(
        "--asset-builder",
        type=Path,
        help="宿主 asset_build_app，用 --target android 增量编译项目资源；省略时须已有 Android catalog",
    )
    parser.add_argument("--skip-native", action="store_true", help="仅打包已由 CMake 生成的原生库")
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error("--jobs 必须大于 0")
    if args.skip_native and (args.project is not None or args.asset_builder is not None):
        parser.error(
            "--skip-native 复用已生成资源，不能同时指定 --project 或 --asset-builder；更新项目时请执行完整构建。"
        )
    asset_builder = args.asset_builder.resolve() if args.asset_builder is not None else None
    if asset_builder is not None and (
        not asset_builder.is_file() or not os.access(asset_builder, os.X_OK)
    ):
        parser.error("--asset-builder 必须指向可执行的宿主 asset_build_app，请先构建桌面资源工具。")
    project = args.project or ROOT / "projects/showcase"
    require_directory("ANDROID_HOME")
    ndk_root = require_directory("ANDROID_NDK_HOME")
    properties = (ndk_root / "source.properties").read_text(encoding="utf-8")
    revision = next(
        (
            line.partition("=")[2].strip()
            for line in properties.splitlines()
            if line.partition("=")[0].strip() == "Pkg.Revision"
        ),
        "",
    )
    if revision != "27.2.12479018":
        raise ValueError("Android 工程固定使用 NDK 27.2.12479018，请修正 ANDROID_NDK_HOME。")
    java_root = require_directory("JAVA_HOME")
    java = java_root / "bin" / ("java.exe" if os.name == "nt" else "java")
    if not java.is_file():
        raise ValueError("JAVA_HOME 中没有 Java，请准备 JDK 17。")
    gradle = gradle_command(java)
    version = subprocess.check_output(gradle + ["--version"], cwd=ROOT, text=True)
    if "Gradle 9.1.0" not in version:
        raise ValueError("Android 工程固定使用 Gradle 9.1.0，请修正 GRADLE_HOME 或使用 wrapper。")
    if not args.skip_native:
        require_directory("VCPKG_ROOT")
        cmake = shutil.which("cmake")
        if not cmake:
            raise ValueError("没有找到 CMake，请先运行环境安装脚本。")
        preset = "android-arm64-" + ("v8-" if args.backend == "v8" else "") + args.config
        run(
            [
                cmake,
                "--preset",
                preset,
                "-DENGINE_V8_JOBS=" + str(args.jobs),
                "-DENGINE_ANDROID_PROJECT=" + str(project.resolve()),
                "-DENGINE_ANDROID_ASSET_BUILDER=" + (str(asset_builder) if asset_builder else ""),
            ]
        )
        run([cmake, "--build", "--preset", preset + "-prepare"])
        run([cmake, "--build", "--preset", preset, "--parallel", str(args.jobs)])
    source = (
        ROOT
        / "build/android-gradle"
        / args.config
        / "outputs/apk"
        / args.config
        / ("app-" + args.config + ".apk")
    )
    # APK 增量更新可能保留被替换的 .so 占用的空洞；仅重建最终包，保留所有编译缓存。
    source.unlink(missing_ok=True)
    run(
        gradle
        + [
            "--no-daemon",
            "--build-cache",
            "--project-cache-dir",
            str(ROOT / "build/android-gradle/cache"),
            "-p",
            str(ROOT / "platforms/android"),
            "-PengineConfiguration=" + args.config,
            "-PengineScriptBackend=" + args.backend,
            ":app:assemble" + args.config.title(),
        ]
    )
    if not source.is_file():
        raise ValueError(f"Gradle 没有生成预期 APK：{source}")
    output = validate_output(
        ROOT / "output/android/arm64" / args.config / "runtime",
        ROOT / "output",
        [source, project.resolve()],
    )
    with staged_output(output) as stage:
        staged_apk = stage / "engine-showcase.apk"
        shutil.copy2(source, staged_apk)
        with zipfile.ZipFile(staged_apk) as archive:
            native = sorted(name for name in archive.namelist() if name.endswith(".so"))
            if native != ["lib/arm64-v8a/libmain.so"]:
                raise ValueError(f"Android 交付必须只含 libmain.so，实际为：{native}")
            if "assets/project/catalog.bin" not in archive.namelist():
                raise ValueError("APK 缺少独立的运行资源清单")
    destination = output / "engine-showcase.apk"
    print(f"APK 已生成：{destination}")
    print("Debug / Release APK 均使用本机调试签名，仅供安装测试。")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print(f"Android 打包未完成：{error}", file=sys.stderr)
        sys.exit(1)
