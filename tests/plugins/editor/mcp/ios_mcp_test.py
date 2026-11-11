"""Exercise iOS container transport, full resource publication and failure boundaries."""

import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[3]))
from support.sample_project import prepare_minimal_project
from plugins.editor.mcp.android_mcp_test import UNICODE
from plugins.editor.mcp.script_tooling_test import (
    call, contents, require, verify_complete_asset_snapshot,
)

UDID = "11111111-2222-4333-8444-555555555555"
BUNDLE = "org.engine.sync-fixture"
APP = "sync-fixture"

FAKE_XCRUN = r'''#!/usr/bin/env python3
import json, os, pathlib, sys
root = pathlib.Path(os.environ["ENGINE_FAKE_IOS_ROOT"])
settings = json.loads((root / "config.json").read_text())
args = sys.argv[1:]
with (root / "calls.jsonl").open("a") as log:
    log.write(json.dumps(args) + "\n")
if args == ["simctl", "list", "devices", "--json"]:
    print(json.dumps({"devices": {"runtime-fixture": [{
        "udid": "00000000-0000-4000-8000-000000000000" if settings.get("wrong_device") else "11111111-2222-4333-8444-555555555555",
        "state": "Shutdown" if settings.get("offline") else "Booted",
        "isAvailable": True
    }]}}))
elif args == ["simctl", "get_app_container", "11111111-2222-4333-8444-555555555555", "org.engine.sync-fixture", "data"]:
    if settings.get("missing_app"):
        raise SystemExit(1)
    print(root / "container")
else:
    raise SystemExit("Unexpected simulator command")
'''


def invoke(server, project, work, requests):
    messages = [
        {"jsonrpc": "2.0", "id": 0, "method": "initialize", "params": {
            "protocolVersion": "2025-06-18", "capabilities": {},
            "clientInfo": {"name": "ios-container-test", "version": "1"},
        }},
        {"jsonrpc": "2.0", "method": "notifications/initialized"},
    ] + requests
    command = [str(server), "--project", str(project), "--ios-simulator", UDID,
               "--ios-bundle-id", BUNDLE, "--ios-application-name", APP,
               "--xcrun", str(work / "fake-xcrun")]
    result = subprocess.run(command, input="\n".join(map(json.dumps, messages)) + "\n",
                            capture_output=True, text=True, encoding="utf-8", timeout=90,
                            env=dict(os.environ, ENGINE_FAKE_IOS_ROOT=str(work)))
    require(result.returncode == 0, f"iOS MCP process failed: {result.stderr}")
    replies = [json.loads(line) for line in result.stdout.splitlines()]
    require(len(replies) == len(requests) + 1, "Unexpected iOS MCP response count")
    return replies[1:]


def test(server, root, work):
    import fcntl
    tool = work / "fake-xcrun"
    tool.write_text(FAKE_XCRUN, encoding="utf-8")
    tool.chmod(0o755)
    (work / "config.json").write_text("{}")
    container = work / "container"
    container.mkdir()
    runtime = container / "Library/Application Support" / APP
    project = prepare_minimal_project(root, work / UNICODE["shell_project"])
    (project / "assets" / UNICODE["shell_filename"]).write_bytes(bytes(range(256)))
    (project / "assets/multichunk.bin").write_bytes(bytes(range(256)) * 12288)
    shutil.copy2(root / "tests/fixtures/asset_tools/blocks.png", project / "assets/raw-image.png")
    result = invoke(server, project, work, [call("sync_assets")])[0]
    require(not result["result"]["isError"], f"Initial iOS sync failed: {contents(result)}")
    receipt = contents(result)
    require(receipt["deviceUdid"] == UDID and receipt["bundleIdentifier"] == BUNDLE and
            receipt["applicationName"] == APP and receipt["runtimeDir"] == str(runtime),
            "The receipt did not identify the selected iOS application")
    available = runtime / "assets/available.json"
    original = available.read_bytes()
    first = json.loads(original)
    verify_complete_asset_snapshot(project, runtime, first)
    timestamp = available.stat().st_mtime_ns
    repeated = contents(invoke(server, project, work, [call("sync_assets")])[0])
    require(repeated["unchanged"] and available.stat().st_mtime_ns == timestamp,
            "An unchanged iOS version was republished")
    # A corrupt immutable carrier must never be reused or silently repaired in place.
    entry = next(item for item in first["files"] if item["size"] > 0)
    entity = runtime / "assets/versions" / first["revision"] / entry["path"]
    good = entity.read_bytes()
    entity.write_bytes(bytes([good[0] ^ 1]) + good[1:])
    failed = invoke(server, project, work, [call("sync_assets")])[0]
    require(failed["result"]["isError"] and available.read_bytes() == original,
            "Corrupt iOS carrier reuse changed the published manifest")
    entity.write_bytes(good)
    source = project / "assets/scripts/game.ts"
    source.write_text(source.read_text() + "\n// iOS sync revision two\n", encoding="utf-8")
    # The same kernel lock is shared with AssetSyncSystem; lock files remain persistent.
    with (runtime / "assets/.sync-lock/owner.lock").open("r+b") as lock:
        fcntl.flock(lock.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
        failed = invoke(server, project, work, [call("sync_assets")])[0]
        require(failed["result"]["isError"] and available.read_bytes() == original,
                "A second iOS publisher bypassed the active lock")
    outside = work / "outside"
    outside.mkdir()
    versions = runtime / "assets/versions"
    saved = runtime / "assets/held-versions"
    versions.rename(saved)
    versions.symlink_to(outside, target_is_directory=True)
    try:
        failed = invoke(server, project, work, [call("sync_assets")])[0]
        require(failed["result"]["isError"] and available.read_bytes() == original and
                not list(outside.iterdir()), "iOS synchronization followed a symlink")
    finally:
        versions.unlink()
        saved.rename(versions)
    updated = invoke(server, project, work, [call("sync_assets")])[0]
    require(not updated["result"]["isError"], f"Updated iOS sync failed: {contents(updated)}")
    second = json.loads(available.read_text())
    require(second["revision"] != first["revision"] and
            (versions / first["revision"]).is_dir(), "iOS sync replaced an old immutable version")
    verify_complete_asset_snapshot(project, runtime, second)
    status = {"running": True, "hotReload": False, "activeRevision": "previous-engine-version", "lastError": ""}
    state_file = runtime / "script-status.json"
    state_file.write_text(json.dumps(status))
    replies = invoke(server, project, work, [call("set_hot_reload", {"enabled": True}),
                     call("set_hot_reload", {"enabled": False}, request_id=2),
                     call("get_status", request_id=3)])
    require(all(not item["result"]["isError"] for item in replies), "iOS control or status failed")
    require(json.loads((runtime / "script-control.json").read_text()) == {"hotReload": False} and
            json.loads(state_file.read_text()) == status, "iOS MCP fabricated runtime state")
    observed = contents(replies[-1])
    require(observed["activeRevision"] == status["activeRevision"] and
            observed["availableRevision"] == second["revision"] and
            observed["state"] == "lastReportedRunning", "iOS status reported false activation")
    control = runtime / "script-control.json"
    control.unlink()
    control.mkdir()
    failed = invoke(server, project, work, [call("set_hot_reload", {"enabled": True})])[0]
    require(failed["result"]["isError"] and control.is_dir() and not list(control.iterdir()),
            "iOS atomic control publication replaced a directory")
    control.rmdir()
    require(not list(runtime.glob(".script-control-*")) and
            not list((runtime / "assets").glob(".staging-*")) and
            not list((runtime / "assets").glob(".available-*")), "Owned iOS staging files leaked")
    for mode in ["offline", "wrong_device", "missing_app"]:
        (work / "config.json").write_text(json.dumps({mode: True}))
        failed = invoke(server, project, work, [call("get_status")])[0]
        require(failed["result"]["isError"], f"The selected iOS target failure was ignored: {mode}")
    (work / "config.json").write_text("{}")
    state_file.write_text("{")
    failed = invoke(server, project, work, [call("get_status")])[0]
    require(failed["result"]["isError"], "Malformed device status was accepted")
    state_file.write_text(json.dumps(status))
    # A prebuilt package for another backend cannot supersede valid iOS resources.
    build = invoke(server, project, work, [call("build_resources", {"targetPlatform": "macos"})])[0]
    require(not build["result"]["isError"], "Cannot prepare the wrong-platform regression fixture")
    previous = available.read_bytes()
    failed = invoke(server, project, work, [call("sync_assets", {"source": "cache/build/macos"})])[0]
    require(failed["result"]["isError"] and available.read_bytes() == previous,
            "A non-iOS catalog was published to the device")
    for arguments in [
        ["--ios-simulator", "booted", "--ios-bundle-id", BUNDLE, "--ios-application-name", APP],
        ["--ios-device", UDID, "--runtime-dir", str(work / "unexpected")],
        ["--ios-device", UDID, "--ios-bundle-id", BUNDLE, "--ios-application-name", "../outside"],
    ]:
        result = subprocess.run([str(server), "--project", str(project), *arguments],
                                input="", capture_output=True, text=True, timeout=20)
        require(result.returncode != 0, "An ambiguous or unsafe iOS target was accepted")
    print("iOS MCP full catalog sync, immutable readback, locks, atomic controls and target checks passed")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mcp", required=True, type=Path)
    parser.add_argument("--root", required=True, type=Path)
    args = parser.parse_args()
    if sys.platform != "darwin":
        print("iOS simulator container regression requires a macOS host")
        raise SystemExit(77)
    with tempfile.TemporaryDirectory(prefix="engine-ios-mcp-") as temporary:
        test(args.mcp.resolve(), args.root.resolve(), Path(temporary).resolve())
