#!/usr/bin/env python3
"""Send one editor MCP operation using only the Python standard library."""

import argparse
import json
from pathlib import Path
import subprocess
import sys
import threading


def receive(process, expected_id):
    while True:
        line = process.stdout.readline()
        if not line:
            raise RuntimeError("MCP server closed stdout before replying (or timed out)")
        reply = json.loads(line)
        if not isinstance(reply, dict) or reply.get("jsonrpc") != "2.0":
            raise RuntimeError("MCP server returned an invalid JSON-RPC message")
        if "id" not in reply and isinstance(reply.get("method"), str):
            continue
        if reply.get("id") != expected_id:
            raise RuntimeError("MCP server returned an unexpected request id")
        if "error" in reply:
            raise RuntimeError(json.dumps(reply["error"], ensure_ascii=False))
        result = reply.get("result")
        if not isinstance(result, dict):
            raise RuntimeError("MCP server result must be an object")
        return result


def send(process, message):
    process.stdin.write(json.dumps(message, ensure_ascii=False) + "\n")
    process.stdin.flush()


def main():
    parser = argparse.ArgumentParser(description="Call the configured editor runtime's MCP tools")
    parser.add_argument("--server", required=True, type=Path, help="Path to mcp_app")
    parser.add_argument("--project", required=True, type=Path)
    targets = parser.add_mutually_exclusive_group(required=True)
    targets.add_argument("--runtime-dir", type=Path)
    targets.add_argument("--android-serial")
    parser.add_argument("--android-package")
    parser.add_argument("--adb", type=Path)
    parser.add_argument("--timeout", type=float, default=120, help="Whole operation timeout in seconds")
    commands = parser.add_subparsers(dest="command", required=True)
    commands.add_parser("start", help="Start the project in the editor window")
    commands.add_parser("stop", help="Stop the project and restore the blank window")
    commands.add_parser("status", help="Show published and last reported runtime state")
    commands.add_parser("sync", help="Compile TypeScript and synchronize a complete version")
    hot_reload = commands.add_parser("hot-reload", help="Set the engine hot reload switch")
    hot_reload.add_argument("enabled", choices=["on", "off"])
    args = parser.parse_args()
    if args.timeout <= 0:
        parser.error("--timeout must be positive")
    if args.android_serial and not args.android_package:
        parser.error("--android-serial requires --android-package")
    if not args.android_serial and (args.android_package or args.adb):
        parser.error("--android-package and --adb require --android-serial")
    tool = {"start": "start_project", "stop": "stop_project", "status": "get_status", "sync": "sync_assets", "hot-reload": "set_hot_reload"}[
        args.command]
    arguments = {"enabled": args.enabled == "on"} if args.command == "hot-reload" else {}
    process = None
    timer = None
    try:
        command = [str(args.server.resolve()), "--project", str(args.project.resolve())]
        if args.android_serial:
            command += ["--android-serial", args.android_serial,
                        "--android-package", args.android_package]
            if args.adb:
                command += ["--adb", str(args.adb.resolve())]
        else:
            command += ["--runtime-dir", str(args.runtime_dir.resolve())]
        process = subprocess.Popen(
            command,
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=None,
            text=True, encoding="utf-8")
        timer = threading.Timer(args.timeout, process.kill)
        timer.daemon = True
        timer.start()
        send(process, {"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {
            "protocolVersion": "2025-06-18", "capabilities": {},
            "clientInfo": {"name": "engine-mcp-client", "version": "0.1.0"}}})
        initialized = receive(process, 1)
        if initialized.get("protocolVersion") != "2025-06-18":
            raise RuntimeError("MCP server selected an unsupported protocol version")
        send(process, {"jsonrpc": "2.0", "method": "notifications/initialized"})
        send(process, {"jsonrpc": "2.0", "id": 2, "method": "tools/call", "params": {
            "name": tool, "arguments": arguments}})
        result = receive(process, 2)
        for block in result.get("content", []):
            if not isinstance(block, dict) or block.get("type") != "text":
                continue
            text = block.get("text", "")
            try:
                print(json.dumps(json.loads(text), indent=2, ensure_ascii=False))
            except json.JSONDecodeError:
                print(text)
        process.stdin.close()
        exit_code = process.wait(timeout=10)
        return 1 if result.get("isError", False) or exit_code != 0 else 0
    except (OSError, ValueError, RuntimeError, subprocess.TimeoutExpired) as error:
        print(f"MCP call failed: {error}", file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        print("MCP call interrupted", file=sys.stderr)
        return 130
    finally:
        if timer is not None:
            timer.cancel()
        if process is not None and process.poll() is None:
            process.kill()
            process.wait()


if __name__ == "__main__":
    raise SystemExit(main())
