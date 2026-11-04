#!/usr/bin/env python3
"""通过真实 MCP、RmlUi 与 TS 验证独立渲染用例，不采集性能分数。

连接调用方已启动并完成资源导入的 showcase 编辑器；不启动或关闭编辑器。
固定 384 个对象，以同相机提交计数验证各路径，截图与回执只写项目 cache。
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
import RenderFeatures from "./modules/render-features.js";
export default function(_args: unknown, gameplay: unknown) {
    const menu = gameplay as { active: RenderFeatures | null;
        model: { selected: {id: string} | null; error: string | null } };
    return { selected: menu.model.selected?.id ?? null, error: menu.model.error,
        rendering: menu.active instanceof RenderFeatures ? menu.active.Inspect() : null };
}
"""


def expect_failure(session, name, arguments):
    try:
        session.call(name, arguments)
    except RuntimeError:
        return
    raise RuntimeError(f"无效请求 {name} 被接受：{arguments}")


def read_lab(session, run):
    value = execute(session, run, READ, None, "mcp-render-features.ts")
    require(value["error"] is None, f"渲染用例启动失败：{value}")
    return value


def ready(session, run):
    value = wait_for("独立渲染用例", lambda: read_lab(session, run),
        lambda item: item["selected"] == "render-features" and item["rendering"] is not None and
                     item["rendering"]["objects"] == 384,
        session.timeout)
    return value["rendering"]


def states(session):
    result = session.call("get_render_features")["renderer"]
    require(result["scope"] == "world_summary", "渲染汇总必须说明实际世界提交范围")
    return result["features"]


def wait_feature(session, feature, enabled, after=0):
    def read():
        result = session.call("get_render_features")
        return {"frame": result["renderer"]["frameNumber"],
                "state": result["renderer"]["features"][feature]}
    return wait_for(f"{feature} 实际渲染状态", read,
        lambda item: item["frame"] > after and item["state"]["requested"] == enabled and
                     item["state"]["effective"] == enabled,
        session.timeout)["state"]


def main_stats(session, after=0):
    result = wait_for("新的主视图提交", lambda: session.call("get_render_submission_stats"),
        lambda item: item["frameNumber"] > after and
                     any(row["viewId"] == 113 and row["candidateMeshes"] >= 384
                         for row in item["views"]), session.timeout)
    require(result["source"] == "scene_mesh_submission" and
            result["gpuCompletion"] == "not_measured", "提交计数不得伪装为 GPU 计时或完成")
    return next(row for row in result["views"] if row["viewId"] == 113)


def selected_ground(session):
    summary = session.call("get_scene", {"limit": 0})
    count = summary["totalNodes"]
    # Ground is appended after the 384 spheres. Read a bounded page, not a >1 MiB scene receipt.
    result = session.call("get_scene", {"offset": max(0, count - 16), "limit": 16})
    nodes = [node for node in result["scene"]["nodes"] if node["name"] == "Hex surface"]
    require(len(nodes) == 1, "没有找到唯一的 Hex 材质测试地面")
    return {"sceneId": result["scene"]["sceneId"], "nodeId": nodes[0]["nodeId"], "slot": 0}


def wait_scoped(session, scope, feature, enabled, identity=None):
    return wait_for(f"{scope}/{feature} 真实值", lambda: session.call("get_render_features", identity or {}),
        lambda item: item[scope]["features"][feature]["requested"] == enabled and
                     (scope == "material" or item[scope]["features"][feature]["effective"] == enabled),
        session.timeout)[scope]


def check(session, results, output):
    status = get_status(session)
    require(status.get("editorAlive") is True, "需要先启动 showcase 编辑器并完成资源导入")
    if not status.get("running"):
        status = lifecycle(session, "start_project", "running")
    run = status["runId"]
    require(read_lab(session, run)["selected"] is None, "请先返回 showcase 菜单再验证渲染用例")
    previous = {name: state["requested"] for name, state in states(session).items()}
    click(session, "scene-render")
    results["initial"] = ready(session, run)
    results["baseline"] = main_stats(session)
    require(results["baseline"]["culledMeshes"] > 0 and
            results["baseline"]["instanceBatches"] > 0,
            "固定场景没有实际使用视锥剔除与静态实例化")
    results["captures"] = [session.capture(str((output / "initial.png").relative_to(PROJECT / "cache")))]
    lod = session.call("get_mesh_lod_import", {"path": "meshes/render-lod-sphere.mesh"})
    require(lod["settings"]["enabled"] and len(lod["meshes"]) == 1, "LOD 没有使用正式导入资源")
    levels = lod["meshes"][0]["levels"]
    require(len(levels) >= 2 and all(levels[index]["triangleCount"] > levels[index + 1]["triangleCount"]
                                     for index in range(len(levels) - 1)), f"导入 LOD 未真实减面：{lod}")
    results["importedLods"] = lod

    for feature in ("frustum_culling", "static_instancing", "static_lod"):
        before = main_stats(session)
        click(session, f"render-{feature}")
        wait_feature(session, feature, False, before["frameNumber"])
        disabled = main_stats(session, before["frameNumber"])
        require(ready(session, run)["objects"] == 384, f"{feature} 改变了场景对象数")
        if feature == "frustum_culling":
            require(disabled["culledMeshes"] == 0 and disabled["visibleMeshes"] >= before["visibleMeshes"],
                    "关闭剔除后仍剔除了对象")
        elif feature == "static_instancing":
            require(disabled["instanceBatches"] == 0 and disabled["drawCalls"] > before["drawCalls"],
                    "关闭实例化后没有恢复真实逐 draw 路径")
        else:
            require(disabled["triangles"] > before["triangles"], "关闭 LOD 后没有恢复 LOD0 拓扑")
        click(session, f"render-{feature}")
        wait_feature(session, feature, True, disabled["frameNumber"])
        results.setdefault("pathComparisons", {})[feature] = {"enabled": before, "disabled": disabled}

    # Change camera through the normal View/Controller route, then compare LOD on/off at that same camera.
    click(session, "render-camera")
    wait_for("远观察镜头", lambda: ready(session, run), lambda item: item["distant"], session.timeout)
    far_on = main_stats(session)
    click(session, "render-static_lod")
    wait_feature(session, "static_lod", False, far_on["frameNumber"])
    far_off = main_stats(session, far_on["frameNumber"])
    require(far_off["triangles"] > far_on["triangles"], "远视角 LOD 开关没有改变实际三角形数量")
    click(session, "render-static_lod")
    wait_feature(session, "static_lod", True, far_off["frameNumber"])
    click(session, "render-camera")
    wait_for("恢复原镜头", lambda: ready(session, run), lambda item: not item["distant"], session.timeout)
    results["farCameraLod"] = {"enabled": far_on, "disabled": far_off}

    for feature in ("wireframe", "mesh_bounds", "camera_frusta", "stats"):
        original = states(session)[feature]
        require(original["supported"], f"当前设备不支持 {feature}，不能宣称完成此验收")
        session.call("set_render_feature", {"feature": feature, "enabled": not original["requested"]})
        changed = wait_feature(session, feature, not original["requested"])
        if feature == "wireframe":
            results["captures"].append(session.capture(str((output / "wireframe.png").relative_to(PROJECT / "cache"))))
        session.call("set_render_feature", {"feature": feature, "enabled": original["requested"]})
        wait_feature(session, feature, original["requested"])
        results.setdefault("mcpDebug", {})[feature] = changed

    session.call("set_render_feature", {"feature": "debug_draw", "enabled": False})
    wait_feature(session, "debug_draw", False)
    line = {"start": [0, 1, 0], "end": [0, 5, 0], "color": 4294902015, "depthTest": True}
    expect_failure(session, "draw_debug_lines", {"lines": [line]})
    session.call("set_render_feature", {"feature": "debug_draw", "enabled": True})
    require(states(session)["debug_draw"]["requested"], "没有接受调试绘制请求")
    results["debugLines"] = session.call("draw_debug_lines", {"lines": [line], "lifetimeFrames": 600})
    require(results["debugLines"]["acceptedLines"] == 1, "没有真实接受调试线")
    wait_feature(session, "debug_draw", True)
    results["captures"].append(session.capture(str((output / "debug-lines.png").relative_to(PROJECT / "cache"))))

    click(session, "render-atmosphere")
    wait_for("UI 天空关闭", lambda: ready(session, run), lambda item: not item["atmosphere"], session.timeout)
    wait_scoped(session, "scene", "atmosphere", False)
    results["captures"].append(session.capture(str((output / "sky-off.png").relative_to(PROJECT / "cache"))))
    click(session, "render-atmosphere")
    environment = wait_scoped(session, "scene", "atmosphere", True)
    env_identity = {key: environment[key] for key in ("sceneId", "nodeId")}
    for enabled in (False, True):
        session.call("set_render_feature", {"feature": "atmosphere", "enabled": enabled, **env_identity})
        wait_scoped(session, "scene", "atmosphere", enabled)

    ground = selected_ground(session)
    click(session, "render-hex")
    wait_for("UI Hex 开启", lambda: ready(session, run), lambda item: item["hex"], session.timeout)
    wait_scoped(session, "material", "hex_tiling", True, ground)
    results["captures"].append(session.capture(str((output / "hex-on.png").relative_to(PROJECT / "cache"))))
    click(session, "render-hex")
    wait_scoped(session, "material", "hex_tiling", False, ground)
    for enabled in (True, False):
        session.call("set_render_feature", {"feature": "hex_tiling", "enabled": enabled, **ground})
        wait_scoped(session, "material", "hex_tiling", enabled, ground)

    # A per-object edit cannot turn off other objects; native readback remains separate from scene summaries.
    original_ground = session.call("get_mesh_render_settings", {key: ground[key] for key in ("sceneId", "nodeId")})
    for feature, field in (("static_instancing", "staticInstancingEnabled"), ("static_lod", "staticLodEnabled")):
        identity = {key: ground[key] for key in ("sceneId", "nodeId")}
        session.call("set_render_feature", {"feature": feature, "enabled": False, **identity})
        require(session.call("get_mesh_render_settings", identity)[field] is False,
                f"{feature} 没有写入选中 MeshRenderer")
        require(states(session)[feature]["requested"], "单对象属性错误地修改了其他对象的全局策略")
        session.call("undo", {"sceneId": ground["sceneId"]})
        require(session.call("get_mesh_render_settings", identity)[field] == original_ground[field], "撤销没有恢复组件属性")
        session.call("redo", {"sceneId": ground["sceneId"]})
        require(session.call("get_mesh_render_settings", identity)[field] is False, "重做没有恢复该对象的属性修改")
        session.call("undo", {"sceneId": ground["sceneId"]})
    camera = session.call("get_render_features")["camera"]
    camera_identity = {key: camera[key] for key in ("sceneId", "nodeId")}
    session.call("set_render_feature", {"feature": "frustum_culling", "enabled": False, **camera_identity})
    require(session.call("get_render_features", camera_identity)["camera"]["frustumCullingEnabled"] is False,
            "统一剔除入口没有编辑 Camera 属性")
    session.call("undo", {"sceneId": camera["sceneId"]})
    require(session.call("get_render_features", camera_identity)["camera"]["frustumCullingEnabled"] is True,
            "Camera 属性撤销失败")
    sun = session.call("get_render_settings")["atmosphereSunNodeId"]
    require(isinstance(sun, str) and sun != "0", "环境查询没有返回实际太阳 Node 身份")
    session.call("set_render_settings", {**env_identity, "atmosphereSunNodeId": sun})
    require(session.call("get_render_settings")["atmosphereSunNodeId"] == sun, "太阳 Node/local ID 边界转换错误")
    expect_failure(session, "set_render_settings", {**env_identity, "atmosphereSunNodeId": ground["nodeId"]})
    for feature in ("frustum_culling", "static_instancing", "static_lod"):
        expect_failure(session, "set_render_feature", {"feature": feature, "enabled": False})

    session.call("set_render_feature", {"feature": "mesh_bounds", "enabled": True})
    require(wait_feature(session, "mesh_bounds", True)["requested"], "统一 renderer 开关未应用")
    session.call("set_render_feature", {"feature": "mesh_bounds", "enabled": False})
    wait_feature(session, "mesh_bounds", False)
    expect_failure(session, "set_render_feature", {"feature": "frame_view_uniforms", "enabled": False})
    expect_failure(session, "set_render_feature", {"feature": "hex_tiling", "enabled": True, **ground, "slot": 65535})
    expect_failure(session, "set_render_feature", {"feature": "atmosphere", "enabled": False,
                   "sceneId": ground["sceneId"], "nodeId": ground["nodeId"]})
    require(wait_scoped(session, "scene", "atmosphere", True), "无效目标修改了实际环境")

    for feature in ("wireframe", "debug_draw", "mesh_bounds", "camera_frusta", "stats"):
        session.call("set_render_feature", {"feature": feature, "enabled": previous[feature]})
    click(session, "back-menu")
    results["returned"] = wait_for("返回菜单清理", lambda: read_lab(session, run),
        lambda item: item["selected"] is None and item["rendering"] is None, session.timeout)
    require({name: state["requested"] for name, state in states(session).items()} == previous,
            "返回菜单没有恢复进入前的渲染请求")
    remaining = session.call("get_scene", {"limit": 0})
    require(remaining["scene"] is None and remaining["totalNodes"] == 0, "返回菜单残留了渲染场景对象")
    # Reopen checks that scene material/environment changes and temporary debug work were not saved.
    click(session, "scene-render")
    reopened = ready(session, run)
    require(reopened["objects"] == 384 and reopened["atmosphere"] and not reopened["hex"] and
            not reopened["distant"], "重新进入继承了上一局临时编辑")
    results["reopened"] = reopened
    click(session, "back-menu")
    wait_for("再次返回菜单", lambda: read_lab(session, run),
        lambda item: item["selected"] is None and item["rendering"] is None, session.timeout)
    print("PASS 384 个固定对象：真实 UI/MCP 功能开关、导入 LOD、逐视图提交、调试、天空/Hex 与返回清理", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--server", required=True, type=Path)
    parser.add_argument("--runtime-dir", required=True, type=Path)
    parser.add_argument("--timeout", type=float, default=180)
    args = parser.parse_args()
    if args.timeout <= 0:
        parser.error("--timeout 必须为正数")
    label = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ") + "-" + uuid.uuid4().hex[:8]
    output = PROJECT / "cache" / "render-features" / label
    output.mkdir(parents=True, exist_ok=False)
    results = {"passed": False, "objects": 384, "timingsCollected": False}
    try:
        with EditorMcpSession(args.server, PROJECT, args.runtime_dir, output / "receipts.jsonl",
                              timeout=args.timeout) as session:
            check(session, results, output)
        results["passed"] = True
        print(f"回执、提交计数和截图：{output}")
        return 0
    except (OSError, ValueError, KeyError, RuntimeError) as error:
        results["error"] = str(error)
        print(f"渲染功能验证失败：{error}\n保留回执：{output}", file=sys.stderr)
        return 1
    finally:
        (output / "result.json").write_text(json.dumps(results, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    raise SystemExit(main())
