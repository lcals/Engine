#!/usr/bin/env python3
"""验证已启动 showcase 编辑器的真实 MCP、Gameplay 和 RmlUi 交互。

此脚本不启动编辑器；成功结束时通过 close_editor 正常关闭调用方提供的实例。
只在项目 cache 中写回执，临时 TS 不写入 assets；UI 验证不直接修改 Model。
"""

from __future__ import annotations

import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import sys
import time
import uuid

ROOT = Path(__file__).resolve().parents[3]
PROJECT = ROOT / "projects" / "showcase"
sys.path.insert(0, str(ROOT / "scripts"))
from editor_mcp_session import EditorMcpSession


IDENTITY_WRITE = """
import Game from "./game.js";
import { LauncherModel } from "./models/launcher_model.js";
import { showcase_cases } from "./models/showcase_cases.js";
type Probe = { model: LauncherModel; original: number };
type View = { model: LauncherModel; __mcpInteraction?: Probe };
export default function(args: { increment: number }, gameplay: unknown) {
    const view = gameplay as View;
    if (!(gameplay instanceof Game) || !(view.model instanceof LauncherModel) ||
        view.model.choices !== showcase_cases || view.__mcpInteraction)
        throw new Error("MCP did not receive the existing Gameplay and module instances");
    view.__mcpInteraction = { model: view.model, original: view.model.selection_count };
    view.model.selection_count += args.increment;
    return { original: view.__mcpInteraction.original, count: view.model.selection_count,
        sameModule: view.model.choices === showcase_cases };
}
"""

IDENTITY_READ = """
import Game from "./game.js";
import { LauncherModel } from "./models/launcher_model.js";
type View = { model: LauncherModel;
    __mcpInteraction?: { model: LauncherModel; original: number } };
export default function(args: { restore: boolean }, gameplay: unknown) {
    const view = gameplay as View;
    const probe = view.__mcpInteraction;
    if (!(gameplay instanceof Game) || !probe || probe.model !== view.model)
        throw new Error("Gameplay or Model instance changed between MCP calls");
    const result = { count: view.model.selection_count, original: probe.original,
        sameGameplay: true, sameModel: true };
    if (args.restore) {
        view.model.selection_count = probe.original;
        delete view.__mcpInteraction;
    }
    return result;
}
"""

STATE_READ = """
import type { Scene } from "engine";
import type { LauncherModel } from "./models/launcher_model.js";
import type { PersistentUiModel } from "./models/persistent_ui_model.js";
type View = { model: LauncherModel;
    active: { scene: Scene | null; model?: PersistentUiModel } | null };
export default function(_args: unknown, gameplay: unknown) {
    const view = gameplay as View;
    const persistent = view.model.selected?.id === "persistent-ui" ? view.active?.model : null;
    return { selected: view.model.selected?.id ?? null,
        count: view.model.selection_count, error: view.model.error,
        sceneValid: view.active?.scene?.IsValid() ?? false,
        persistent: persistent ? { events: persistent.events, completed: persistent.completed,
            loading: persistent.loading, identity: persistent.identity } : null };
}
"""

ERROR_SCRIPT = """
export default function(_args: unknown, _gameplay: unknown) {
    throw new Error("intentional-mcp-interaction-error");
}
"""


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def tool_payload(result):
    values = []
    for block in result.get("content", []):
        if block.get("type") == "text":
            try:
                value = json.loads(block.get("text", ""))
            except json.JSONDecodeError:
                continue
            if isinstance(value, dict):
                values.append(value)
    require(not result.get("isError") and len(values) == 1,
            f"MCP 未返回唯一成功的 JSON 对象：{result}")
    return values[0]


def get_status(session):
    result, _ = session.request_tool("get_status", {})
    status = tool_payload(result)
    session.validate_identity("get_status", status)
    return status


def wait_for(label, operation, predicate, timeout):
    deadline = time.monotonic() + timeout
    while True:
        value = operation()
        if predicate(value):
            return value
        if time.monotonic() >= deadline:
            raise RuntimeError(f"等待{label}超时，最后状态：{value}")
        time.sleep(0.1)


def lifecycle(session, command, expected):
    # 生命周期工具可能在 10 秒后返回 pending；只查询原 requestId，禁止重发。
    result, receipt = session.request_tool(command, {})
    require(receipt is not None, f"{command} 缺少生命周期回执：{result}")
    if receipt.get("activation") == "pending":
        request_id = receipt.get("requestId")
        require(isinstance(request_id, str) and request_id, "生命周期回执缺少 requestId")
        status = wait_for(command, lambda: get_status(session),
                          lambda item: item.get("lastRequestId") == request_id and
                          item.get("projectState") not in ("starting", "stopping"),
                          session.timeout)
    else:
        session.validate_identity(command, receipt)
        require(not result.get("isError") and receipt.get("activation") == "completed",
                f"{command} 未完成：{receipt}")
        # Completed editor receipts carry operation state under result; status owns the
        # current run/session fields. Read it instead of treating the receipt as status.
        require(receipt.get("success", receipt.get("lastRequestSucceeded")) is True,
                f"{command} 返回失败回执：{receipt}")
        status = get_status(session)
    require(status.get("lastRequestSucceeded") is True and
            status.get("projectState") == expected,
            f"{command} 没有进入 {expected}：{status}")
    return get_status(session)


def script_arguments(session, run_id, source, arguments=None, filename="mcp-interaction.ts"):
    return {"session": session.session_id, "runId": run_id,
            "sourcePath": f"scripts/{filename}", "source": source, "arguments": arguments}


def execute(session, run_id, source, arguments=None, filename="mcp-interaction.ts"):
    result = session.call("execute_project_script",
                          script_arguments(session, run_id, source, arguments, filename))
    require(result.get("runId") == run_id and "value" in result,
            f"脚本回执不属于当前 Run：{result}")
    return result["value"]


def expect_script_failure(session, run_id, source, expected):
    try:
        execute(session, run_id, source)
    except RuntimeError as error:
        require(expected in str(error), f"脚本因非预期原因失败：{error}")
        return
    raise RuntimeError("预期失败的项目脚本被接受")


def ui_element(session, element_id):
    documents = session.call("get_ui_documents")
    matches = []
    for document in documents["documents"]:
        if not document.get("documentId"):
            continue
        offset = 0
        while True:
            tree = session.call("get_ui_tree", {
                "nodeId": document["nodeId"], "documentId": document["documentId"],
                "limit": 512, "depth": 16, "offset": offset,
            })
            matches.extend(item for item in tree["elements"] if item.get("id") == element_id)
            if tree.get("nextOffset") is None:
                break
            offset = tree["nextOffset"]
    require(len(matches) == 1, f"控件 {element_id} 必须唯一，实际找到 {len(matches)} 个")
    return matches[0], documents["window"]


def finish_input(session, result):
    job = result.get("job")
    require(isinstance(job, str) and job, f"输入作业缺少身份：{result}")
    complete = wait_for("真实 UI 输入", lambda: session.call("get_ui_input", {"job": job}),
                        lambda item: item.get("status") != "running", session.timeout)
    require(complete.get("status") == "complete" and
            complete.get("completedEvents") == complete.get("eventCount"),
            f"真实 UI 输入未完成：{complete}")
    return complete


def click(session, element_id):
    element, window = ui_element(session, element_id)
    require(window.get("available"), "编辑器逻辑窗口尺寸不可用")
    # 真正滚动菜单，把目标中心移入窗口；不通过修改样式伪造可点击状态。
    for _ in range(40):
        x, y, width, height = element["bounds"]
        center_x, center_y = x + width / 2, y + height / 2
        if 0 <= center_x < window["logicalWidth"] and 0 <= center_y < window["logicalHeight"]:
            require(element.get("visible") and element.get("enabled"),
                    f"控件 {element_id} 不可见或未启用：{element}")
            return finish_input(session, session.call("click_ui", {"target": element["handle"]}))
        require(0 <= center_x < window["logicalWidth"], f"控件 {element_id} 横向超出窗口")
        direction = -5 if center_y >= window["logicalHeight"] else 5
        finish_input(session, session.call("ui_input", {
            "documentId": element["handle"]["documentId"],
            "events": [{"type": "wheel", "x": window["logicalWidth"] / 2,
                        "y": window["logicalHeight"] / 2, "deltaX": 0, "deltaY": direction}],
        }))
        element = session.call("get_ui_element", {"target": element["handle"]})
    raise RuntimeError(f"滚动后控件 {element_id} 仍未进入窗口")


def check(session, results):
    required = {"get_status", "start_project", "stop_project", "execute_project_script",
                "get_ui_documents", "get_ui_tree", "get_ui_element", "click_ui", "ui_input",
                "get_ui_input", "get_editor_receipt", "close_editor"}
    require(required <= session.tools, f"缺少 MCP 工具：{sorted(required - session.tools)}")
    status = get_status(session)
    require(status.get("editorAlive") is True, "需要调用方先启动 showcase 编辑器")
    if not status.get("running"):
        status = lifecycle(session, "start_project", "running")
    run_id = status.get("runId")
    require(isinstance(run_id, str) and run_id.isdecimal() and int(run_id) > 0,
            f"活动 Run 身份无效：{status}")
    results.update(session=session.session_id, firstRun=run_id)

    write = execute(session, run_id, IDENTITY_WRITE, {"increment": 7})
    read = execute(session, run_id, IDENTITY_READ, {"restore": True}, "mcp-interaction-read.ts")
    require(write["sameModule"] and read["sameGameplay"] and read["sameModel"] and
            read["count"] == write["original"] + 7 and read["original"] == write["original"],
            f"跨请求 Gameplay/Model 身份或读写值不一致：{write}, {read}")
    restored = execute(session, run_id, STATE_READ)
    require(restored["count"] == write["original"], "临时 Model 写入未恢复")
    results["sharedInstances"] = read
    print("PASS 同一活动 Gameplay、Model 和已加载模块；跨请求写读后恢复数据", flush=True)

    expect_script_failure(session, run_id, ERROR_SCRIPT, "intentional-mcp-interaction-error")
    status = get_status(session)
    require(status.get("running") and status.get("runId") == run_id,
            f"单次脚本失败停止或替换了 Run：{status}")
    require(execute(session, run_id, STATE_READ)["count"] == write["original"],
            "错误脚本后既有 Model 不可继续读取")
    print("PASS 错误脚本只失败该请求，活动 Run 可继续执行", flush=True)

    stopped = lifecycle(session, "stop_project", "idle")
    require(stopped.get("running") is False and stopped.get("runId") == "0",
            f"停止后 Run 身份未清空：{stopped}")
    expect_script_failure(session, run_id, STATE_READ, "active Run does not match")
    restarted = lifecycle(session, "start_project", "running")
    new_run = restarted.get("runId")
    require(isinstance(new_run, str) and new_run.isdecimal() and
            int(new_run) > 0 and new_run != run_id, f"重开没有新 Run：{restarted}")
    expect_script_failure(session, run_id, STATE_READ, "active Run does not match")
    run_id = new_run
    results["secondRun"] = run_id
    print("PASS 停止和重开均拒绝旧 runId，新 Run 正常运行", flush=True)

    read_state = lambda: execute(session, run_id, STATE_READ)
    initial = read_state()
    require(initial["selected"] is None and initial["error"] is None,
            f"新 Run 未进入菜单：{initial}")
    click(session, "scene-cube")
    pbr = wait_for("PBR 场景打开", read_state,
                   lambda item: item["selected"] == "pbr-material" and item["sceneValid"],
                   session.timeout)
    require(pbr["error"] is None and pbr["count"] == initial["count"] + 1,
            f"UI 点击没有准确更新 LauncherModel：{pbr}")
    click(session, "back-menu")
    wait_for("返回菜单", read_state, lambda item: item["selected"] is None, session.timeout)
    results["pbrClick"] = pbr
    print("PASS 真实 RmlUi 点击打开 PBR 范例并返回菜单", flush=True)

    click(session, "scene-persistent-ui")
    before = wait_for("常驻 UI 范例打开", read_state,
                      lambda item: item["selected"] == "persistent-ui" and
                      item["persistent"] is not None, session.timeout)
    note, _ = ui_element(session, "lifetime-note")
    original_value = note["value"]
    typed = " / MCP 中文输入 ✓"
    handle = note["handle"]
    finish_input(session, session.call("ui_input", {
        "documentId": handle["documentId"],
        "events": [{"type": "focus", "target": handle},
                   {"type": "key_down", "key": "End", "target": handle},
                   {"type": "key_up", "key": "End", "target": handle},
                   {"type": "text", "text": typed, "target": handle}],
    }))
    after = wait_for("真实 input 回调更新 Model", read_state,
                     lambda item: item["persistent"] is not None and
                     item["persistent"]["events"] > before["persistent"]["events"],
                     session.timeout)
    entered = session.call("get_ui_element", {"target": handle})
    require(entered["value"] == original_value + typed,
            f"Unicode 输入值不匹配：{entered['value']!r}")
    click(session, "load-a")
    loaded = wait_for("常驻 UI 跨场景提交", read_state,
                      lambda item: item["persistent"] is not None and
                      item["persistent"]["completed"] == after["persistent"]["completed"] + 1,
                      session.timeout)
    preserved = session.call("get_ui_element", {"target": handle})
    require(preserved["value"] == entered["value"] and
            loaded["persistent"]["identity"] == before["persistent"]["identity"],
            "场景提交没有保留同一常驻 UI 文字和身份")
    results["persistentInput"] = {"before": before["persistent"],
                                  "after": loaded["persistent"], "value": preserved["value"]}
    click(session, "back-menu")
    wait_for("常驻 UI 返回菜单", read_state,
             lambda item: item["selected"] is None, session.timeout)
    print("PASS 真实 Unicode 输入改变 Model.events，文字与原控件跨场景保留", flush=True)

    results["closeReceipt"] = session.call("close_editor", {"delayMs": 500})
    closed = wait_for("编辑器正常关闭", lambda: get_status(session),
                      lambda item: not item.get("editorAlive", True) and
                      item.get("lastReportedProjectState", item.get("projectState")) == "closed",
                      session.timeout)
    require(closed.get("running") is False and closed.get("runId") == "0",
            f"关闭后还残留活动 Run：{closed}")
    results["closedStatus"] = closed
    print("PASS close_editor 完成回执先返回，编辑器随后正常关闭", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--server", required=True, type=Path, help="已构建的 mcp_app")
    parser.add_argument("--runtime-dir", required=True, type=Path,
                        help="调用方已启动的 showcase 编辑器运行目录")
    parser.add_argument("--timeout", type=float, default=120)
    args = parser.parse_args()
    if args.timeout <= 0:
        parser.error("--timeout 必须为正数")
    label = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ") + "-" + uuid.uuid4().hex[:8]
    output = PROJECT / "cache" / "mcp-interaction" / label
    output.mkdir(parents=True, exist_ok=False)
    results = {"project": str(PROJECT), "runtimeDir": str(args.runtime_dir.resolve()),
               "passed": False}
    virtual_paths = [PROJECT / "assets/scripts" / name for name in
                     ("mcp-interaction.ts", "mcp-interaction-read.ts")]
    try:
        require(not any(path.exists() for path in virtual_paths), "临时 TS 路径与项目源文件冲突")
        with EditorMcpSession(args.server, PROJECT, args.runtime_dir, output / "receipts.jsonl",
                              timeout=args.timeout) as session:
            check(session, results)
        require(not any(path.exists() for path in virtual_paths), "临时脚本被意外写入 assets")
        results["passed"] = True
        print(f"PASS showcase MCP 真实交互；回执与结果：{output}")
        return 0
    except (OSError, ValueError, KeyError, RuntimeError) as error:
        results["error"] = str(error)
        print(f"showcase MCP 交互验证失败：{error}\n保留回执：{output}", file=sys.stderr)
        return 1
    finally:
        (output / "result.json").write_text(json.dumps(results, ensure_ascii=False, indent=2) + "\n",
                                           encoding="utf-8")


if __name__ == "__main__":
    raise SystemExit(main())
