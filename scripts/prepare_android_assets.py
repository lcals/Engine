"""按 v2 资源清单复制散文件或包成员，事务更新 Android 内置资源并保留 v8。"""

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
        or any(char in path for char in ("\\", ":"))
        or any(ord(char) < 32 or ord(char) == 127 for char in path)
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


def compile_catalog(project, asset_builder=None, manifest=None, resource_root=None):
    if resource_root is not None:
        if manifest is not None:
            raise ValueError("--manifest 与 --resource-root 不能同时指定")
        source = Path(resource_root).absolute()
        require_directory(source)
        return source
    if manifest is None and asset_builder is not None:
        default = project / "settings/resource-build-android.toml"
        if default.is_file():
            manifest = Path("settings/resource-build-android.toml")
    if asset_builder is None:
        if manifest is not None or not (project / "cache/catalog.bin").is_file():
            raise ValueError(
                "缺少已编译的 GUID 资源目录；请传入 --asset-builder，"
                "或用 --resource-root 指定已有运行包目录。"
            )
        return project / "cache"
    command = [str(Path(asset_builder).resolve()), "--project", str(project)]
    if manifest is None:
        command += ["--target", "android"]
    else:
        command += ["--manifest", str(manifest)]
    result = subprocess.run(
        command,
        cwd=ROOT,
        stdout=subprocess.PIPE if manifest is not None else sys.stderr,
        text=True,
        check=False,
    )
    if result.returncode != 0:
        raise ValueError("Android 资产任务编译失败；包内旧资源保持原样，请修复编译诊断后重试。")
    if manifest is None:
        return project / "cache"
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


class FlatBuffer:
    """读取所需的标量/表/字符串，不将包载荷读入目录模型。"""

    def __init__(self, data, identifier):
        self.data = data
        if len(data) < 8 or data[4:8] != identifier:
            raise ValueError("FlatBuffers 标识或长度无效")
        self.root = self.scalar(0, "<I")

    def scalar(self, offset, fmt):
        size = struct.calcsize(fmt)
        if offset is None or offset < 0 or offset + size > len(self.data):
            raise ValueError("FlatBuffers 字段越界")
        return struct.unpack_from(fmt, self.data, offset)[0]

    def field(self, table, index):
        vtable = table - self.scalar(table, "<i")
        length = self.scalar(vtable, "<H")
        object_size = self.scalar(vtable + 2, "<H")
        if length < 4 or vtable + length > len(self.data) or table + object_size > len(self.data):
            raise ValueError("FlatBuffers 表越界")
        location = vtable + 4 + index * 2
        relative = self.scalar(location, "<H") if location + 2 <= vtable + length else 0
        if relative and (relative < 4 or relative >= object_size):
            raise ValueError("FlatBuffers 表字段偏移无效")
        return table + relative if relative else None

    def value(self, table, index, fmt, default=0):
        location = self.field(table, index)
        return self.scalar(location, fmt) if location is not None else default

    def pointer(self, location):
        relative = self.scalar(location, "<I")
        target = location + relative
        if relative == 0 or target >= len(self.data):
            raise ValueError("FlatBuffers 相对指针越界")
        return target

    def string_at(self, location):
        start = self.pointer(location)
        length = self.scalar(start, "<I")
        end = start + 4 + length
        if end >= len(self.data) or self.data[end] != 0:
            raise ValueError("FlatBuffers 字符串越界")
        return self.data[start + 4 : end].decode("utf-8")

    def string(self, table, index, required=True):
        location = self.field(table, index)
        if location is None and not required:
            return ""
        return self.string_at(location)

    def vector(self, table, index, required=True):
        location = self.field(table, index)
        if location is None and not required:
            return []
        vector = self.pointer(location)
        count = self.scalar(vector, "<I")
        if count > 65536 or vector + 4 + count * 4 > len(self.data):
            raise ValueError("FlatBuffers vector 越界")
        return [vector + 4 + index * 4 for index in range(count)]


def catalog_entries(source):
    path = safe_file(source, "catalog.bin")
    if path.stat().st_size > MAX_MANIFEST_SIZE:
        raise ValueError("GUID catalog.bin 超过清单大小上限")
    data = FlatBuffer(path.read_bytes(), b"EASC")
    if data.value(data.root, 0, "<I", 1) != 2:
        raise ValueError("不支持的 GUID catalog 版本；请重新构建 v2 运行资源")
    if not data.string(data.root, 1):
        raise ValueError("GUID catalog 缺少修订标识")
    if data.string(data.root, 2) != "android":
        raise ValueError("资源目录目标必须是 android；不能把桌面 Metal 产物打进 Android 包。")
    entries = []
    identities, paths, modules = set(), set(), set()
    guid_pattern = r"[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}"
    for pointer in data.vector(data.root, 3):
        asset = data.pointer(pointer)
        guid = data.string(asset, 0)
        logical = safe_relative(data.string(asset, 1))
        kind = data.value(asset, 2, "<B")
        checksum = data.string(asset, 3)
        size = data.value(asset, 4, "<Q")
        module = data.string(asset, 6, False)
        version = data.value(asset, 7, "<I", 1)
        header_size = data.value(asset, 8, "<I")
        location = data.pointer(data.field(asset, 9))
        mount = data.string(location, 0)
        relative = safe_relative(data.string(location, 1))
        offset = data.value(location, 2, "<Q")
        length = data.value(location, 3, "<Q")
        dependencies = [data.string_at(item) for item in data.vector(asset, 5, False)]
        if (
            not re.fullmatch(guid_pattern, guid)
            or guid == "00000000-0000-0000-0000-000000000000"
            or guid in identities
            or logical in paths
            or not logical.startswith("project/")
            or kind > 7  # AssetKind.audio is appended after texture; existing values stay stable.
            or version != 1
            or not re.fullmatch(r"[0-9a-f]{16}", checksum)
            or mount != "content"
            or relative == "catalog.bin"
            or header_size < 16
            or header_size > 65536
            or header_size > size
            or length != size
            or offset + length > (1 << 63) - 1
        ):
            raise ValueError("GUID catalog 身份、路径、定位或实体头无效")
        if len(dependencies) != len(set(dependencies)) or any(
            not re.fullmatch(guid_pattern, dep) for dep in dependencies
        ):
            raise ValueError("GUID catalog 依赖身份无效")
        if kind == 1:
            safe_relative(module)
            if (
                not module.endswith(".js")
                or module in modules
                or size - header_size > MAX_SCRIPT_SIZE
            ):
                raise ValueError("GUID catalog 脚本模块无效或超过上限")
            modules.add(module)
        if size > MAX_ASSET_SIZE:
            raise ValueError("GUID catalog 资源超过大小上限")
        identities.add(guid)
        paths.add(logical)
        entries.append(
            {
                "guid": guid,
                "kind": kind,
                "format_version": version,
                "header_size": header_size,
                "path": relative,
                "offset": offset,
                "size": size,
                "hash": checksum,
                "dependencies": dependencies,
            }
        )
    # 内置依赖由运行引擎提供；打包阶段只检查其专用命名空间和正常依赖无环。
    # 完整内置身份校验已经由 asset_build_app 的共享 C++ 元数据接口完成。
    edges = {item["guid"]: item["dependencies"] for item in entries}
    done, visiting = set(), set()
    for guid in edges:
        pending = [(guid, False)]
        while pending:
            current, expanded = pending.pop()
            if current in done:
                continue
            if expanded:
                visiting.remove(current)
                done.add(current)
                continue
            if current in visiting:
                raise ValueError("GUID catalog 必要依赖包含循环")
            visiting.add(current)
            pending.append((current, True))
            for dependency in edges[current]:
                if dependency in edges:
                    pending.append((dependency, False))
                elif not dependency.startswith("00000000-0000-4000-8000-"):
                    raise ValueError("GUID catalog 必要依赖缺失")
    if ENTRY not in modules:
        raise ValueError("GUID catalog 缺少 scripts/game.js 入口模块")
    return entries


def safe_file(root, relative):
    safe_relative(relative)
    current = root
    for component in relative.split("/"):
        current /= component
        if current.is_symlink():
            raise ValueError(f"资源不支持符号链接：{current}")
    if not current.is_file():
        raise ValueError(f"资源必须是普通文件：{current}")
    return current


def validate_entity(path, entry):
    with path.open("rb") as stream:
        stream.seek(entry["offset"])
        prefix = stream.read(8)
        if (
            len(prefix) != 8
            or prefix[:4] != b"ERS1"
            or struct.unpack_from("<I", prefix, 4)[0] != entry["header_size"] - 8
        ):
            raise ValueError(f"资源实体头无效：{entry['guid']}")
        header = FlatBuffer(stream.read(entry["header_size"] - 8), b"ERSH")
        if (
            header.string(header.root, 0) != entry["guid"]
            or header.value(header.root, 1, "<B") != entry["kind"]
            or header.value(header.root, 2, "<I", 1) != entry["format_version"]
            or header.value(header.root, 3, "<Q") != entry["header_size"]
            or header.value(header.root, 4, "<Q") != entry["size"] - entry["header_size"]
        ):
            raise ValueError(f"资源实体头与 catalog 不一致：{entry['guid']}")
        stream.seek(entry["offset"])
        remaining, checksum = entry["size"], FNV_OFFSET
        while remaining:
            block = stream.read(min(remaining, 1024 * 1024))
            if not block:
                raise ValueError(f"GUID 资源与 catalog 内容校验不符：{entry['guid']}")
            remaining -= len(block)
            for value in block:
                checksum = ((checksum ^ value) * FNV_PRIME) & FNV_MASK
        if f"{checksum:016x}" != entry["hash"]:
            raise ValueError(f"GUID 资源与 catalog 内容校验不符：{entry['guid']}")


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
    expected = catalog_entries(source)
    carriers = {"catalog.bin": MAX_MANIFEST_SIZE}
    for entry in expected:
        carriers[entry["path"]] = max(
            carriers.get(entry["path"], 0), entry["offset"] + entry["size"]
        )
    target = staging / "project"
    target.mkdir()
    files = []
    description_size = 0
    for relative, limit in sorted(carriers.items()):
        path = safe_file(source, relative)
        item = copy_asset(path, target / relative, relative, limit)
        description_size += len(json_bytes(item)) + 1
        if description_size > MAX_MANIFEST_SIZE:
            raise ValueError("Android 项目资源清单超过 4 MiB，请减少单包资源数量。")
        files.append(item)
    if catalog_entries(target) != expected:
        raise ValueError("复制期间 GUID catalog 发生变化，请重新打包。")
    for entry in expected:
        validate_entity(target / entry["path"], entry)
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


def prepare(project, output, asset_builder=None, manifest=None, resource_root=None):
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
        source = compile_catalog(project, asset_builder, manifest, resource_root)
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
    parser.add_argument("--manifest", type=Path, help="项目 settings 下的 Android 构建清单")
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
