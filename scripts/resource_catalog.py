"""Shared runtime catalog and entity validation for desktop and Android delivery."""

import json
import os
from pathlib import Path
import re
import stat
import struct

MAX_ASSET_SIZE = 64 * 1024 * 1024
MAX_SCRIPT_SIZE = 4 * 1024 * 1024
MAX_MANIFEST_SIZE = 4 * 1024 * 1024
ENTRY = "scripts/game.js"
FNV_OFFSET = 14695981039346656037
FNV_PRIME = 1099511628211
FNV_MASK = (1 << 64) - 1


def parse_build_output(text, target_platform):
    """Read the absolute output directory from a build receipt; callers check existence as needed."""
    try:
        receipt = json.loads(text)
    except (ValueError, TypeError) as error:
        raise ValueError("Resource build did not return a valid JSON receipt") from error
    if not isinstance(receipt, dict):
        raise ValueError("Resource build receipt must be a JSON object")
    if receipt.get("status") != "ok" or receipt.get("targetPlatform") != target_platform:
        raise ValueError(f"Resource build receipt target must be {target_platform} with status ok")
    output = receipt.get("output")
    if not isinstance(output, str) or not output:
        raise ValueError("Resource build receipt output must be a nonempty directory string")
    path = Path(output)
    if not path.is_absolute():
        raise ValueError("Resource build receipt output must be an absolute directory")
    return path


def safe_relative(path):
    if (
        not path
        or path.startswith("/")
        or any(char in path for char in ("\\", ":"))
        or any(ord(char) < 32 or ord(char) == 127 for char in path)
        or any(part in ("", ".", "..") for part in path.split("/"))
    ):
        raise ValueError(f"Unsafe relative resource path: {path!r}")
    # 不让无法表达为 UTF-8 的系统文件名进入 JSON 或 SDL 资源接口。
    path.encode("utf-8")
    return path


def logical_path(path):
    """Validate runtime logical names and storage paths separately; only engine resources use namespaces."""
    if path.startswith("builtin://"):
        safe_relative(path[len("builtin://") :])
    else:
        safe_relative(path)
    return path


class FlatBuffer:
    """Read required scalars, tables and strings without loading package payloads into the catalog model."""

    def __init__(self, data, identifier):
        self.data = data
        if len(data) < 8 or data[4:8] != identifier:
            raise ValueError("Invalid FlatBuffers identifier or length")
        self.root = self.scalar(0, "<I")

    def scalar(self, offset, fmt):
        size = struct.calcsize(fmt)
        if offset is None or offset < 0 or offset + size > len(self.data):
            raise ValueError("FlatBuffers field is out of bounds")
        return struct.unpack_from(fmt, self.data, offset)[0]

    def field(self, table, index):
        vtable = table - self.scalar(table, "<i")
        length = self.scalar(vtable, "<H")
        object_size = self.scalar(vtable + 2, "<H")
        if length < 4 or vtable + length > len(self.data) or table + object_size > len(self.data):
            raise ValueError("FlatBuffers table is out of bounds")
        location = vtable + 4 + index * 2
        relative = self.scalar(location, "<H") if location + 2 <= vtable + length else 0
        if relative and (relative < 4 or relative >= object_size):
            raise ValueError("Invalid FlatBuffers table field offset")
        return table + relative if relative else None

    def value(self, table, index, fmt, default=0):
        location = self.field(table, index)
        return self.scalar(location, fmt) if location is not None else default

    def pointer(self, location):
        relative = self.scalar(location, "<I")
        target = location + relative
        if relative == 0 or target >= len(self.data):
            raise ValueError("FlatBuffers relative pointer is out of bounds")
        return target

    def string_at(self, location):
        start = self.pointer(location)
        length = self.scalar(start, "<I")
        end = start + 4 + length
        if end >= len(self.data) or self.data[end] != 0:
            raise ValueError("FlatBuffers string is out of bounds")
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
            raise ValueError("FlatBuffers vector is out of bounds")
        return [vector + 4 + index * 4 for index in range(count)]


def read_catalog(source):
    path = safe_file(source, "catalog.bin")
    with path.open("rb") as stream:
        snapshot = stream.read(MAX_MANIFEST_SIZE + 1)
    if len(snapshot) > MAX_MANIFEST_SIZE:
        raise ValueError("GUID catalog.bin exceeds the catalog size limit")
    return snapshot


def catalog_entries(source, target_platform="android"):
    return parse_catalog(read_catalog(source), target_platform)


def parse_catalog(snapshot, target_platform="android"):
    data = FlatBuffer(snapshot, b"EASC")
    if data.value(data.root, 0, "<I", 1) != 2:
        raise ValueError("Unsupported GUID catalog version; rebuild v2 runtime resources")
    if not data.string(data.root, 1):
        raise ValueError("GUID catalog is missing its revision")
    if data.string(data.root, 2) != target_platform:
        raise ValueError(f"Resource catalog target must be {target_platform}; resources from other platforms cannot be mixed.")
    entries = []
    identities, paths, modules = set(), set(), set()
    guid_pattern = r"[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}"
    for pointer in data.vector(data.root, 3):
        asset = data.pointer(pointer)
        guid = data.string(asset, 0)
        logical = logical_path(data.string(asset, 1))
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
            or kind > 20  # AssetKind in asset_catalog.fbs ends with movie=20.
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
            raise ValueError("Invalid GUID catalog identity, path, location or entity header")
        if len(dependencies) != len(set(dependencies)) or any(
            not re.fullmatch(guid_pattern, dep) for dep in dependencies
        ):
            raise ValueError("Invalid GUID catalog dependency identity")
        if kind == 1:
            safe_relative(module)
            if (
                not module.endswith(".js")
                or module in modules
                or size - header_size > MAX_SCRIPT_SIZE
            ):
                raise ValueError("GUID catalog script module is invalid or exceeds the limit")
            modules.add(module)
        if size > MAX_ASSET_SIZE:
            raise ValueError("GUID catalog resource exceeds the size limit")
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
    # 运行包包含引擎内置实体，所有必要依赖都必须在本目录中，不借用旧包或宿主定义。
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
                raise ValueError("GUID catalog required dependencies contain a cycle")
            visiting.add(current)
            pending.append((current, True))
            for dependency in edges[current]:
                if dependency in edges:
                    pending.append((dependency, False))
                else:
                    raise ValueError("GUID catalog required dependency is missing")
    if ENTRY not in modules:
        raise ValueError("GUID catalog is missing the scripts/game.js entry module")
    return entries


def safe_file(root, relative):
    safe_relative(relative)
    current = root
    for component in relative.split("/"):
        current /= component
        if current.is_symlink():
            raise ValueError(f"Resources do not support symbolic links: {current}")
    if not current.is_file():
        raise ValueError(f"Resource must be a regular file: {current}")
    return current


def copy_asset(source, destination, limit=MAX_ASSET_SIZE):
    before = source.lstat()
    if not stat.S_ISREG(before.st_mode):
        raise ValueError(f"Resource must be a regular file, not a symbolic link: {source}")
    if limit is None:
        limit = before.st_size
    if before.st_size > limit:
        raise ValueError(f"Resource exceeds the {limit} byte limit: {source}")
    destination.parent.mkdir(parents=True, exist_ok=True)
    size = 0
    with source.open("rb") as input_file, destination.open("xb") as output_file:
        opened = os.fstat(input_file.fileno())
        if not stat.S_ISREG(opened.st_mode) or (opened.st_dev, opened.st_ino) != (
            before.st_dev,
            before.st_ino,
        ):
            raise ValueError(f"Resource source changed during preparation: {source}")
        while block := input_file.read(1024 * 1024):
            size += len(block)
            if size > limit:
                raise ValueError(f"Resource exceeded the {limit} byte limit while reading: {source}")
            output_file.write(block)
    if size != before.st_size:
        raise ValueError(f"Resource length changed during preparation: {source}")


def copy_resource_snapshot(
    source,
    destination,
    target_platform,
    *,
    carrier_roots=None,
    exact_carrier_size=False,
    require_unchanged_catalog=False,
):
    """Pin the catalog, copy unique containers and validate every entity; platform entry points publish the result."""
    snapshot = read_catalog(source)
    expected = parse_catalog(snapshot, target_platform)
    carriers = {}
    for entry in expected:
        root, separator, _ = entry["path"].partition("/")
        if carrier_roots is not None and (root not in carrier_roots or not separator):
            roots = " or ".join(name + "/" for name in carrier_roots)
            raise ValueError(f"GUID resource containers must be under {roots} paths")
        carriers[entry["path"]] = max(
            carriers.get(entry["path"], 0), entry["offset"] + entry["size"]
        )
    # 使用已验证的目录快照，实体必须与这一版目录匹配。
    catalog_path = destination / "catalog.bin"
    if catalog_path.exists() or catalog_path.is_symlink():
        raise ValueError("Resource catalog conflicts with a deployed file")
    with catalog_path.open("xb") as stream:
        stream.write(snapshot)
    for relative, extent in sorted(carriers.items()):
        target = destination / relative
        if target.exists() or target.is_symlink():
            raise ValueError(f"Resource path conflicts with a deployed file: {relative}")
        copy_asset(safe_file(source, relative), target, extent if exact_carrier_size else None)
    if read_catalog(destination) != snapshot or (
        require_unchanged_catalog and read_catalog(source) != snapshot
    ):
        raise ValueError("GUID catalog changed during copying; package resources again.")
    for entry in expected:
        validate_entity(safe_file(destination, entry["path"]), entry)
    catalog = FlatBuffer(snapshot, b"EASC")
    return {
        "revision": catalog.string(catalog.root, 1),
        "resource_count": len(expected),
        "file_count": len(carriers) + 1,
    }


def validate_entity(path, entry):
    with path.open("rb") as stream:
        stream.seek(entry["offset"])
        prefix = stream.read(8)
        if (
            len(prefix) != 8
            or prefix[:4] != b"ERS1"
            or struct.unpack_from("<I", prefix, 4)[0] != entry["header_size"] - 8
        ):
            raise ValueError(f"Invalid resource entity header: {entry['guid']}")
        header = FlatBuffer(stream.read(entry["header_size"] - 8), b"ERSH")
        if (
            header.string(header.root, 0) != entry["guid"]
            or header.value(header.root, 1, "<B") != entry["kind"]
            or header.value(header.root, 2, "<I", 1) != entry["format_version"]
            or header.value(header.root, 3, "<Q") != entry["header_size"]
            or header.value(header.root, 4, "<Q") != entry["size"] - entry["header_size"]
        ):
            raise ValueError(f"Resource entity header does not match catalog: {entry['guid']}")
        stream.seek(entry["offset"])
        remaining, checksum = entry["size"], FNV_OFFSET
        while remaining:
            block = stream.read(min(remaining, 1024 * 1024))
            if not block:
                raise ValueError(f"GUID resource checksum mismatch with catalog: {entry['guid']}")
            remaining -= len(block)
            for value in block:
                checksum = ((checksum ^ value) * FNV_PRIME) & FNV_MASK
        if f"{checksum:016x}" != entry["hash"]:
            raise ValueError(f"GUID resource checksum mismatch with catalog: {entry['guid']}")
