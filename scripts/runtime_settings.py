"""项目 TOML 汇总为唯一运行配置；不部署编辑器设置或源配置。"""

import json
import tomllib
from pathlib import Path

from artifact_output import plain_path


def read_source(project, name):
    source = plain_path(Path(project) / "settings" / name)
    if not source.exists():
        return {"version": 1}
    if not source.is_file() or source.stat().st_size > 65536:
        raise ValueError(f"{name} 必须是至多 64 KiB 的普通文件")
    document = tomllib.loads(source.read_text(encoding="utf-8"))
    if type(document.get("version")) is not int or document["version"] != 1:
        raise ValueError(f"{name} 必须使用 version 1")
    if set(document) - {"version", "defaults", "profiles", "overrides"}:
        raise ValueError(f"{name} 包含未知顶层字段")
    return document


def attach_parallel(layer, parallel):
    if not isinstance(layer, dict) or not isinstance(parallel, dict) or "parallel" in layer:
        raise ValueError("并行配置层无效或在两个源文件重复定义")
    layer["parallel"] = parallel


def export_runtime_settings(project, destination):
    document = read_source(project, "runtime.toml")
    parallel = read_source(project, "parallel.toml")
    for name in ("defaults", "overrides"):
        if name in parallel:
            attach_parallel(document.setdefault(name, {}), parallel[name])
    if "profiles" in parallel:
        source_profiles = parallel["profiles"]
        target_profiles = document.setdefault("profiles", {})
        if not isinstance(source_profiles, dict) or not isinstance(target_profiles, dict):
            raise ValueError("运行配置 profiles 必须为表")
        for name, layer in source_profiles.items():
            attach_parallel(target_profiles.setdefault(name, {}), layer)
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
