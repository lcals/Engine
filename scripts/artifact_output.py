"""Validate artifact paths and publish directories, retaining the previous backup if rollback fails."""

from contextlib import contextmanager
import os
from pathlib import Path
import shutil
import tempfile


def plain_path(path):
    path = Path(os.path.abspath(path))
    for parent in (path, *path.parents):
        if parent.is_symlink() or (hasattr(parent, "is_junction") and parent.is_junction()):
            raise ValueError(f"Artifact path must not contain links: {parent}")
    return path


def validate_output(output, root, sources):
    output = plain_path(output)
    root = plain_path(root)
    if output == root or not output.is_relative_to(root):
        raise ValueError(f"Artifact must be under a dedicated output root: {root}")
    for source in sources:
        source = plain_path(source)
        if output.is_relative_to(source) or source.is_relative_to(output):
            raise ValueError(f"Output and source must not contain each other: {output} / {source}")
    if output.exists() and not output.is_dir():
        raise ValueError(f"Artifact output must be a directory: {output}")
    return output


@contextmanager
def staged_output(output):
    output.parent.mkdir(parents=True, exist_ok=True)
    lock = output.parent / ("." + output.name + "-export-lock")
    try:
        lock.mkdir()
    except FileExistsError as error:
        raise ValueError(f"Export is active or an earlier task was interrupted; confirm no task is running before removing the lock: {lock}") from error
    temporary = None
    preserve = False
    try:
        temporary = Path(tempfile.mkdtemp(prefix="." + output.name + "-export-", dir=output.parent))
        stage = temporary / "next"
        previous = temporary / "previous"
        stage.mkdir()
        yield stage
        # 全部复制、校验和平台签名成功后才替换现有产物。
        try:
            if output.exists():
                os.replace(output, previous)
            os.replace(stage, output)
        except BaseException as error:
            if previous.exists():
                try:
                    # 信号可能在 rename 已完成后送达；新输出也可能已安装。
                    if output.exists():
                        os.replace(output, stage)
                    os.replace(previous, output)
                except BaseException as restore_error:
                    preserve = True
                    raise ValueError(
                        f"Artifact publication and rollback failed; previous version preserved at {previous}; restore it before retrying: {restore_error}"
                    ) from error
            raise
    finally:
        if temporary is not None and not preserve:
            shutil.rmtree(temporary)
        lock.rmdir()
