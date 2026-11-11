"""Collect module runtime TOML and export common settings plus the current target platform."""

import json
import tomllib
from pathlib import Path

from artifact_output import plain_path

PLATFORMS = {"windows", "macos", "linux", "android", "ios", "web"}


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
        raise ValueError(f"{source.name} must be a regular file of at most 64 KiB")
    document = tomllib.loads(source.read_text(encoding="utf-8"))
    if set(document) - {"common", "quality", "platform"} or any(
        not isinstance(value, dict) for value in document.values()
    ):
        raise ValueError(f"{source.name} accepts only common/quality/platform tables")
    if document.get("quality", {}):
        raise ValueError(f"{source.name} quality settings require a gameplay selection and are not enabled yet")
    platforms = document.get("platform", {})
    if set(platforms) - PLATFORMS or any(not isinstance(value, dict) for value in platforms.values()):
        raise ValueError(f"{source.name} contains an invalid platform table")
    return compact(merge(document.get("common", {}), platforms.get(platform, {})))


def export_runtime_settings(project, destination, platform):
    if platform not in PLATFORMS:
        raise ValueError(f"Invalid runtime configuration target platform: {platform}")
    settings = plain_path(Path(project) / "settings")
    document = {}
    for source in sorted(settings.glob("runtime-*.toml")):
        module = source.stem.removeprefix("runtime-")
        if not module:
            raise ValueError("Runtime configuration must specify a module name")
        value = read_source(source, platform)
        if value:
            document[module] = value
    if not {"game", "engine", "parallel"} <= document.keys():
        raise ValueError("Runtime configuration must explicitly provide game, engine and parallel modules")
    try:
        payload = (json.dumps(document, ensure_ascii=False, allow_nan=False, indent=2) + "\n").encode("utf-8")
    except (TypeError, ValueError) as error:
        raise ValueError("Runtime configuration does not support dates or nonfinite numbers") from error
    if len(payload) > 65536:
        raise ValueError("Combined runtime.json must not exceed 64 KiB")
    target = Path(destination) / "settings/runtime.json"
    if target.exists():
        raise ValueError("Runtime resources must not overwrite host configuration settings/runtime.json")
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_bytes(payload)
