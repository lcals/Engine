"""编译项目 TS 并事务更新 Android 包内 project 资源和清单，保留独立 v8 资源。"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import stat
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
    if (not path or path.startswith("/") or any(char in path for char in ("\\", ":", "\0"))
            or any(part in ("", ".", "..") for part in path.split("/"))):
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


def compile_project(project):
    node = shutil.which("node")
    if node is None:
        raise ValueError("没有找到 Node.js。请安装 Node.js 并将 node 加入 PATH 后重试。")
    compiler = project / "settings/node_modules/typescript"
    if not compiler.is_dir():
        raise ValueError(
            "项目尚未准备 TypeScript 依赖。请按 README 在项目 settings 目录运行 npm ci 后重试。")
    result = subprocess.run(
        [node, str(ROOT / "scripts/build_scripts.mjs"), "--project", str(project)],
        cwd=ROOT, stdout=sys.stderr, check=False)
    if result.returncode != 0:
        raise ValueError(
            "TypeScript 编译失败，Android 包内资源保持原样。请修复上述诊断后重试；"
            "依赖缺失时先在项目 settings 目录运行 npm ci。")


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
        if (not stat.S_ISREG(opened.st_mode)
                or (opened.st_dev, opened.st_ino) != (before.st_dev, before.st_ino)):
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
    return json.dumps(value, ensure_ascii=False, sort_keys=True,
                      separators=(",", ":")).encode("utf-8")


def stage_assets(source, staging):
    require_directory(source)
    target = staging / "project"
    target.mkdir()
    files = []
    description_size = 0
    for path in asset_files(source):
        relative = safe_relative(path.relative_to(source).as_posix())
        item = copy_asset(path, target / relative, relative)
        description_size += len(json_bytes(item)) + 1
        if description_size > MAX_MANIFEST_SIZE:
            raise ValueError("Android 项目资源清单超过 4 MiB，请减少单包资源数量。")
        files.append(item)
    files.sort(key=lambda item: item["path"])
    if not any(item["path"] == ENTRY for item in files):
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
                f"资源发布失败且回滚未完成；备份保留在 {staging}，请恢复后重试："
                f"{restore_error}") from error
        raise ValueError(f"资源发布失败，已恢复上一份输出，请检查目录权限后重试：{error}") from error
    finally:
        if not preserve_backup:
            shutil.rmtree(staging)


def prepare(project, output):
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
        raise ValueError("Android 包资源输出与项目目录不能相互包含，请使用 CMake 的 android/assets 目录。")
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
            f"Android 资源正在准备或上次任务中断：{lock}。确认没有打包任务后移除此锁目录再重试。") from error
    try:
        compile_project(project)
        staging = Path(tempfile.mkdtemp(prefix=".project-prepare-", dir=output))
        try:
            manifest = stage_assets(project / "cache/assets", staging)
        except BaseException:
            shutil.rmtree(staging)
            raise
        publish_assets(staging, output)
        return manifest
    finally:
        lock.rmdir()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--project", required=True, type=Path, help="含 settings/cache/assets 的项目目录")
    parser.add_argument("--output", required=True, type=Path, help="CMake binary/android/assets 目录")
    arguments = parser.parse_args()
    manifest = prepare(arguments.project, arguments.output)
    print(f"Android 项目资源已准备：{arguments.output}（{len(manifest['files'])} 个文件，"
          f"版本 {manifest['revision']}）")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError, UnicodeError) as error:
        print(f"Android 资源准备未完成：{error}", file=sys.stderr)
        sys.exit(1)
