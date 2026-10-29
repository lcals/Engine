"""编译 GUID 资源并事务更新 Android 包内 catalog/gen 和清单，保留独立 v8 资源。"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import stat
import struct
import re
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
MAX_ASSET_SIZE = 64 * 1024 * 1024
MAX_SCRIPT_SIZE = 4 * 1024 * 1024
MAX_MANIFEST_SIZE = 4 * 1024 * 1024
ENTRY = "scripts/game.js"
FNV_OFFSET = 14695981039346656037
FNV_PRIME = 1099511628211
FNV_MASK = (1 << 64) - 1


def require_directory(path):
    if path.is_symlink() or not path.is_dir():
        raise ValueError(f"必须是现有普通目录，不能是符号链接：{path}")


def safe_relative(path):
    if (
        not path
        or path.startswith("/")
        or any(char in path for char in ("\\", ":", "\0"))
        or any(part in ("", ".", "..") for part in path.split("/"))
    ):
        raise ValueError(f"资源相对路径不安全：{path!r}")
    # 不让无法表达为 UTF-8 的系统文件名进入 JSON 或 SDL 资源接口。
    path.encode("utf-8")
    return path


def asset_files(root, directory=None):
    current = directory or root
    with os.scandir(current) as entries:
        ordered = sorted(entries, key=lambda item: item.name)
    for item in ordered:
        path = Path(item.path)
        safe_relative(path.relative_to(root).as_posix())
        if item.is_symlink():
            raise ValueError(f"资源不支持符号链接：{path}")
        if item.is_dir(follow_symlinks=False):
            yield from asset_files(root, path)
        elif item.is_file(follow_symlinks=False):
            yield path
        else:
            raise ValueError(f"资源必须是普通文件或目录：{path}")


def compile_catalog(project, asset_builder=None):
    if asset_builder is None:
        if not (project / "cache/catalog.bin").is_file():
            raise ValueError(
                "缺少已编译的 GUID 资源目录；请传入 --asset-builder，或先用 asset_build_app --target android 编译项目。"
            )
        return
    asset_builder = Path(asset_builder).resolve()
    result = subprocess.run(
        [str(asset_builder), "--project", str(project), "--target", "android"],
        cwd=ROOT,
        stdout=sys.stderr,
        check=False,
    )
    if result.returncode != 0:
        raise ValueError("Android 资产任务编译失败；包内旧资源保持原样，请修复编译诊断后重试。")


def catalog_entries(source):
    # FlatBuffers catalog 是运行时契约；打包工具只读取它，不依赖编辑器 SQLite。
    catalog_path = source / "catalog.bin"
    if catalog_path.is_symlink() or not catalog_path.is_file():
        raise ValueError("GUID catalog.bin 必须是普通文件，不能是符号链接")
    data = catalog_path.read_bytes()
    if len(data) > MAX_MANIFEST_SIZE or data[4:8] != b"EASC":
        raise ValueError("GUID catalog.bin 标识或长度无效")

    def scalar(offset, fmt):
        size = struct.calcsize(fmt)
        if offset < 0 or offset + size > len(data):
            raise ValueError("GUID catalog.bin 越界")
        return struct.unpack_from(fmt, data, offset)[0]

    def field(table, index):
        vtable = table - scalar(table, "<i")
        length = scalar(vtable, "<H")
        location = vtable + 4 + index * 2
        relative = scalar(location, "<H") if location + 2 <= vtable + length else 0
        return table + relative if relative else None

    def pointer(location):
        if location is None:
            raise ValueError("GUID catalog.bin 缺少必需字段")
        return location + scalar(location, "<I")

    def string(table, index, required=True):
        location = field(table, index)
        if location is None:
            if not required:
                return ""
            raise ValueError("GUID catalog.bin 缺少字符串")
        start = pointer(location)
        length = scalar(start, "<I")
        end = start + 4 + length
        if end >= len(data) or data[end] != 0:
            raise ValueError("GUID catalog.bin 字符串越界")
        return data[start + 4 : end].decode("utf-8")

    table = scalar(0, "<I")
    version = field(table, 0)
    if (scalar(version, "<I") if version is not None else 1) != 1:
        raise ValueError("不支持的 GUID catalog 版本")
    if string(table, 2) != "android":
        raise ValueError("资源目录目标必须是 android；不能把桌面 Metal 产物打进 Android 包。")
    vector = pointer(field(table, 3))
    count = scalar(vector, "<I")
    if count > 65536 or count > len(data) // 4:
        raise ValueError("GUID catalog 条目数量越界")
    entries = []
    identities = set()
    paths = set()
    modules = set()
    for index in range(count):
        asset = pointer(vector + 4 + index * 4)
        guid = string(asset, 0)
        path = safe_relative(string(asset, 1))
        kind_field = field(asset, 2)
        kind = scalar(kind_field, "<B") if kind_field is not None else 0
        checksum = string(asset, 3)
        size_field = field(asset, 4)
        size = scalar(size_field, "<Q") if size_field is not None else 0
        module = string(asset, 6, False)
        if (
            not re.fullmatch(r"[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}", guid)
            or guid in identities
            or path in paths
            or kind > 5
            or not re.fullmatch(r"[0-9a-f]{16}", checksum)
        ):
            raise ValueError("GUID catalog 条目身份、路径或校验值无效")
        if kind == 1:
            safe_relative(module)
            if not module.endswith(".js") or module in modules or size > MAX_SCRIPT_SIZE:
                raise ValueError("GUID catalog 脚本模块无效或超过上限")
            modules.add(module)
        if size > MAX_ASSET_SIZE:
            raise ValueError("GUID catalog 资源超过大小上限")
        identities.add(guid)
        paths.add(path)
        entries.append({"path": "gen/" + guid, "size": size, "hash": checksum})
    if ENTRY not in modules:
        raise ValueError("GUID catalog 缺少 scripts/game.js 入口模块")
    return entries


def copy_asset(source, destination, relative):
    before = source.lstat()
    if not stat.S_ISREG(before.st_mode):
        raise ValueError(f"资源必须是普通文件，不能是符号链接：{source}")
    limit = MAX_SCRIPT_SIZE if relative.endswith((".js", ".mjs")) else MAX_ASSET_SIZE
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
    target = staging / "project"
    target.mkdir()
    files = []
    description_size = 0
    guid_catalog = (source / "catalog.bin").exists()
    expected = {item["path"]: item for item in catalog_entries(source)} if guid_catalog else {}
    if guid_catalog:
        require_directory(source / "gen")
    paths = (
        [source / "catalog.bin"] + [source / relative for relative in expected]
        if guid_catalog
        else asset_files(source)
    )
    for path in paths:
        relative = safe_relative(path.relative_to(source).as_posix())
        item = copy_asset(path, target / relative, relative)
        if relative in expected and item != expected[relative]:
            raise ValueError(f"GUID 资源与 catalog 内容校验不符：{relative}")
        description_size += len(json_bytes(item)) + 1
        if description_size > MAX_MANIFEST_SIZE:
            raise ValueError("Android 项目资源清单超过 4 MiB，请减少单包资源数量。")
        files.append(item)
    if guid_catalog and {item["path"]: item for item in catalog_entries(target)} != expected:
        raise ValueError("复制期间 GUID catalog 发生变化，请重新打包。")
    files.sort(key=lambda item: item["path"])
    if not guid_catalog and not any(item["path"] == ENTRY for item in files):
        raise ValueError("生成目录缺少 scripts/game.js，请检查 assets/scripts/game.ts。")
    manifest = {"entry": ENTRY, "files": files, "deleted": []}
    manifest["revision"] = "apk-" + hashlib.sha256(json_bytes(manifest)).hexdigest()
    encoded = json_bytes(manifest) + b"\n"
    if len(encoded) > MAX_MANIFEST_SIZE:
        raise ValueError("Android 项目资源清单超过 4 MiB，请减少单包资源数量。")
    (staging / "project-manifest.json").write_bytes(encoded)
    return manifest


def validate_outputs(output):
    project = output / "project"
    manifest = output / "project-manifest.json"
    if project.is_symlink() or (project.exists() and not project.is_dir()):
        raise ValueError(f"包内 project 输出必须是普通目录：{project}")
    if manifest.is_symlink() or (manifest.exists() and not manifest.is_file()):
        raise ValueError(f"包内资源清单输出必须是普通文件：{manifest}")


def publish_assets(staging, output):
    # 同目录改名确保在同一文件系统内。只有两项均发布完成，调用方才能继续 Gradle 打包。
    names = ("project", "project-manifest.json")
    moved = []
    published = []
    preserve_backup = False
    try:
        validate_outputs(output)
        backup = staging / "previous"
        backup.mkdir()
        for name in names:
            if (output / name).exists():
                (output / name).rename(backup / name)
                moved.append(name)
        for name in names:
            (staging / name).rename(output / name)
            published.append(name)
    except BaseException as error:
        try:
            for name in reversed(published):
                (output / name).rename(staging / name)
            for name in reversed(moved):
                (staging / "previous" / name).rename(output / name)
        except BaseException as restore_error:
            preserve_backup = True
            raise ValueError(
                f"资源发布失败且回滚未完成；备份保留在 {staging}，请恢复后重试：" f"{restore_error}"
            ) from error
        raise ValueError(
            f"资源发布失败，已恢复上一份输出，请检查目录权限后重试：{error}"
        ) from error
    finally:
        if not preserve_backup:
            shutil.rmtree(staging)


def prepare(project, output, asset_builder=None):
    project = Path(project).absolute()
    require_directory(project)
    for name in ("settings", "assets", "cache"):
        require_directory(project / name)
    project = project.resolve()
    output = Path(output).absolute()
    if output.is_symlink():
        raise ValueError(f"Android 资源输出不能是符号链接：{output}")
    output = output.resolve()
    if output.is_relative_to(project) or project.is_relative_to(output):
        raise ValueError(
            "Android 包资源输出与项目目录不能相互包含，请使用 CMake 的 android/assets 目录。"
        )
    # 编译前检查 TS 与静态资源树；编译结果也会在复制前独立检查。
    for _ in asset_files(project / "assets"):
        pass
    output.mkdir(parents=True, exist_ok=True)
    require_directory(output)
    validate_outputs(output)
    lock = output / ".project-prepare-lock"
    try:
        lock.mkdir()
    except FileExistsError as error:
        raise ValueError(
            f"Android 资源正在准备或上次任务中断：{lock}。确认没有打包任务后移除此锁目录再重试。"
        ) from error
    try:
        compile_catalog(project, asset_builder)
        source = project / "cache"
        staging = Path(tempfile.mkdtemp(prefix=".project-prepare-", dir=output))
        try:
            manifest = stage_assets(source, staging)
        except BaseException:
            shutil.rmtree(staging)
            raise
        publish_assets(staging, output)
        return manifest
    finally:
        lock.rmdir()


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
    arguments = parser.parse_args()
    manifest = prepare(arguments.project, arguments.output, arguments.asset_builder)
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
