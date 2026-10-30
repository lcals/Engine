"""产物目录检查及整目录发布；回滚失败时保留上一版备份。"""

from contextlib import contextmanager
import os
from pathlib import Path
import shutil
import tempfile


def plain_path(path):
    path = Path(os.path.abspath(path))
    for parent in (path, *path.parents):
        if parent.is_symlink() or (hasattr(parent, "is_junction") and parent.is_junction()):
            raise ValueError(f"产物路径不能包含链接：{parent}")
    return path


def validate_output(output, root, sources):
    output = plain_path(output)
    root = plain_path(root)
    if output == root or not output.is_relative_to(root):
        raise ValueError(f"产物必须位于专用输出根目录的子目录：{root}")
    for source in sources:
        source = plain_path(source)
        if output.is_relative_to(source) or source.is_relative_to(output):
            raise ValueError(f"输出与来源不能相互包含：{output} / {source}")
    if output.exists() and not output.is_dir():
        raise ValueError(f"产物输出必须是目录：{output}")
    return output


@contextmanager
def staged_output(output):
    output.parent.mkdir(parents=True, exist_ok=True)
    lock = output.parent / ("." + output.name + "-export-lock")
    try:
        lock.mkdir()
    except FileExistsError as error:
        raise ValueError(f"该产物正在导出，或上次任务中断；确认无任务后移除锁：{lock}") from error
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
                        f"产物发布及回滚失败，旧版保留在 {previous}，请恢复后重试：{restore_error}"
                    ) from error
            raise
    finally:
        if temporary is not None and not preserve:
            shutil.rmtree(temporary)
        lock.rmdir()
