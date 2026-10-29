"""Exercise real MCP Android transport against a private fake adb device."""

import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

from script_tooling_test import call, contents, require


FAKE_ADB = r'''
import json
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys

root = Path(os.environ["ENGINE_FAKE_ANDROID_ROOT"])
config = json.loads((root / "config.json").read_text())
arguments = sys.argv[1:]
with (root / "calls.jsonl").open("a") as log:
    log.write(json.dumps(arguments) + "\n")
if arguments == ["get-state"]:
    print("offline" if config.get("offline") else "device")
    raise SystemExit(0)
if arguments[0] == "exec-out" and len(arguments) == 2:
    command = arguments[1]
elif arguments[:2] == ["shell", "-T"] and len(arguments) == 3:
    command = arguments[2]
else:
    raise SystemExit("unexpected adb invocation")
lexer = shlex.shlex(command, posix=True, punctuation_chars=True)
lexer.whitespace_split = True
tokens = list(lexer)
assert tokens[:4] == ["run-as", "org.engine.minimal", "sh", "-c"], tokens
script = tokens[4]
marker = re.search(r"__ENGINE_ADB_[0-9-]+__", command).group()
if config.get("release"):
    output, code = b"run-as: package not debuggable\n", 1
else:
    payload = sys.stdin.buffer.read()
    upload = "cat >" in script and ".staging-" in script
    if upload and config.get("corrupt_upload"):
        payload += b"corrupt"
    if upload and config.get("fail_upload"):
        payload = payload[:len(payload) // 2]
    result = subprocess.run(["/bin/sh", "-c", script], cwd=root / "app",
                            input=payload, capture_output=True)
    output, code = result.stdout + result.stderr, result.returncode
    if script.endswith("; pwd") and code == 0:
        output = b"/data/user/0/org.engine.minimal\n"
    if upload and config.get("fail_upload"):
        output += b"simulated interrupted upload"
        code = 1
sys.stdout.buffer.write(output)
drop_receipt = (config.get("drop_publish_receipt") and "mv -f" in script and
                " files/assets/available.json" in script)
drop_receipt = drop_receipt or (config.get("drop_control_receipt") and "mv -f" in script and
                              " files/script-control.json" in script)
if not drop_receipt:
    sys.stdout.buffer.write(f"\n{marker}:{code}\n".encode())
'''


def invoke(server, project, work, device, requests, package="org.engine.minimal"):
    messages = [
        {"jsonrpc": "2.0", "id": 0, "method": "initialize", "params": {
            "protocolVersion": "2025-06-18", "capabilities": {},
            "clientInfo": {"name": "android-mcp-test", "version": "1"}}},
        {"jsonrpc": "2.0", "method": "notifications/initialized"},
    ] + requests
    environment = dict(os.environ, ENGINE_FAKE_ANDROID_ROOT=str(device))
    # Python -s <serial> executes this private fixture; no platform shell launcher is needed.
    result = subprocess.run(
        [str(server), "--project", str(project), "--android-serial", "fake-adb.py",
         "--android-package", package, "--adb", sys.executable],
        cwd=work, env=environment,
        input="\n".join(json.dumps(message) for message in messages) + "\n",
        capture_output=True, text=True, encoding="utf-8", timeout=90, check=False)
    if package != "org.engine.minimal":
        return result
    require(result.returncode == 0, f"Android MCP failed: {result.stderr}")
    replies = [json.loads(line) for line in result.stdout.splitlines()]
    require(len(replies) == len(requests) + 1, "unexpected Android protocol response count")
    return replies[1:]


def entry(path):
    return {"path": path, "size": 1, "hash": "af63f54c86021707"}


def test(server, root, work):
    (work / "fake-adb.py").write_text(FAKE_ADB, encoding="utf-8")
    project = work / "project 中文 $literal"
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
    unusual = project / "assets" / "quote' $(touch injected) 中文.bin"
    unusual.write_bytes(bytes(range(256)) + b"\x00binary\r\n")

    device = work / "device"
    runtime = device / "app/files"
    (runtime / "assets").mkdir(parents=True)
    settings = device / "config.json"
    settings.write_text("{}")
    builtin = {"revision": "0123456789abcdef", "entry": "scripts/game.js", "files": [
        entry("scripts/game.js"), entry("scripts/builtin-only.js")],
        "deleted": ["scripts/previously-deleted.js"]}
    builtin_path = runtime / "assets/builtin.json"
    builtin_path.write_text(json.dumps(builtin))
    available_path = runtime / "assets/available.json"
    status = {"running": True, "hotReload": False, "activeRevision": builtin["revision"],
              "availableRevision": "", "lastError": ""}
    status_path = runtime / "script-status.json"
    status_path.write_text(json.dumps(status))

    replies = invoke(server, project, work, device, [call("sync_assets"), call("get_status", request_id=2)])
    require(not replies[0]["result"]["isError"], f"initial Android sync failed: {contents(replies[0])}")
    receipt = contents(replies[0])
    require(receipt["deviceSerial"] == "fake-adb.py" and
            receipt["packageName"] == "org.engine.minimal" and
            receipt["runtimeDir"] == "/data/user/0/org.engine.minimal/files", "wrong Android receipt target")
    published = json.loads(available_path.read_text())
    require({"scripts/builtin-only.js", "scripts/previously-deleted.js"}.issubset(published["deleted"]),
            "first sync did not retain built-in deletion baseline")
    uploaded = runtime / "assets/versions" / published["revision"] / unusual.name
    require(uploaded.read_bytes() == unusual.read_bytes(), "binary/Unicode/quoted filename changed")
    require(not (device / "app/injected").exists(), "remote resource path caused shell expansion")
    reported = contents(replies[1])
    require(reported["activeRevision"] == status["activeRevision"] and
            reported["availableRevision"] == published["revision"], "sync falsely activated gameplay")
    timestamp = available_path.stat().st_mtime_ns
    repeated = contents(invoke(server, project, work, device, [call("sync_assets")])[0])
    require(repeated.get("unchanged") and available_path.stat().st_mtime_ns == timestamp,
            "identical Android sync republished the manifest")

    # A newer installed APK can add built-in paths absent from the older available manifest.
    builtin["files"].append(entry("scripts/new-apk-only.js"))
    builtin_path.write_text(json.dumps(builtin))
    reply = invoke(server, project, work, device, [call("sync_assets")])[0]
    require(not reply["result"]["isError"], "sync after APK baseline upgrade failed")
    published = json.loads(available_path.read_text())
    require("scripts/new-apk-only.js" in published["deleted"], "APK upgrade deletion escaped baseline")
    original = available_path.read_bytes()
    source = project / "assets/scripts/game.ts"
    source.write_text(source.read_text() + "\n// changed Android version\n")

    for mode in ["fail_upload", "corrupt_upload"]:
        settings.write_text(json.dumps({mode: True}))
        reply = invoke(server, project, work, device, [call("sync_assets")])[0]
        require(reply["result"]["isError"], f"{mode} was not rejected")
        require(available_path.read_bytes() == original, f"{mode} changed active publication")
        require(not (runtime / "assets/.sync-lock").exists(), f"{mode} leaked owned lock")
        require(not list((runtime / "assets").glob(".staging-*")), f"{mode} leaked staged files")
    settings.write_text("{}")
    reply = invoke(server, project, work, device, [call("sync_assets")])[0]
    require(not reply["result"]["isError"], "retry after interrupted upload failed")
    require((runtime / "assets/versions" / published["revision"]).is_dir(), "old version was removed")

    replies = invoke(server, project, work, device, [
        call("set_hot_reload", {"enabled": True}),
        call("set_hot_reload", {"enabled": False}, request_id=2),
    ])
    require(all(not reply["result"]["isError"] for reply in replies), "Android control update failed")
    require(json.loads((runtime / "script-control.json").read_text()) == {"hotReload": False},
            "Android control replacement failed")
    require(json.loads(status_path.read_text()) == status, "MCP wrote fabricated engine status")

    control = runtime / "script-control.json"
    control.unlink()
    control.mkdir()
    reply = invoke(server, project, work, device, [call("set_hot_reload", {"enabled": True})])[0]
    require(reply["result"]["isError"] and not list(control.iterdir()),
            "control publication falsely succeeded into a directory")
    control.rmdir()
    settings.write_text('{"drop_control_receipt":true}')
    reply = invoke(server, project, work, device, [call("set_hot_reload", {"enabled": True})])[0]
    require(reply["result"]["isError"] and "not confirmed" in contents(reply)["error"],
            "missing control receipt did not report uncertainty")
    require(json.loads(control.read_text()) == {"hotReload": True},
            "fixture did not actually commit the unconfirmed control")
    settings.write_text("{}")

    (runtime / "assets/.sync-lock").mkdir()
    (runtime / "assets/.sync-lock/owner").write_text("another-host")
    before_lock = available_path.read_bytes()
    reply = invoke(server, project, work, device, [call("sync_assets")])[0]
    require(reply["result"]["isError"] and available_path.read_bytes() == before_lock,
            "another publisher's lock was bypassed")
    require((runtime / "assets/.sync-lock/owner").read_text() == "another-host",
            "another publisher's lock was removed")
    shutil.rmtree(runtime / "assets/.sync-lock")

    for mode in ["release", "offline"]:
        settings.write_text(json.dumps({mode: True}))
        reply = invoke(server, project, work, device, [call("get_status")])[0]
        require(reply["result"]["isError"], f"{mode} device reported success")
    settings.write_text("{}")
    invalid = invoke(server, project, work, device, [], package="org.engine.minimal;touch bad")
    require(invalid.returncode != 0 and not invalid.stdout, "unsafe package name was accepted")

    # Commit succeeded but the final network receipt was lost: report uncertainty, query device.
    source.write_text(source.read_text() + "\n// another version\n")
    settings.write_text('{"drop_publish_receipt":true}')
    before_unknown = available_path.read_bytes()
    reply = invoke(server, project, work, device, [call("sync_assets")])[0]
    require(reply["result"]["isError"] and "not confirmed" in contents(reply)["error"],
            "missing final receipt did not report unconfirmed publication")
    require(available_path.read_bytes() != before_unknown, "fixture did not actually commit publication")
    settings.write_text("{}")
    actual = contents(invoke(server, project, work, device, [call("get_status")])[0])
    require(actual["availableRevision"] == json.loads(available_path.read_text())["revision"],
            "status used a host mirror instead of the device after uncertain publication")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--mcp", type=Path, required=True)
    parser.add_argument("--root", type=Path, required=True)
    args = parser.parse_args()
    if not Path("/bin/sh").is_file():
        print("Fake Android shell fixture requires /bin/sh; use real-device tests on this host")
        raise SystemExit(77)
    with tempfile.TemporaryDirectory(prefix="engine-android-mcp-") as directory:
        test(args.mcp.resolve(), args.root.resolve(), Path(directory).resolve())
    print("Android MCP target, binary sync, deletion baseline, failure and control tests passed")
