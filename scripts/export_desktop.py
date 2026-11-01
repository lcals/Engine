"""导出桌面程序和独立资源；macOS 默认原生程序、可选 .app，不在启动时解包。"""

import argparse
import json
from pathlib import Path
import plistlib
import re
import shutil
import subprocess
import sys

from artifact_output import plain_path, staged_output, validate_output
from resource_catalog import copy_resource_snapshot, parse_build_output, safe_relative

ROOT = Path(__file__).resolve().parents[1]


def copy_resources(source, destination, platform):
    receipt = copy_resource_snapshot(source, destination, platform)
    return receipt["resource_count"], receipt["file_count"] - 1


def build_resources(args):
    command = [
        str(args.builder),
        "--project",
        str(args.project),
        "--package",
        "--target",
        args.platform,
    ]
    if args.manifest:
        command += ["--manifest", args.manifest.as_posix()]
    result = subprocess.run(
        command,
        cwd=ROOT,
        capture_output=True,
        encoding="utf-8",
        timeout=600,
        check=False,
    )
    if result.returncode != 0:
        raise ValueError(f"项目资源构建失败：\n{result.stdout}\n{result.stderr}")
    return plain_path(parse_build_output(result.stdout, args.platform))


def macos_bundle(args, stage):
    bundle = stage / (args.product + ".app")
    contents = bundle / "Contents"
    binary = contents / "MacOS" / args.product
    binary.parent.mkdir(parents=True)
    (contents / "Resources").mkdir()
    shutil.copy2(args.executable, binary)
    with (contents / "Info.plist").open("wb") as stream:
        plistlib.dump(
            {
                "CFBundleExecutable": args.product,
                "CFBundleIdentifier": "org.engine." + args.product.replace("_", "-"),
                "CFBundleName": args.product,
                "CFBundlePackageType": "APPL",
                "CFBundleVersion": "1",
                "CFBundleShortVersionString": "0.1.0",
                "NSHighResolutionCapable": True,
                # .app 直接读取 Resources；已发布的外部版本由现有宿主流程优先选择。
                "SDL_FILESYSTEM_BASE_DIR_TYPE": "resource",
            },
            stream,
        )
    return bundle


def export(args):
    if not re.fullmatch(r"[a-z][a-z0-9_-]*", args.product):
        raise ValueError("产品名称必须为小写英文标识")
    args.executable = plain_path(args.executable)
    sources = [args.executable]
    if args.icu:
        args.icu = plain_path(args.icu)
        sources.append(args.icu)
    if args.kind == "runtime":
        if not args.project or not args.builder:
            raise ValueError("运行包需要 --project 和 --builder")
        args.project, args.builder = plain_path(args.project), plain_path(args.builder)
        sources.extend([args.project, args.builder])
    for origin, relative in args.support_file + args.support_directory:
        safe_relative(relative)
        sources.append(plain_path(origin))
    output = validate_output(args.output, ROOT / "output", sources)
    source = build_resources(args) if args.kind == "runtime" else None
    if source is not None:
        validate_output(output, ROOT / "output", [source])
    with staged_output(output) as stage:
        bundle = macos_bundle(args, stage) if args.platform == "macos" and args.app_bundle else None
        executable = args.product + (".exe" if args.platform == "windows" else "")
        base = bundle / "Contents/Resources" if bundle else stage
        resources = base / "assets"
        if bundle is None:
            shutil.copy2(args.executable, stage / executable)
        if args.icu:
            shutil.copy2(args.icu, base / "icudtl.dat")
        for origin, relative in args.support_file:
            target = stage / relative
            if target.exists():
                raise ValueError(f"工具路径与已部署文件冲突：{relative}")
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(origin, target)
        for origin, relative in args.support_directory:
            shutil.copytree(origin, stage / relative)
        if source:
            resources.mkdir(exist_ok=True)
            count, carriers = copy_resources(source, resources, args.platform)
        else:
            count, carriers = 0, 0
        if bundle:
            subprocess.run(
                [
                    args.cmake,
                    "-DENGINE_APP_BUNDLE=" + str(bundle),
                    "-DENGINE_LIBRARY_DIRS=" + ";".join(args.library_dir),
                    "-P",
                    str(ROOT / "cmake/install/macos_bundle.cmake"),
                ],
                check=True,
            )
        elif args.platform == "macos":
            subprocess.run(
                [
                    args.cmake,
                    "-DENGINE_EXECUTABLE=" + str(stage / executable),
                    "-DENGINE_LIBRARY_DIRS=" + ";".join(args.library_dir),
                    "-P",
                    str(ROOT / "cmake/install/macos_native.cmake"),
                ],
                check=True,
            )
    print(
        json.dumps(
            {
                "status": "ok",
                "output": str(output),
                "targetPlatform": args.platform,
                "resourceCount": count,
                "resourceFiles": carriers,
            },
            ensure_ascii=False,
        )
    )


def main():
    sys.stdout.reconfigure(encoding="utf-8")
    sys.stderr.reconfigure(encoding="utf-8")
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--kind", choices=("editor", "runtime"), required=True)
    parser.add_argument("--platform", choices=("windows", "macos"), required=True)
    parser.add_argument("--product", required=True)
    parser.add_argument("--executable", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--project", type=Path)
    parser.add_argument("--builder", type=Path)
    parser.add_argument("--manifest", type=Path, default=Path("settings/resource-build.toml"))
    parser.add_argument("--icu", type=Path)
    parser.add_argument("--cmake", default="cmake")
    parser.add_argument(
        "--app-bundle", action="store_true", help="macOS 可选 .app；默认直接运行原生程序"
    )
    parser.add_argument("--library-dir", action="append", default=[])
    parser.add_argument("--support-file", nargs=2, action="append", default=[])
    parser.add_argument("--support-directory", nargs=2, action="append", default=[])
    try:
        export(parser.parse_args())
        return 0
    except (OSError, ValueError, KeyError, subprocess.SubprocessError) as error:
        print(f"桌面导出未完成：{error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
