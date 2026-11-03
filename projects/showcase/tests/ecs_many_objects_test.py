#!/usr/bin/env python3
"""通过真实编辑器 MCP 验证 ECS 大量实体的身份、层级和回收，不记录性能分数。

连接调用方已启动的 showcase 编辑器，正常 UI 操作进入测试、切换状态、重建并返回。
不关闭编辑器；不替换原生 ECS/渲染/脚本宿主；回执只写本地 cache。
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import sys
import uuid

from mcp_interaction_test import (PROJECT, EditorMcpSession, click, execute, get_status,
                                  lifecycle, require, wait_for)

READ = """
import EcsManyObjects from "./modules/ecs-many-objects.js";
export default function(_args: unknown, gameplay: unknown) {
    const menu = gameplay as { active: EcsManyObjects | null; model: { selected: {id: string} | null } };
    return { selected: menu.model.selected?.id ?? null,
        ecs: menu.active instanceof EcsManyObjects ? menu.active.Inspect() : null };
}
"""


def ready(session, run, count, previous_checks=-1):
    def read():
        value = execute(session, run, READ, None, "mcp-ecs-many-objects.ts")
        require(value["selected"] == "ecs-many-objects" and value["ecs"], "没有打开 ECS 范例")
        state = value["ecs"]
        require(state["phase"] != "failed", f"ECS 实体校验失败：{state}")
        return state
    state = wait_for("ECS 实体创建与逐项校验", read,
        lambda s: s["phase"] == "ready" and s["pending"] is None and
                  s["requested"] == count and s["checks"] > previous_checks, session.timeout)
    require(state["created"] == count and state["checked"] == count and state["sceneValid"],
            f"实体数量或场景身份不正确：{state}")
    scene = session.call("get_scene", {"offset": 0, "limit": 0})
    require(scene["totalNodes"] == count + 5, f"场景保留了多余实体或缺少实体：{scene}")
    return state


def check(session, results, count, output):
    status = get_status(session)
    require(status.get("editorAlive") is True, "需要先启动 showcase 编辑器并完成资源导入")
    if not status.get("running"):
        status = lifecycle(session, "start_project", "running")
    run = status["runId"]
    current = execute(session, run, READ, None, "mcp-ecs-many-objects.ts")
    require(current["selected"] is None, "请先返回 showcase 菜单再运行 ECS 功能验证")
    click(session, "scene-ecs")
    state = ready(session, run, 10000)
    if count != 10000:
        click(session, f"ecs-{count}")
        state = ready(session, run, count, state["checks"])
    results["initial"] = state
    results["capture"] = session.capture(str((output / "many-objects.png").relative_to(PROJECT / "cache")))
    require(state["visible"] == count and state["active"] == count, "初始实体状态不正确")
    for action, expected_visible, expected_active in [
        ("move", count, count),
        ("visibility", count // 2, count),
        ("activation", count // 2, count // 2),
        ("reparent", count // 2 + 1, count // 2 + 1),
        ("recreate", count // 2, count // 2),
        ("visibility", count, count // 2),
        ("activation", count, count),
    ]:
        click(session, f"ecs-{action}")
        state = ready(session, run, count, state["checks"])
        require(state["visible"] == expected_visible and state["active"] == expected_active,
                f"{action} 的继承状态不正确：{state}")
        results.setdefault("actions", []).append({"action": action, "state": state})
    require(state["stale_handles"] >= 3, "删除重建后没有校验旧句柄失效")
    click(session, "ecs-1000")
    state = ready(session, run, 1000, state["checks"])
    require(state["stale_handles"] >= 27, "整组回收后没有校验旧实体与组件句柄")
    results["recreated"] = state
    click(session, "back-menu")
    menu = wait_for("返回菜单", lambda: execute(session, run, READ, None, "mcp-ecs-many-objects.ts"),
                    lambda item: item["selected"] is None and item["ecs"] is None, session.timeout)
    results["menu"] = menu
    click(session, "scene-ecs")
    reopened = ready(session, run, 10000)
    require(reopened["generations"] == 1 and reopened["visible"] == 10000 and
            reopened["active"] == 10000, f"重新进入残留了上局状态：{reopened}")
    results["reopened"] = reopened
    click(session, "back-menu")
    wait_for("再次返回菜单", lambda: execute(session, run, READ, None, "mcp-ecs-many-objects.ts"),
             lambda item: item["selected"] is None and item["ecs"] is None, session.timeout)
    print(f"PASS {count} 个真实 ECS 对象：增组件、移动、层级隐藏/激活、换父级、删除重建、整组回收和返回菜单", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--server", required=True, type=Path)
    parser.add_argument("--runtime-dir", required=True, type=Path)
    parser.add_argument("--count", type=int, choices=(1000, 10000, 50000), default=10000)
    parser.add_argument("--timeout", type=float, default=600)
    args = parser.parse_args()
    if args.timeout <= 0:
        parser.error("--timeout 必须为正数")
    label = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ") + "-" + uuid.uuid4().hex[:8]
    output = PROJECT / "cache" / "ecs-many-objects" / label
    output.mkdir(parents=True, exist_ok=False)
    results = {"passed": False, "count": args.count}
    try:
        with EditorMcpSession(args.server, PROJECT, args.runtime_dir, output / "receipts.jsonl",
                              timeout=args.timeout) as session:
            check(session, results, args.count, output)
        results["passed"] = True
        print(f"回执与功能结果：{output}")
        return 0
    except (OSError, ValueError, KeyError, RuntimeError) as error:
        results["error"] = str(error)
        print(f"ECS 功能验证失败：{error}\n保留回执：{output}", file=sys.stderr)
        return 1
    finally:
        (output / "result.json").write_text(json.dumps(results, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    raise SystemExit(main())
