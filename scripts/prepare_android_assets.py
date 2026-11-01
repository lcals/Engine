"""按 v2 资源清单复制散文件或包成员，事务更新 Android 内置资源并保留 v8。"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import stat
import subprocess
import sys

from artifact_output import plain_path, staged_output
from resource_catalog import (
    ENTRY,
    FNV_OFFSET,
    FNV_PRIME,
    FNV_MASK,
    MAX_ASSET_SIZE,
    MAX_MANIFEST_SIZE,
    MAX_SCRIPT_SIZE,
    FlatBuffer,
    catalog_entries,
    parse_catalog,
    read_catalog,
    safe_file,
    safe_relative,
    validate_entity,
)

ROOT = Path(__file__).resolve().parents[1]
RESOURCE_MANIFEST = "resource-manifest.json"


def require_directory(path):
    if (
        path.is_symlink()
        or (hasattr(path, "is_junction") and path.is_junction())
        or not path.is_dir()
    ):
        raise ValueError(f"必须是现有普通目录，不能是符号链接：{path}")


def asset_files(root, directory=None):
    current = directory or root
    with os.scandir(current) as entries:
        ordered = sorted(entries, key=lambda item: item.name)
    for item in ordered:
        path = Path(item.path)
        safe_relative(path.relative_to(root).as_posix())
        if item.is_symlink() or (hasattr(path, "is_junction") and path.is_junction()):
            raise ValueError(f"资源不支持符号链接：{path}")
        if item.is_dir(follow_symlinks=False):
            yield from asset_files(root, path)
        elif item.is_file(follow_symlinks=False):
            yield path
        else:
            raise ValueError(f"资源必须是普通文件或目录：{path}")


def compile_catalog(project, asset_builder=None, manifest=None, resource_root=None):
    if resource_root is not None:
        if manifest is not None:
            raise ValueError("--manifest 与 --resource-root 不能同时指定")
        source = Path(resource_root).absolute()
        require_directory(source)
        return source
    if asset_builder is None:
        source = project / "cache/build/android"
        if manifest is not None or not (source / "catalog.bin").is_file():
            raise ValueError(
                "缺少已编译的 GUID 资源目录；请传入 --asset-builder，"
                "或用 --resource-root 指定已有运行包目录。"
            )
        return source
    if manifest is None:
        manifest = Path("settings/resource-build.toml")
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
        raise ValueError("Android 资产任务编译失败；包内旧资源保持原样，请修复编译诊断后重试。")
    try:
        receipt = json.loads(result.stdout)
        output = receipt["output"]
    except (ValueError, TypeError, KeyError) as error:
        raise ValueError("资源构建未返回有效 output 路径，请检查 asset_build_app 版本。") from error
    if (
        receipt.get("status") != "ok"
        or receipt.get("targetPlatform") != "android"
        or not isinstance(output, str)
    ):
        raise ValueError("资源构建回执目标不是 android，不能复制桌面资源。")
    source = Path(output)
    if not source.is_absolute():
        raise ValueError("资源构建回执 output 必须是绝对目录")
    require_directory(source)
    return source


def copy_asset(source, destination, relative, limit=MAX_ASSET_SIZE):
    before = source.lstat()
    if not stat.S_ISREG(before.st_mode):
        raise ValueError(f"资源必须是普通文件，不能是符号链接：{source}")
    if before.st_size > limit:
        raise ValueError(f"资源超过 {limit} 字节上限：{source}")
    destination.parent.mkdir(parents=True, exist_ok=True)
    size = 0
    checksum = FNV_OFFSET
    with source.open("rb") as input_file, destination.open("xb") as output_file:
        opened = os.fstat(input_file.fileno())
        if not stat.S_ISREG(opened.st_mode) or (opened.st_dev, opened.st_ino) != (
            before.st_dev,
            before.st_ino,
        ):
            raise ValueError(f"准备资源期间文件来源发生变化：{source}")
        while block := input_file.read(1024 * 1024):
            size += len(block)
            if size > limit:
                raise ValueError(f"读取期间资源超过 {limit} 字节上限：{source}")
            output_file.write(block)
            for value in block:
                checksum = ((checksum ^ value) * FNV_PRIME) & FNV_MASK
    if size != before.st_size:
        raise ValueError(f"准备资源期间文件长度发生变化：{source}")
    return {"path": relative, "size": size, "hash": f"{checksum:016x}"}


def json_bytes(value):
    return json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":")).encode(
        "utf-8"
    )


def stage_assets(source, staging):
    require_directory(source)
    snapshot = read_catalog(source)
    expected = parse_catalog(snapshot)
    carriers = {"catalog.bin": MAX_MANIFEST_SIZE}
    for entry in expected:
        root, separator, _ = entry["path"].partition("/")
        if root not in ("gen", "packs") or not separator:
            raise ValueError("GUID 资源载体必须位于 gen/ 或 packs/ 下")
        carriers[entry["path"]] = max(
            carriers.get(entry["path"], 0), entry["offset"] + entry["size"]
        )
    target = staging
    files = []
    description_size = 0
    for relative, limit in sorted(carriers.items()):
        path = safe_file(source, relative)
        item = copy_asset(path, target / relative, relative, limit)
        description_size += len(json_bytes(item)) + 1
        if description_size > MAX_MANIFEST_SIZE:
            raise ValueError("Android 项目资源清单超过 4 MiB，请减少单包资源数量。")
        files.append(item)
    if read_catalog(target) != snapshot:
        raise ValueError("复制期间 GUID catalog 发生变化，请重新打包。")
    for entry in expected:
        validate_entity(target / entry["path"], entry)
    manifest = {"entry": ENTRY, "files": files, "deleted": []}
    manifest["revision"] = "apk-" + hashlib.sha256(json_bytes(manifest)).hexdigest()
    encoded = json_bytes(manifest) + b"\n"
    if len(encoded) > MAX_MANIFEST_SIZE:
        raise ValueError("Android 项目资源清单超过 4 MiB，请减少单包资源数量。")
    (staging / RESOURCE_MANIFEST).write_bytes(encoded)
    return manifest


def prepare(project, output, asset_builder=None, manifest=None, resource_root=None):
    project = Path(project).absolute()
    require_directory(project)
    for name in ("settings", "assets", "cache"):
        require_directory(project / name)
    project = project.resolve()
    output = plain_path(output).resolve()
    if output.is_relative_to(project) or project.is_relative_to(output):
        raise ValueError(
            "Android 包资源输出与项目目录不能相互包含，请使用 CMake 的 android/assets 目录。"
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
                copy_asset(path, preserved / relative, relative)
        source = compile_catalog(project, asset_builder, manifest, resource_root)
        manifest = stage_assets(source, staging)
    return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--project", required=True, type=Path, help="含 settings/cache/assets 的项目目录"
    )
    parser.add_argument(
        "--output", required=True, type=Path, help="CMake binary/android/assets 目录"
    )
    parser.add_argument(
        "--asset-builder", type=Path, help="宿主 asset_build_app 路径，编译 Android GUID 产物"
    )
    parser.add_argument(
        "--manifest", type=Path, help="项目构建清单；默认 settings/resource-build.toml"
    )
    parser.add_argument("--resource-root", type=Path, help="已有 v2 运行包目录；指定后不调用构建器")
    arguments = parser.parse_args()
    manifest = prepare(
        arguments.project,
        arguments.output,
        arguments.asset_builder,
        arguments.manifest,
        arguments.resource_root,
    )
    print(
        f"Android 项目资源已准备：{arguments.output}（{len(manifest['files'])} 个文件，"
        f"版本 {manifest['revision']}）"
    )
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError, UnicodeError) as error:
        print(f"Android 资源准备未完成：{error}", file=sys.stderr)
        sys.exit(1)
