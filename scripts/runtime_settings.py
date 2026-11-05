"""收集各模块的运行时 TOML，仅导出公共项和当前目标平台。"""

import json
import tomllib
from pathlib import Path

from artifact_output import plain_path

PLATFORMS = {"windows", "macos", "linux", "android", "ios"}


def merge(target, source):
    for key, value in source.items():
        if isinstance(value, dict) and isinstance(target.get(key), dict):
            merge(target[key], value)
        else:
            target[key] = value
    return target


def compact(value):
    if isinstance(value, dict):
        return {key: item for key, value in value.items() if (item := compact(value)) != {}}
    return value


def read_source(source, platform):
    source = plain_path(source)
    if not source.is_file() or source.stat().st_size > 65536:
        raise ValueError(f"{source.name} 必须是至多 64 KiB 的普通文件")
    document = tomllib.loads(source.read_text(encoding="utf-8"))
    if set(document) - {"common", "quality", "platform"} or any(
        not isinstance(value, dict) for value in document.values()
    ):
        raise ValueError(f"{source.name} 只接受 common/quality/platform 表")
    if document.get("quality", {}):
        raise ValueError(f"{source.name} 的品质配置需要业务选择，目前尚未启用")
    platforms = document.get("platform", {})
    if set(platforms) - PLATFORMS or any(not isinstance(value, dict) for value in platforms.values()):
        raise ValueError(f"{source.name} 包含无效平台表")
    return compact(merge(document.get("common", {}), platforms.get(platform, {})))


def export_runtime_settings(project, destination, platform):
    if platform not in PLATFORMS:
        raise ValueError(f"无效运行配置目标平台：{platform}")
    settings = plain_path(Path(project) / "settings")
    document = {}
    for source in sorted(settings.glob("runtime-*.toml")):
        module = source.stem.removeprefix("runtime-")
        if not module:
            raise ValueError("运行配置必须指定模块名")
        value = read_source(source, platform)
        if value:
            document[module] = value
    if not {"game", "engine", "parallel"} <= document.keys():
        raise ValueError("运行配置必须显式提供 game、engine 和 parallel 模块")
    try:
        payload = (json.dumps(document, ensure_ascii=False, allow_nan=False, indent=2) + "\n").encode("utf-8")
    except (TypeError, ValueError) as error:
        raise ValueError("运行配置不支持日期或非有限数值") from error
    if len(payload) > 65536:
        raise ValueError("汇总 runtime.json 不能超过 64 KiB")
    target = Path(destination) / "settings/runtime.json"
    if target.exists():
        raise ValueError("运行资源不能覆盖宿主配置 settings/runtime.json")
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_bytes(payload)
