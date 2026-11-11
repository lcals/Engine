"""Copy loose files or package members from a v2 catalog and transactionally update Android built-in assets while preserving v8."""

import argparse
import json
import os
from pathlib import Path
import subprocess
import sys

from artifact_output import plain_path, staged_output
from runtime_settings import export_runtime_settings
from resource_catalog import (
    copy_asset,
    copy_resource_snapshot,
    parse_build_output,
    safe_relative,
)

ROOT = Path(__file__).resolve().parents[1]


def require_directory(path):
    if (
        path.is_symlink()
        or (hasattr(path, "is_junction") and path.is_junction())
        or not path.is_dir()
    ):
        raise ValueError(f"Must be an existing regular directory, not a symbolic link: {path}")


def asset_files(root, directory=None):
    current = directory or root
    with os.scandir(current) as entries:
        ordered = sorted(entries, key=lambda item: item.name)
    for item in ordered:
        path = Path(item.path)
        safe_relative(path.relative_to(root).as_posix())
        if item.is_symlink() or (hasattr(path, "is_junction") and path.is_junction()):
            raise ValueError(f"Resources do not support symbolic links: {path}")
        if item.is_dir(follow_symlinks=False):
            yield from asset_files(root, path)
        elif item.is_file(follow_symlinks=False):
            yield path
        else:
            raise ValueError(f"Resource must be a regular file or directory: {path}")


def compile_catalog(project, asset_builder=None, manifest=None, resource_root=None):
    if resource_root is not None:
        if manifest is not None:
            raise ValueError("--manifest and --resource-root cannot be combined")
        source = Path(resource_root).absolute()
        require_directory(source)
        return source
    if asset_builder is None:
        source = project / "cache/build/android"
        if manifest is not None or not (source / "catalog.bin").is_file():
            raise ValueError(
                "Compiled GUID resource directory is missing; pass --asset-builder "
                "or use --resource-root to select an existing runtime package."
            )
        return source
    if manifest is None:
        manifest = Path("settings/editor-resource-build.toml")
    command = [
        str(Path(asset_builder).resolve()),
        "--project",
        str(project),
        "--package",
        "--target",
        "android",
    ]
    if manifest is not None:
        command += ["--manifest", Path(manifest).as_posix()]
    result = subprocess.run(
        command,
        cwd=ROOT,
        stdout=subprocess.PIPE,
        text=True,
        encoding="utf-8",
        check=False,
    )
    if result.returncode != 0:
        raise ValueError("Android asset build failed; existing package resources are unchanged. Fix compiler diagnostics and retry.")
    source = parse_build_output(result.stdout, "android")
    require_directory(source)
    return source


def stage_assets(source, staging):
    require_directory(source)
    receipt = copy_resource_snapshot(
        source,
        staging,
        "android",
        carrier_roots=("gen", "packs"),
        exact_carrier_size=True,
        require_unchanged_catalog=True,
    )
    return {"revision": receipt["revision"], "file_count": receipt["file_count"]}


def prepare(project, output, asset_builder=None, manifest=None, resource_root=None):
    project = Path(project).absolute()
    require_directory(project)
    for name in ("settings", "assets", "cache"):
        require_directory(project / name)
    project = project.resolve()
    output = plain_path(output).resolve()
    if output.is_relative_to(project) or project.is_relative_to(output):
        raise ValueError(
            "Android asset output and project directory must not contain each other; use the CMake android/assets directory."
        )
    # 编译前检查 TS 与静态资源树；编译结果也会在复制前独立检查。
    for _ in asset_files(project / "assets"):
        pass
    if output.exists():
        require_directory(output)
    # 整个 assets 根作为一版发布；仅保留独立 VM 数据，其他构建残留自然移除。
    with staged_output(output) as staging:
        v8 = output / "v8"
        if v8.exists() or v8.is_symlink():
            require_directory(v8)
            preserved = staging / "v8"
            preserved.mkdir()
            for path in asset_files(v8):
                relative = path.relative_to(v8).as_posix()
                copy_asset(path, preserved / relative)
        source = compile_catalog(project, asset_builder, manifest, resource_root)
        receipt = stage_assets(source, staging)
        # APK assets 是平台容器；settings 与 catalog/gen/packs 并列，不进入资源清单。
        export_runtime_settings(project, staging, "android")
    return receipt


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--project", required=True, type=Path, help="Project directory containing settings/cache/assets"
    )
    parser.add_argument(
        "--output", required=True, type=Path, help="CMake binary/android/assets directory"
    )
    parser.add_argument(
        "--asset-builder", type=Path, help="Host asset_build_app path for Android GUID resources"
    )
    parser.add_argument(
        "--manifest", type=Path, help="Project build manifest; defaults to settings/editor-resource-build.toml"
    )
    parser.add_argument("--resource-root", type=Path, help="Existing v2 runtime package directory; skips the builder")
    arguments = parser.parse_args()
    receipt = prepare(
        arguments.project,
        arguments.output,
        arguments.asset_builder,
        arguments.manifest,
        arguments.resource_root,
    )
    print(
        f"Android project resources prepared: {arguments.output} ({receipt['file_count']} files, "
        f"revision {receipt['revision']})"
    )
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError, UnicodeError) as error:
        print(f"Android resource preparation did not complete: {error}", file=sys.stderr)
        sys.exit(1)
