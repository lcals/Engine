"""Use the real TypeScript compiler and MCP executable without network access."""

import argparse
import json
from pathlib import Path
import shutil
import subprocess
import tempfile


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def invoke(mcp, project, runtime, requests):
    messages = [
        {"jsonrpc": "2.0", "id": 0, "method": "initialize", "params": {
            "protocolVersion": "2025-06-18", "capabilities": {},
            "clientInfo": {"name": "engine-test", "version": "1"}}},
        {"jsonrpc": "2.0", "method": "notifications/initialized"},
    ] + requests
    result = subprocess.run(
        [str(mcp), "--project", str(project), "--runtime-dir", str(runtime)],
        input="\n".join(json.dumps(message) if isinstance(message, dict) else message
                        for message in messages) + "\n",
        capture_output=True, text=True, timeout=90, check=False)
    require(result.returncode == 0, f"MCP failed: {result.stderr}")
    replies = [json.loads(line) for line in result.stdout.splitlines()]
    require(replies[0]["result"]["serverInfo"]["name"] == "engine-editor-tools",
            "initialize did not return server identity")
    require(all(reply["jsonrpc"] == "2.0" for reply in replies),
            "stdout contains non-protocol output")
    return replies[1:]


def call(name, arguments=None, request_id=1):
    return {"jsonrpc": "2.0", "id": request_id, "method": "tools/call",
            "params": {"name": name, "arguments": arguments or {}}}


def contents(reply):
    return json.loads(reply["result"]["content"][0]["text"])


def test(mcp, root, temporary):
    # 空格及 shell 元字符必须作为普通 argv 路径传递，不能经 shell 执行。
    project = temporary / "project with spaces $literal 中文"
    runtime = temporary / "runtime"
    shutil.copytree(root / "projects/minimal", project,
                    ignore=shutil.ignore_patterns("node_modules", "cache"))
    (project / "cache").mkdir()
    compiler = root / "projects/minimal/settings/node_modules/typescript"
    require(compiler.is_dir(), "Run npm ci --prefix projects/minimal/settings first")
    shutil.copytree(compiler, project / "settings/node_modules/typescript")
    config_path = project / "settings/tsconfig.json"
    config = json.loads(config_path.read_text())
    config["files"] = [str(root / "include/engine.d.ts")]
    config_path.write_text(json.dumps(config), encoding="utf-8")
    unicode_asset = project / "assets/资料/文本.txt"
    unicode_asset.parent.mkdir()
    unicode_asset.write_text("UTF-8 asset content", encoding="utf-8")

    replies = invoke(mcp, project, runtime, [
        {"jsonrpc": "2.0", "id": 1, "method": "tools/list"},
        call("get_status", request_id=2),
        call("set_hot_reload", {"enabled": False}, request_id=3),
        call("sync_assets", request_id=4),
        call("set_hot_reload", {"enabled": True}, request_id=5),
        call("set_hot_reload", {"enabled": "false"}, request_id=6),
        "{not json}",
        {"jsonrpc": "2.0", "id": 8, "method": "ping"},
    ])
    require(len(replies) == 8, "notifications must not produce responses")
    require({tool["name"] for tool in replies[0]["result"]["tools"]} ==
            {"sync_assets", "set_hot_reload", "get_status"}, "wrong tool list")
    require(contents(replies[1])["state"] == "offline", "missing runtime must be offline")
    require(contents(replies[2])["requestedHotReload"] is False, "disable control failed")
    require(not replies[3]["result"]["isError"], f"sync failed: {contents(replies[3])}")
    require(contents(replies[4])["requestedHotReload"] is True, "enable control failed")
    require(replies[5]["error"]["code"] == -32602, "invalid boolean accepted")
    require(replies[6]["error"]["code"] == -32700, "parse error not reported")
    require(replies[7]["result"] == {}, "ping failed after malformed request")
    control = json.loads((runtime / "script-control.json").read_text())
    require(control == {"hotReload": True}, "control file was not atomically replaced")
    manifest_path = runtime / "assets/available.json"
    original_manifest = manifest_path.read_bytes()
    require("资料/文本.txt" in {item["path"] for item in json.loads(original_manifest)["files"]},
            "Unicode asset path did not survive compile and synchronization")
    offline = contents(invoke(mcp, project, runtime, [call("get_status")])[0])
    require(offline["state"] == "offline" and offline["availableRevision"] ==
            json.loads(original_manifest)["revision"],
            "published version must be queryable without a running engine")
    original_output = (project / "cache/assets/scripts/game.js").read_bytes()
    source = project / "assets/scripts/game.ts"
    original_source = source.read_text()

    source.write_text(original_source + "\nconst broken: number = 'not a number';\n")
    reply = invoke(mcp, project, runtime, [call("sync_assets")])[0]
    require(reply["result"]["isError"], "TypeScript error must fail sync")
    require(manifest_path.read_bytes() == original_manifest, "failed compile published old assets")
    require((project / "cache/assets/scripts/game.js").read_bytes() == original_output,
            "failed compile damaged prior compiler output")
    source.write_text(original_source)

    helper = project / "assets/scripts/helper.ts"
    helper.write_text("export const value = 1;\n")
    reply = invoke(mcp, project, runtime, [call("sync_assets")])[0]
    require(not reply["result"]["isError"], "adding a module failed")
    manifest = json.loads(manifest_path.read_text())
    require("scripts/helper.js" in {item["path"] for item in manifest["files"]},
            "added module missing")
    helper.unlink()
    reply = invoke(mcp, project, runtime, [call("sync_assets")])[0]
    require(not reply["result"]["isError"], "deleting a module failed")
    manifest = json.loads(manifest_path.read_text())
    require("scripts/helper.js" in manifest["deleted"], "deleted module was not recorded")

    # No running engine is fabricated: get_status returns only the explicit runtime report.
    expected = {"running": True, "hotReload": False, "activeRevision": "older",
                "availableRevision": manifest["revision"], "lastError": ""}
    (runtime / "script-status.json").write_text(json.dumps(expected))
    reply = contents(invoke(mcp, project, runtime, [call("get_status")])[0])
    require(reply["activeRevision"] == "older" and reply["hotReload"] is False,
            "status incorrectly claims synchronized code is active")
    (runtime / "script-status.json").write_text('{"running":"invalid"}')
    reply = invoke(mcp, project, runtime, [call("get_status")])[0]
    require(reply["result"]["isError"], "invalid status fields must not abort the MCP process")
    replies = invoke(mcp, project, runtime, [
        {"jsonrpc": "2.0", "id": 1, "method": "tools/call", "params": []},
        {"jsonrpc": "2.0", "id": 2, "method": "tools/call", "params": {
            "name": "sync_assets", "arguments": False}},
        {"jsonrpc": "2.0", "id": 3, "method": "ping"},
    ])
    require(replies[0]["error"]["code"] == -32602 and
            replies[1]["error"]["code"] == -32602 and replies[2]["result"] == {},
            "malformed tool params must return errors without stopping the protocol")
    missing_target = subprocess.run([str(mcp), "--project", str(project)],
                                    capture_output=True, text=True, timeout=10)
    require(missing_target.returncode != 0 and not missing_target.stdout,
            "MCP accepted an implicit runtime target")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--mcp", required=True, type=Path)
    parser.add_argument("--root", required=True, type=Path)
    arguments = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="engine-script-tooling-") as directory:
        test(arguments.mcp.resolve(), arguments.root.resolve(), Path(directory).resolve())
    print("TypeScript compile, atomic sync, MCP protocol and control tests passed")
