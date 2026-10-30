"""导出静态 Windows 宿主和独立运行资源，不封装或解压 EXE。"""

import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
from prepare_android_assets import catalog_entries, safe_file, validate_entity


def plain(path):
    path = Path(os.path.abspath(path))
    for parent in (path, *path.parents):
        if parent.is_symlink() or (hasattr(parent, "is_junction") and parent.is_junction()):
            raise ValueError(f"输出或来源路径不能包含链接：{parent}")
    return path


def export(args):
    project = plain(args.project).resolve()
    output = plain(args.output).resolve()
    build = (ROOT / "build").resolve()
    if output == build or not output.is_relative_to(build) or output.is_relative_to(project):
        raise ValueError("导出目录必须在 build/ 内，且不能覆盖源项目")
    result = subprocess.run(
        [
            str(args.builder.resolve()),
            "--project",
            str(project),
            "--manifest",
            args.manifest.as_posix(),
        ],
        cwd=ROOT,
        capture_output=True,
        encoding="utf-8",
        timeout=600,
        check=False,
    )
    if result.returncode != 0:
        raise ValueError(f"项目资源构建失败：\n{result.stdout}\n{result.stderr}")
    receipt = json.loads(result.stdout)
    if receipt.get("status") != "ok" or receipt.get("targetPlatform") != "windows":
        raise ValueError("构建结果不是有效的 Windows 资源目录")
    source = plain(Path(receipt["output"])).resolve(strict=True)
    entries = catalog_entries(source, target_platform="windows")
    carriers = {entry["path"] for entry in entries}
    for entry in entries:
        validate_entity(safe_file(source, entry["path"]), entry)
    output.parent.mkdir(parents=True, exist_ok=True)
    if output.exists() and not output.is_dir():
        raise ValueError("导出路径已存在且不是目录")
    # 先准备完整产物，再替换整个专用导出目录；失败时保留上一版。
    with tempfile.TemporaryDirectory(prefix=".export-", dir=output.parent) as temporary:
        stage = Path(temporary) / "next"
        previous = Path(temporary) / "previous"
        files = [(safe_file(source, relative), relative) for relative in sorted(carriers)]
        files += [(plain(args.executable), args.executable.name), (plain(args.icu), "icudtl.dat")]
        files += [(source / "catalog.bin", "catalog.bin")]
        for origin, relative in files:
            staged = stage / relative
            staged.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(origin, staged)
        if output.exists():
            os.replace(output, previous)
        try:
            os.replace(stage, output)
        except OSError:
            if previous.exists():
                os.replace(previous, output)
            raise
    print(
        json.dumps(
            {
                "status": "ok",
                "output": str(output),
                "executable": args.executable.name,
                "resourceCount": len(entries),
                "resourceFiles": len(carriers),
                "targetPlatform": "windows",
            },
            ensure_ascii=False,
        )
    )


def main():
    sys.stdout.reconfigure(encoding="utf-8")
    sys.stderr.reconfigure(encoding="utf-8")
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--project", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--builder", type=Path, required=True)
    parser.add_argument("--executable", type=Path, required=True)
    parser.add_argument("--icu", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    try:
        export(args)
        return 0
    except (OSError, ValueError, KeyError, subprocess.TimeoutExpired) as error:
        print(f"Windows 项目导出失败：{error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
