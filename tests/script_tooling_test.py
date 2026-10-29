"""Use the real TypeScript compiler and MCP executable without network access."""

import argparse
import json
import re
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def metadata_guid(source):
    text = Path(str(source) + ".meta").read_text(encoding="utf-8")
    match = re.search(
        r'^guid\s*=\s*"([0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12})"\s*(?:#.*)?$',
        text,
        re.MULTILINE,
    )
    require(match is not None, "source meta must contain a canonical TOML GUID")
    return match.group(1)


def check_compiler_inputs(root, project, temporary):
    builder = root / "scripts/build_scripts.mjs"
    config_path = project / "settings/tsconfig.json"
    config = json.loads(config_path.read_text())
    inherited = temporary / "shared-tsconfig.json"
    inherited.write_text('{"compilerOptions":{"noUnusedLocals":false}}', encoding="utf-8")
    declarations = temporary / "external-types.d.ts"
    declarations.write_text("declare const fingerprintInput: string;\n", encoding="utf-8")
    config["extends"] = str(inherited)
    config["files"].append(str(declarations))
    config_path.write_text(json.dumps(config), encoding="utf-8")

    def inputs():
        result = subprocess.run(
            ["node", str(builder), "--project", str(project), "--inputs"],
            capture_output=True,
            text=True,
            timeout=30,
            check=False,
        )
        require(result.returncode == 0, f"script input discovery failed: {result.stderr}")
        return json.loads(result.stdout)

    first = inputs()
    identities = {entry["path"] for entry in first["inputs"]}
    require(
        str(inherited).replace("\\", "/") in identities
        and str(declarations).replace("\\", "/") in identities,
        "external inherited config/declarations missing from compiler fingerprint",
    )
    require(first == inputs(), "unchanged TypeScript input fingerprint is not stable")
    inherited.write_text('{"compilerOptions":{"noUnusedLocals":true}}', encoding="utf-8")
    second = inputs()
    require(second != first, "inherited configuration mutation did not change fingerprint")
    declarations.write_text("declare const fingerprintInput: number;\n", encoding="utf-8")
    require(inputs() != second, "external declaration mutation did not change fingerprint")
    lock = project / "cache/.script-build-lock"
    lock.mkdir()
    (lock / "old-owner").write_text("abandoned fixture")
    output = temporary / "explicit-ts-staging"
    result = subprocess.run(
        ["node", str(builder), "--project", str(project), "--output", str(output)],
        capture_output=True,
        text=True,
        timeout=30,
        check=False,
    )
    require(
        result.returncode == 0, f"explicit staging must ignore old directory lock: {result.stderr}"
    )
    require(
        (lock / "old-owner").read_text() == "abandoned fixture",
        "compiler must not remove another operation's lock",
    )
    shutil.rmtree(lock)
    result = subprocess.run(
        ["node", str(builder), "--project", str(project)],
        capture_output=True,
        text=True,
        timeout=30,
        check=False,
    )
    require(
        result.returncode != 0 and not (project / "cache/assets").exists(),
        "implicit legacy cache/assets output must be rejected",
    )


def resource_payload(entity):
    require(entity[:4] == b"ERS1" and len(entity) >= 8, "invalid resource entity framing")
    header_size = int.from_bytes(entity[4:8], "little") + 8
    require(8 < header_size <= len(entity), "invalid bounded resource header")
    return entity[header_size:]


def verify_complete_asset_snapshot(project, runtime, manifest):
    expected = {"catalog.bin"}
    for source in (project / "assets").rglob("*"):
        if not source.is_file() or source.name.endswith(".meta") or source.name == ".gitkeep":
            continue
        expected.add("gen/" + metadata_guid(source))
    actual = {item["path"] for item in manifest["files"]}
    require(actual == expected, "MCP snapshot must include every catalog asset exactly once")
    version = runtime / "assets/versions" / manifest["revision"]
    for relative in expected:
        require(
            (version / relative).read_bytes() == (project / "cache" / relative).read_bytes(),
            f"MCP changed generated/raw asset bytes: {relative}",
        )
    require(
        not any(
            path.endswith((".meta", ".sqlite")) or path.startswith("settings/") for path in actual
        ),
        "editor settings/meta/SQLite entered runtime assets",
    )
    for relative in (
        "scenes/main.scene",
        "meshes/cube.mesh",
        "materials/cube.material",
        "shaders/pbr.shader",
    ):
        require(
            "gen/" + metadata_guid(project / "assets" / relative) in actual,
            f"non-script typed asset missing: {relative}",
        )


def invoke(mcp, project, runtime, requests):
    messages = [
        {
            "jsonrpc": "2.0",
            "id": 0,
            "method": "initialize",
            "params": {
                "protocolVersion": "2025-06-18",
                "capabilities": {},
                "clientInfo": {"name": "engine-test", "version": "1"},
            },
        },
        {"jsonrpc": "2.0", "method": "notifications/initialized"},
    ] + requests
    result = subprocess.run(
        [str(mcp), "--project", str(project), "--runtime-dir", str(runtime)],
        input="\n".join(
            json.dumps(message) if isinstance(message, dict) else message for message in messages
        )
        + "\n",
        capture_output=True,
        text=True,
        timeout=90,
        check=False,
    )
    require(result.returncode == 0, f"MCP failed: {result.stderr}")
    replies = [json.loads(line) for line in result.stdout.splitlines()]
    require(
        replies[0]["result"]["serverInfo"]["name"] == "engine-editor-tools",
        "initialize did not return server identity",
    )
    require(
        all(reply["jsonrpc"] == "2.0" for reply in replies), "stdout contains non-protocol output"
    )
    return replies[1:]


def call(name, arguments=None, request_id=1):
    return {
        "jsonrpc": "2.0",
        "id": request_id,
        "method": "tools/call",
        "params": {"name": name, "arguments": arguments or {}},
    }


def contents(reply):
    return json.loads(reply["result"]["content"][0]["text"])


def test(mcp, root, temporary, without_modeling=False, without_character_controller=False):
    # 空格及 shell 元字符必须作为普通 argv 路径传递，不能经 shell 执行。
    project = temporary / "project with spaces $literal 中文"
    runtime = temporary / "runtime"
    shutil.copytree(
        root / "projects/minimal", project, ignore=shutil.ignore_patterns("node_modules", "cache")
    )
    (project / "cache").mkdir()
    compiler = root / "projects/minimal/settings/node_modules/typescript"
    require(compiler.is_dir(), "Run npm ci --prefix projects/minimal/settings first")
    shutil.copytree(compiler, project / "settings/node_modules/typescript")
    config_path = project / "settings/tsconfig.json"
    config = json.loads(config_path.read_text())
    config["files"] = [str(root / "include/engine.d.ts")]
    config_path.write_text(json.dumps(config), encoding="utf-8")
    check_compiler_inputs(root, project, temporary)
    unicode_asset = project / "assets/资料/文本.txt"
    unicode_asset.parent.mkdir()
    unicode_asset.write_text("UTF-8 asset content", encoding="utf-8")
    image_asset = project / "assets/资料/raw.png"
    shutil.copy2(root / "tests/fixtures/asset_tools/blocks.png", image_asset)
    binary_asset = project / "assets/资料/raw.bin"
    binary_asset.write_bytes(bytes(range(256)) + b"\0raw binary")

    replies = invoke(
        mcp,
        project,
        runtime,
        [
            {"jsonrpc": "2.0", "id": 1, "method": "tools/list"},
            call("get_status", request_id=2),
            call("set_hot_reload", {"enabled": False}, request_id=3),
            call("sync_assets", request_id=4),
            call("set_hot_reload", {"enabled": True}, request_id=5),
            call("set_hot_reload", {"enabled": "false"}, request_id=6),
            "{not json}",
            {"jsonrpc": "2.0", "id": 8, "method": "ping"},
        ],
    )
    require(len(replies) == 8, "notifications must not produce responses")
    expected_tools = {
        "sync_assets",
        "build_resources",
        "set_hot_reload",
        "get_status",
        "start_project",
        "stop_project",
        "get_scene",
        "set_transform",
        "set_light",
        "set_camera",
        "set_collider",
        "get_collider",
        "set_rigid_body",
        "get_rigid_body",
        "add_impulse",
        "set_audio_source",
        "get_audio_source",
        "set_audio_listener",
        "get_audio_listener",
        "play_audio",
        "pause_audio",
        "stop_audio",
        "get_audio_state",
        "set_animator",
        "get_animator",
        "play_animation",
        "pause_animation",
        "stop_animation",
        "seek_animation",
        "get_animation_state",
        "instantiate_model",
        "set_character_motor",
        "get_character_motor",
        "set_material_texture",
        "set_material_parameter",
        "execute_batch",
        "undo",
        "redo",
        "get_editor_receipt",
        "create_node",
        "set_active_camera",
        "set_material_type",
        "save_scene",
        "get_render_settings",
        "set_render_settings",
        "capture_frame",
        "get_frame_capture",
        "modeling_help",
        "modeling_create",
        "modeling_get",
        "modeling_select",
        "modeling_edit",
        "modeling_delete",
        "modeling_duplicate",
        "modeling_combine",
        "modeling_separate",
        "modeling_save",
        "modeling_load",
    }
    if without_modeling:
        expected_tools = {name for name in expected_tools if not name.startswith("modeling_")}
    controller_tools = {"set_character_controller", "get_character_controller"}
    if not without_character_controller:
        expected_tools.update(controller_tools)
    actual_tools = {tool["name"] for tool in replies[0]["result"]["tools"]}
    require(
        actual_tools == expected_tools,
        f"wrong tool list: missing={sorted(expected_tools - actual_tools)}, "
        f"unexpected={sorted(actual_tools - expected_tools)}",
    )
    if without_character_controller:
        for name in sorted(controller_tools):
            unavailable = invoke(mcp, project, runtime, [call(name)])[0]
            require(
                unavailable.get("error", {}).get("message") == "Unknown tool",
                f"disabled character controller plugin still exposes {name}",
            )
    if without_modeling:
        unavailable = invoke(mcp, project, runtime, [call("modeling_help")])[0]
        require(
            unavailable.get("error", {}).get("message") == "Unknown tool",
            "disabled modeling plugin still exposes an executable tool",
        )
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
    manifest = json.loads(original_manifest)
    verify_complete_asset_snapshot(project, runtime, manifest)
    texture_payload = resource_payload(
        (project / "cache/gen" / metadata_guid(image_asset)).read_bytes()
    )
    require(
        texture_payload[4:8] == b"ETEX", "image was not converted into a BasisU texture resource"
    )
    for raw in (binary_asset,):
        require(
            resource_payload((project / "cache/gen" / metadata_guid(raw)).read_bytes())
            == raw.read_bytes(),
            "raw binary bytes changed during import",
        )
    asset_guid = metadata_guid(unicode_asset)
    require(
        "gen/" + asset_guid in {item["path"] for item in json.loads(original_manifest)["files"]},
        "Unicode asset GUID did not survive compile and synchronization",
    )
    require(
        "catalog.bin" in {item["path"] for item in json.loads(original_manifest)["files"]},
        "GUID resource catalog was not synchronized",
    )
    require(not (project / "cache/assets").exists(), "formal compiler produced legacy asset tree")
    offline = contents(invoke(mcp, project, runtime, [call("get_status")])[0])
    require(
        offline["state"] == "offline"
        and offline["availableRevision"] == json.loads(original_manifest)["revision"],
        "published version must be queryable without a running engine",
    )
    script_guid = metadata_guid(project / "assets/scripts/game.ts")
    script_output = project / "cache/gen" / script_guid
    original_output = script_output.read_bytes()
    require(b"sourceMappingURL=data:" in original_output, "GUID JS must carry an inline source map")
    source = project / "assets/scripts/game.ts"
    original_source = source.read_text()

    source.write_text(original_source + "\nconst broken: number = 'not a number';\n")
    reply = invoke(mcp, project, runtime, [call("sync_assets")])[0]
    require(reply["result"]["isError"], "TypeScript error must fail sync")
    require(manifest_path.read_bytes() == original_manifest, "failed compile published old assets")
    require(
        script_output.read_bytes() == original_output,
        "failed compile damaged prior compiler output",
    )
    source.write_text(original_source)

    helper = project / "assets/scripts/helper.ts"
    helper.write_text("export const value = 1;\n")
    reply = invoke(mcp, project, runtime, [call("sync_assets")])[0]
    require(not reply["result"]["isError"], "adding a module failed")
    manifest = json.loads(manifest_path.read_text())
    helper_guid = metadata_guid(helper)
    require(
        "gen/" + helper_guid in {item["path"] for item in manifest["files"]}, "added module missing"
    )
    helper.unlink()
    reply = invoke(mcp, project, runtime, [call("sync_assets")])[0]
    require(not reply["result"]["isError"], "deleting a module failed")
    manifest = json.loads(manifest_path.read_text())
    require("gen/" + helper_guid in manifest["deleted"], "deleted module was not recorded")

    (project / "settings/test-package.toml").write_text(
        'target_platform=""\noutput="cache/build/test-package"\n'
        '[[items]]\nname="game"\nstorage="package"\n'
        'roots=["project/scripts/game.ts", "project/scenes/main.scene"]\n',
        encoding="utf-8",
    )
    built = invoke(
        mcp, project, runtime, [call("build_resources", {"manifest": "settings/test-package.toml"})]
    )[0]
    require(not built["result"]["isError"], f"MCP resource build failed: {contents(built)}")
    first_build = contents(built)
    client = subprocess.run(
        [
            sys.executable,
            str(root / "scripts/mcp_client.py"),
            "--server",
            str(mcp),
            "--project",
            str(project),
            "--runtime-dir",
            str(runtime),
            "build",
            "--manifest",
            "settings/test-package.toml",
            "--item",
            "game",
        ],
        capture_output=True,
        text=True,
        timeout=120,
        check=False,
    )
    require(client.returncode == 0, f"MCP client build failed: {client.stderr} {client.stdout}")
    rebuilt = json.loads(client.stdout)
    require(
        rebuilt["revision"] == first_build["revision"] and rebuilt["reusedPackages"] > 0,
        "unchanged MCP package build did not reuse its output",
    )
    client_prefix = [
        sys.executable,
        str(root / "scripts/mcp_client.py"),
        "--server",
        str(mcp),
        "--project",
        str(project),
        "--runtime-dir",
        str(runtime),
        "call",
    ]
    argument_file = temporary / "tool arguments.json"
    argument_file.write_text('{"enabled": false}', encoding="utf-8")
    for arguments, field, expected in (
        (["get_status", "--arguments", "{}"], "state", "offline"),
        (
            ["set_hot_reload", "--arguments-file", str(argument_file)],
            "requestedHotReload",
            False,
        ),
    ):
        called = subprocess.run(
            client_prefix + arguments, capture_output=True, text=True, timeout=30, check=False
        )
        require(
            called.returncode == 0 and json.loads(called.stdout).get(field) == expected,
            f"generic MCP client call failed: {called.stderr} {called.stdout}",
        )
    for arguments in (
        ["get_status", "--arguments", "[]"],
        ["get_status", "--arguments", "{broken"],
        ["get_status", "--arguments", "{}", "--arguments-file", str(argument_file)],
    ):
        invalid = subprocess.run(
            client_prefix + arguments, capture_output=True, text=True, timeout=10, check=False
        )
        require(
            invalid.returncode != 0 and not invalid.stdout,
            f"generic client accepted invalid or ambiguous JSON arguments: {invalid.stdout}",
        )
    packed = invoke(
        mcp, project, runtime, [call("sync_assets", {"source": "cache/build/test-package"})]
    )[0]
    require(
        not packed["result"]["isError"],
        f"prebuilt package synchronization failed: {contents(packed)}",
    )
    packed_manifest = json.loads(manifest_path.read_text())
    require(
        any(item["path"].startswith("packs/") for item in packed_manifest["files"]),
        "merged package was not included in deployment",
    )
    invalid = invoke(mcp, project, runtime, [call("build_resources", {"manifest": "../outside"})])[
        0
    ]
    require(invalid["result"]["isError"], "build manifest traversal was accepted")
    manifest = packed_manifest

    # No running engine is fabricated: get_status returns only the explicit runtime report.
    expected = {
        "running": True,
        "hotReload": False,
        "activeRevision": "older",
        "availableRevision": manifest["revision"],
        "lastError": "",
    }
    (runtime / "script-status.json").write_text(json.dumps(expected))
    reply = contents(invoke(mcp, project, runtime, [call("get_status")])[0])
    require(
        reply["activeRevision"] == "older" and reply["hotReload"] is False,
        "status incorrectly claims synchronized code is active",
    )
    (runtime / "script-status.json").write_text('{"running":"invalid"}')
    reply = invoke(mcp, project, runtime, [call("get_status")])[0]
    require(reply["result"]["isError"], "invalid status fields must not abort the MCP process")
    replies = invoke(
        mcp,
        project,
        runtime,
        [
            {"jsonrpc": "2.0", "id": 1, "method": "tools/call", "params": []},
            {
                "jsonrpc": "2.0",
                "id": 2,
                "method": "tools/call",
                "params": {"name": "sync_assets", "arguments": False},
            },
            {"jsonrpc": "2.0", "id": 3, "method": "ping"},
        ],
    )
    require(
        replies[0]["error"]["code"] == -32602
        and replies[1]["error"]["code"] == -32602
        and replies[2]["result"] == {},
        "malformed tool params must return errors without stopping the protocol",
    )
    missing_target = subprocess.run(
        [str(mcp), "--project", str(project)], capture_output=True, text=True, timeout=10
    )
    require(
        missing_target.returncode != 0 and not missing_target.stdout,
        "MCP accepted an implicit runtime target",
    )


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--mcp", required=True, type=Path)
    parser.add_argument("--root", required=True, type=Path)
    parser.add_argument(
        "--without-modeling",
        action="store_true",
        help="Validate the editor MCP host with the modeling plugin disabled",
    )
    parser.add_argument(
        "--without-character-controller",
        action="store_true",
        help="Validate that the optional character controller MCP adapter is absent",
    )
    arguments = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="engine-script-tooling-") as directory:
        test(
            arguments.mcp.resolve(),
            arguments.root.resolve(),
            Path(directory).resolve(),
            without_modeling=arguments.without_modeling,
            without_character_controller=arguments.without_character_controller,
        )
    print("TypeScript compile, atomic sync, MCP protocol and control tests passed")
