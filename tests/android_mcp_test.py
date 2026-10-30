"""Exercise real MCP Android transport against a private fake adb device."""

import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

from sample_project import prepare_minimal_project
from script_tooling_test import (
    call,
    contents,
    metadata_guid,
    require,
    resource_payload,
    verify_complete_asset_snapshot,
)

FAKE_ADB = r"""
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
    chunk = re.search(r"dd if=.* bs=1048576 skip=([0-9]+) count=1", script)
    if chunk and int(chunk.group(1)) == 1:
        if config.get("fail_readback_chunk"):
            output, code = b"simulated interrupted chunk", 1
        elif config.get("short_readback_chunk"):
            output = output[:-1]
        elif config.get("corrupt_readback_chunk") and output:
            output = bytes([output[0] ^ 1]) + output[1:]
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
"""


def invoke(server, project, work, device, requests, package="org.engine.minimal"):
    messages = [
        {
            "jsonrpc": "2.0",
            "id": 0,
            "method": "initialize",
            "params": {
                "protocolVersion": "2025-06-18",
                "capabilities": {},
                "clientInfo": {"name": "android-mcp-test", "version": "1"},
            },
        },
        {"jsonrpc": "2.0", "method": "notifications/initialized"},
    ] + requests
    environment = dict(os.environ, ENGINE_FAKE_ANDROID_ROOT=str(device))
    # Python -s <serial> executes this private fixture; no platform shell launcher is needed.
    result = subprocess.run(
        [
            str(server),
            "--project",
            str(project),
            "--android-serial",
            "fake-adb.py",
            "--android-package",
            package,
            "--adb",
            sys.executable,
        ],
        cwd=work,
        env=environment,
        input="\n".join(json.dumps(message) for message in messages) + "\n",
        capture_output=True,
        text=True,
        encoding="utf-8",
        timeout=90,
        check=False,
    )
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
    prepare_minimal_project(root, project)
    unusual = project / "assets" / "quote' $(touch injected) 中文.bin"
    unusual.write_bytes(bytes(range(256)) + b"\x00binary\r\n")
    image_asset = project / "assets/raw-image.png"
    shutil.copy2(root / "tests/fixtures/asset_tools/blocks.png", image_asset)

    device = work / "device"
    runtime = device / "app/files"
    (runtime / "assets").mkdir(parents=True)
    settings = device / "config.json"
    settings.write_text("{}")
    builtin = {
        "revision": "0123456789abcdef",
        "entry": "scripts/game.js",
        "files": [entry("scripts/game.js"), entry("scripts/builtin-only.js")],
        "deleted": ["scripts/previously-deleted.js"],
    }
    builtin_path = runtime / "assets/builtin.json"
    builtin_path.write_text(json.dumps(builtin))
    available_path = runtime / "assets/available.json"
    status = {
        "running": True,
        "hotReload": False,
        "activeRevision": builtin["revision"],
        "availableRevision": "",
        "lastError": "",
    }
    status_path = runtime / "script-status.json"
    status_path.write_text(json.dumps(status))

    replies = invoke(
        server, project, work, device, [call("sync_assets"), call("get_status", request_id=2)]
    )
    require(
        not replies[0]["result"]["isError"], f"initial Android sync failed: {contents(replies[0])}"
    )
    receipt = contents(replies[0])
    require(
        receipt["deviceSerial"] == "fake-adb.py"
        and receipt["packageName"] == "org.engine.minimal"
        and receipt["runtimeDir"] == "/data/user/0/org.engine.minimal/files",
        "wrong Android receipt target",
    )
    published = json.loads(available_path.read_text())
    verify_complete_asset_snapshot(project, runtime, published)
    image_guid = metadata_guid(image_asset)
    texture_entity = (project / "cache/gen" / image_guid).read_bytes()
    uploaded_texture = (
        runtime / "assets/versions" / published["revision"] / "gen" / image_guid
    ).read_bytes()
    require(
        resource_payload(texture_entity)[4:8] == b"ETEX",
        "PNG was not imported as a BasisU texture resource before Android synchronization",
    )
    require(
        uploaded_texture == texture_entity,
        "Android synchronization changed the complete generated texture entity",
    )
    require(
        {"scripts/builtin-only.js", "scripts/previously-deleted.js"}.issubset(published["deleted"]),
        "first sync did not retain built-in deletion baseline",
    )
    unusual_guid = metadata_guid(unusual)
    uploaded = runtime / "assets/versions" / published["revision"] / "gen" / unusual_guid
    require(
        "catalog.bin" in {item["path"] for item in published["files"]},
        "Android GUID catalog missing from snapshot",
    )
    require(
        resource_payload(uploaded.read_bytes()) == unusual.read_bytes(),
        "binary/Unicode/quoted filename changed",
    )
    require(not (device / "app/injected").exists(), "remote resource path caused shell expansion")
    reported = contents(replies[1])
    require(
        reported["activeRevision"] == status["activeRevision"]
        and reported["availableRevision"] == published["revision"],
        "sync falsely activated gameplay",
    )
    timestamp = available_path.stat().st_mtime_ns
    repeated = contents(invoke(server, project, work, device, [call("sync_assets")])[0])
    require(
        repeated.get("unchanged") and available_path.stat().st_mtime_ns == timestamp,
        "identical Android sync republished the manifest",
    )

    # A newer installed APK can add built-in paths absent from the older available manifest.
    builtin["files"].append(entry("scripts/new-apk-only.js"))
    builtin_path.write_text(json.dumps(builtin))
    reply = invoke(server, project, work, device, [call("sync_assets")])[0]
    require(not reply["result"]["isError"], "sync after APK baseline upgrade failed")
    published = json.loads(available_path.read_text())
    require(
        "scripts/new-apk-only.js" in published["deleted"], "APK upgrade deletion escaped baseline"
    )
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
    require(
        (runtime / "assets/versions" / published["revision"]).is_dir(), "old version was removed"
    )

    replies = invoke(
        server,
        project,
        work,
        device,
        [
            call("set_hot_reload", {"enabled": True}),
            call("set_hot_reload", {"enabled": False}, request_id=2),
        ],
    )
    require(
        all(not reply["result"]["isError"] for reply in replies), "Android control update failed"
    )
    require(
        json.loads((runtime / "script-control.json").read_text()) == {"hotReload": False},
        "Android control replacement failed",
    )
    require(json.loads(status_path.read_text()) == status, "MCP wrote fabricated engine status")

    control = runtime / "script-control.json"
    control.unlink()
    control.mkdir()
    reply = invoke(server, project, work, device, [call("set_hot_reload", {"enabled": True})])[0]
    require(
        reply["result"]["isError"] and not list(control.iterdir()),
        "control publication falsely succeeded into a directory",
    )
    control.rmdir()
    settings.write_text('{"drop_control_receipt":true}')
    reply = invoke(server, project, work, device, [call("set_hot_reload", {"enabled": True})])[0]
    require(
        reply["result"]["isError"] and "not confirmed" in contents(reply)["error"],
        "missing control receipt did not report uncertainty",
    )
    require(
        json.loads(control.read_text()) == {"hotReload": True},
        "fixture did not actually commit the unconfirmed control",
    )
    settings.write_text("{}")

    (runtime / "assets/.sync-lock").mkdir()
    (runtime / "assets/.sync-lock/owner").write_text("another-host")
    before_lock = available_path.read_bytes()
    reply = invoke(server, project, work, device, [call("sync_assets")])[0]
    require(
        reply["result"]["isError"] and available_path.read_bytes() == before_lock,
        "another publisher's lock was bypassed",
    )
    require(
        (runtime / "assets/.sync-lock/owner").read_text() == "another-host",
        "another publisher's lock was removed",
    )
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
    require(
        reply["result"]["isError"] and "not confirmed" in contents(reply)["error"],
        "missing final receipt did not report unconfirmed publication",
    )
    require(
        available_path.read_bytes() != before_unknown, "fixture did not actually commit publication"
    )
    settings.write_text("{}")
    actual = contents(invoke(server, project, work, device, [call("get_status")])[0])
    require(
        actual["availableRevision"] == json.loads(available_path.read_text())["revision"],
        "status used a host mirror instead of the device after uncertain publication",
    )

    # The device verifies merged carriers in bounded readback chunks, including later-chunk failure.
    large = project / "assets/large"
    large.mkdir()
    for name in ("first.bin", "second.bin"):
        with (large / name).open("wb") as resource:
            resource.truncate(33 * 1024 * 1024)
    (project / "settings/android-package.toml").write_text(
        'target_platform="android"\noutput="cache/build/android-package"\n'
        '[[items]]\nname="game"\nstorage="package"\n'
        'roots=["project/scripts/game.ts","project/scenes/main.scene"]\n'
        'keep=["project/large/first.bin","project/large/second.bin"]\n',
        encoding="utf-8",
    )
    built = invoke(
        server,
        project,
        work,
        device,
        [call("build_resources", {"manifest": "settings/android-package.toml"})],
    )[0]
    require(not built["result"]["isError"], f"Android package build failed: {contents(built)}")
    packages = list((project / "cache/build/android-package/packs").iterdir())
    require(
        len(packages) == 1 and packages[0].stat().st_size > 65 * 1024 * 1024,
        "expected a valid carrier above the previous adb response limit",
    )
    previous = available_path.read_bytes()
    request = call("sync_assets", {"source": "cache/build/android-package"})
    for mode in ("fail_readback_chunk", "short_readback_chunk", "corrupt_readback_chunk"):
        settings.write_text(json.dumps({mode: True}))
        rejected = invoke(server, project, work, device, [request])[0]
        require(rejected["result"]["isError"], f"{mode} was not rejected")
        require(available_path.read_bytes() == previous, f"{mode} changed the published version")
        require(not (runtime / "assets/.sync-lock").exists(), f"{mode} leaked owned lock")
        require(not list((runtime / "assets").glob(".staging-*")), f"{mode} leaked staged files")
    settings.write_text("{}")
    (device / "calls.jsonl").write_text("")
    transferred = invoke(server, project, work, device, [request])[0]
    require(
        not transferred["result"]["isError"],
        f"large Android package transfer failed: {contents(transferred)}",
    )
    manifest = json.loads(available_path.read_text())
    package_entries = [item for item in manifest["files"] if item["path"].startswith("packs/")]
    require(
        len(package_entries) == 1 and package_entries[0]["size"] == packages[0].stat().st_size,
        "large carrier length changed during device transfer",
    )
    calls = [json.loads(line) for line in (device / "calls.jsonl").read_text().splitlines()]
    commands = [arguments[-1] for arguments in calls if arguments[0] in ("exec-out", "shell")]
    chunks = [command for command in commands if "dd if=" in command and "/packs/" in command]
    require(
        len(chunks) >= 66 and any("skip=65 count=1" in command for command in chunks),
        "large carrier was not verified through bounded readback chunks",
    )
    require(
        not any("; cat " in command and "/packs/" in command for command in commands),
        "device verification still read an entire carrier into one response",
    )


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
