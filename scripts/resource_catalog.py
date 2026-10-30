"""桌面与 Android 交付共用的运行资源目录和实体校验。"""

import re
import struct

MAX_ASSET_SIZE = 64 * 1024 * 1024
MAX_SCRIPT_SIZE = 4 * 1024 * 1024
MAX_MANIFEST_SIZE = 4 * 1024 * 1024
ENTRY = "scripts/game.js"
FNV_OFFSET = 14695981039346656037
FNV_PRIME = 1099511628211
FNV_MASK = (1 << 64) - 1


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


def read_catalog(source):
    path = safe_file(source, "catalog.bin")
    with path.open("rb") as stream:
        snapshot = stream.read(MAX_MANIFEST_SIZE + 1)
    if len(snapshot) > MAX_MANIFEST_SIZE:
        raise ValueError("GUID catalog.bin 超过清单大小上限")
    return snapshot


def catalog_entries(source, target_platform="android"):
    return parse_catalog(read_catalog(source), target_platform)


def parse_catalog(snapshot, target_platform="android"):
    data = FlatBuffer(snapshot, b"EASC")
    if data.value(data.root, 0, "<I", 1) != 2:
        raise ValueError("不支持的 GUID catalog 版本；请重新构建 v2 运行资源")
    if not data.string(data.root, 1):
        raise ValueError("GUID catalog 缺少修订标识")
    if data.string(data.root, 2) != target_platform:
        raise ValueError(f"资源目录目标必须是 {target_platform}，不能混用其他平台产物。")
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
            or kind > 12  # Font and UI append to the stable AssetKind values in asset_catalog.fbs.
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
