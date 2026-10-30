"""关闭脚本插件后仍可导入资源、运行编辑器，并明确拒绝脚本玩法启动。"""

import argparse
from pathlib import Path
import tempfile

from editor_lifecycle_test import launch, prepare_project, read_json, terminate, tool, wait_for
from script_tooling_test import require


def test(editor, mcp, root, temporary):
    project = prepare_project(root, temporary)
    runtime = temporary / "runtime"
    log = temporary / "editor.log"
    process = launch(editor, project, runtime, log)
    try:
        status = wait_for(
            process, lambda: read_json(runtime / "project-status.json"), "editor without script"
        )
        require(status["running"] is False, "editor must start idle without a script runtime")
        require((project / "cache/catalog.bin").is_file(), "asset import requires no V8 runtime")
        rejected = tool(mcp, project, runtime, "start_project", expect_error=True)
        require(
            "No script runtime was provided by the host" in str(rejected),
            f"missing script plugin must have an explicit startup error: {rejected}",
        )
        require(process.poll() is None, "failed project startup must preserve the editor")
        stopped = tool(mcp, project, runtime, "stop_project")
        require(stopped["running"] is False, "stopping an unavailable gameplay must be safe")
    finally:
        terminate(process)

    process = launch(editor, project, runtime, log, frames=4)
    try:
        require(process.wait(timeout=30) == 0, "no-script editor must render and close normally")
        require(
            "stopped after 4 rendered frames" in log.read_text(encoding="utf-8"),
            "real GPU frames required",
        )
    finally:
        terminate(process)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--editor", type=Path, required=True)
    parser.add_argument("--mcp", type=Path, required=True)
    parser.add_argument("--root", type=Path, required=True)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="engine-no-script-") as temporary:
        test(
            args.editor.resolve(),
            args.mcp.resolve(),
            args.root.resolve(),
            Path(temporary).resolve(),
        )
