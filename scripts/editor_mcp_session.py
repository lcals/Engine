"""Local editor MCP session with instance checks, completion receipts and a call ledger."""

from datetime import datetime, timezone
import json
from pathlib import Path
import struct
import subprocess
import threading
import time
import zlib

from mcp_client import receive, send


def utc_now():
    return datetime.now(timezone.utc).isoformat()


def opaque_black_png(path):
    """Inspect engine-written, unfiltered RGBA PNGs; return None for other formats."""
    data = Path(path).read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        return None
    position = 8
    header = None
    compressed = bytearray()
    while position + 12 <= len(data):
        length = struct.unpack_from(">I", data, position)[0]
        end = position + 12 + length
        if end > len(data):
            return None
        kind = data[position + 4:position + 8]
        contents = data[position + 8:position + 8 + length]
        if kind == b"IHDR" and length == 13:
            header = struct.unpack(">IIBBBBB", contents)
        elif kind == b"IDAT":
            compressed.extend(contents)
        elif kind == b"IEND":
            break
        position = end
    if header is None:
        return None
    width, height, depth, color, compression, filtering, interlace = header
    if not width or not height or (depth, color, compression, filtering, interlace) != (8, 6, 0, 0, 0):
        return None
    try:
        raw = zlib.decompress(compressed)
    except zlib.error:
        return None
    stride = width * 4 + 1
    if len(raw) != stride * height:
        return None
    black_row = b"\x00\x00\x00\xff" * width
    for offset in range(0, len(raw), stride):
        if raw[offset] != 0:
            return None
        if raw[offset + 1:offset + stride] != black_row:
            return False
    return True


class EditorMcpSession:
    def __init__(self, server, project, runtime_dir, receipts, timeout=120):
        self.server = Path(server).resolve()
        self.project = Path(project).resolve()
        self.runtime_dir = Path(runtime_dir).resolve()
        self.receipts = Path(receipts)
        self.timeout = timeout
        self.process = None
        self.ledger = None
        self.next_id = 1
        self.session_id = None
        self.tools = set()
        self.prepare_id = None
        self.scene_load_baseline = set()

    def __enter__(self):
        try:
            self.ledger = self.receipts.open("x", encoding="utf-8")
            self.process = subprocess.Popen(
                [str(self.server), "--project", str(self.project),
                 "--runtime-dir", str(self.runtime_dir)],
                stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                text=True, encoding="utf-8",
            )
            result = self.rpc("initialize", {
                "protocolVersion": "2025-06-18", "capabilities": {},
                "clientInfo": {"name": "engine-visual-review", "version": "1.0.0"},
            })
            if result.get("protocolVersion") != "2025-06-18":
                raise RuntimeError("MCP protocol version mismatch")
            send(self.process, {"jsonrpc": "2.0", "method": "notifications/initialized"})
            cursor = None
            while True:
                listing = self.rpc("tools/list", {"cursor": cursor} if cursor else {})
                self.tools.update(item["name"] for item in listing.get("tools", []))
                cursor = listing.get("nextCursor")
                if not cursor:
                    break
            return self
        except BaseException:
            self.close()
            raise

    def __exit__(self, _kind, _value, _traceback):
        if (_kind is not None and self.session_id is not None and self.ledger is not None and
                self.process is not None and self.process.poll() is None and
                {"capture_frame", "get_frame_capture"} <= self.tools):
            try:
                try:
                    relative = self.receipts.parent.resolve().relative_to(
                        (self.project / "cache").resolve())
                except ValueError:
                    relative = Path("visual-failures") / str(time.time_ns())
                previous_timeout = self.timeout
                self.timeout = min(self.timeout, 8)
                try:
                    capture = self.capture(str(relative / f"failure-{time.time_ns()}.png"),
                                           inspect_black=True)
                    self.ledger.write(json.dumps({"time": utc_now(), "failureCapture": capture}) + "\n")
                    self.ledger.flush()
                finally:
                    self.timeout = previous_timeout
            except (OSError, ValueError, RuntimeError) as error:
                try:
                    self.ledger.write(json.dumps({"time": utc_now(), "failureCaptureError": str(error)}) + "\n")
                    self.ledger.flush()
                except OSError:
                    pass
        self.close()

    def close(self):
        # 只关闭本工具拥有的 MCP 子进程；编辑器窗口继续由调用方管理。
        if self.process is not None:
            if self.process.poll() is None:
                try:
                    self.process.stdin.close()
                except OSError:
                    pass
                try:
                    self.process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    self.process.kill()
                    self.process.wait()
            if not self.process.stdin.closed:
                self.process.stdin.close()
            self.process.stdout.close()
        if self.ledger is not None:
            self.ledger.close()

    def rpc(self, method, params):
        request_id = self.next_id
        self.next_id += 1
        timer = threading.Timer(self.timeout, self.process.kill)
        timer.daemon = True
        timer.start()
        try:
            send(self.process, {
                "jsonrpc": "2.0", "id": request_id, "method": method, "params": params,
            })
            return receive(self.process, request_id)
        finally:
            timer.cancel()

    def request_tool(self, name, arguments):
        entry = {"time": utc_now(), "tool": name, "arguments": arguments}
        try:
            result = self.rpc("tools/call", {"name": name, "arguments": arguments})
            receipt = None
            for block in result.get("content", []):
                if block.get("type") != "text":
                    continue
                try:
                    value = json.loads(block.get("text", ""))
                except json.JSONDecodeError:
                    continue
                if isinstance(value, dict) and "activation" in value:
                    receipt = value
                    break
            entry.update(receipt=receipt, is_error=result.get("isError", False))
            if receipt is None:
                entry["response"] = result
            return result, receipt
        except (OSError, ValueError, RuntimeError) as error:
            entry["error"] = str(error)
            raise
        finally:
            self.ledger.write(json.dumps(entry, ensure_ascii=False) + "\n")
            self.ledger.flush()

    def validate_identity(self, name, receipt):
        if receipt.get("projectRoot") != str(self.project):
            raise RuntimeError(f"MCP {name} receipt project does not match")
        if receipt.get("runtimeDir") != str(self.runtime_dir):
            raise RuntimeError(f"MCP {name} receipt runtime directory does not match")
        session_id = receipt.get("session")
        if not isinstance(session_id, str) or not session_id:
            raise RuntimeError(f"MCP {name} receipt has no editor session identity")
        if self.session_id is None:
            self.session_id = session_id
        elif session_id != self.session_id:
            raise RuntimeError("Editor instance changed during inspection; stopping capture")

    def call(self, name, arguments=None):
        if name not in self.tools:
            raise RuntimeError(f"Current MCP has not registered tool {name}")
        if name == "start_project":
            before = self.status()
            if not before.get("running"):
                self.scene_load_baseline = {
                    (item.get("owner"), item.get("request"))
                    for item in before.get("loadIds", [])
                }
        deadline = time.monotonic() + self.timeout
        result, receipt = self.request_tool(name, arguments or {})
        while receipt is not None:
            self.validate_identity(name, receipt)
            if receipt.get("activation") != "pending":
                break
            identity = {"session": receipt.get("session"), "requestId": receipt.get("requestId")}
            if not all(isinstance(value, str) and value for value in identity.values()):
                raise RuntimeError(f"MCP {name} pending receipt has no request identity")
            if time.monotonic() >= deadline:
                raise RuntimeError(f"MCP {name} is still processing; the original request is recorded and will not be resent")
            time.sleep(0.1)
            result, receipt = self.request_tool("get_request_receipt", identity)
            if receipt and any(receipt.get(key) != value for key, value in identity.items()):
                raise RuntimeError(f"MCP {name} returned a receipt for another request")
        succeeded = receipt.get("success", receipt.get("lastRequestSucceeded")) if receipt else False
        if result.get("isError") or succeeded is not True:
            raise RuntimeError(f"MCP {name} failed: {json.dumps(receipt or result, ensure_ascii=False)}")
        if receipt.get("activation") != "completed":
            raise RuntimeError(f"MCP {name} is incomplete; stopping capture")
        value = receipt.get("result", {})
        if name == "start_project" and value.get("accepted") and value.get("prepareId") != "0":
            self.prepare_id = value["prepareId"]
        if (name == "release_gameplay_prepare" and value.get("released") and
                value.get("prepareId") == self.prepare_id):
            self.prepare_id = None
        return value

    def capture(self, relative_path, inspect_black=False, scope=None):
        cache = (self.project / "cache").resolve()
        expected = (cache / relative_path).resolve()
        if not expected.is_relative_to(cache) or expected.exists():
            raise RuntimeError("Capture path is outside the project cache or already exists")
        deadline = time.monotonic() + self.timeout
        arguments = {"path": relative_path, "selection": "scene_scope" if scope else "next_frame"}
        if scope:
            arguments["scope"] = scope
        result = self.call("capture_frame", arguments)
        capture_id = result.get("captureId")
        if not isinstance(capture_id, dict):
            raise RuntimeError("Capture receipt has no request identity")
        while True:
            if result.get("captureId") != capture_id or result.get("path") != str(expected):
                raise RuntimeError("Capture receipt path does not match the request")
            state = result.get("state")
            if state == "complete":
                with expected.open("rb") as source:
                    header = source.read(24)
                if (len(header) != 24 or header[:8] != b"\x89PNG\r\n\x1a\n"
                        or header[12:16] != b"IHDR"):
                    raise RuntimeError("Capture is not a valid PNG file")
                width, height = struct.unpack(">II", header[16:24])
                if width == 0 or height == 0:
                    raise RuntimeError("Invalid capture dimensions")
                capture = {"path": str(expected), "state": state, "width": width,
                           "height": height, "bytes": expected.stat().st_size,
                           "captureId": capture_id, "frame": result.get("frame")}
                if inspect_black:
                    capture["opaqueBlack"] = opaque_black_png(expected)
                return capture
            if state not in {"waiting_frame", "reading_back", "writing"}:
                raise RuntimeError(f"Capture did not complete successfully: {state}")
            if time.monotonic() >= deadline:
                raise RuntimeError("Capture is still processing; query get_frame_capture instead of resending the request")
            time.sleep(0.1)
            result = self.call("get_frame_capture", {"captureId": capture_id})

    def status(self):
        result, _ = self.request_tool("get_status", {})
        values = []
        for block in result.get("content", []):
            if block.get("type") == "text":
                try:
                    value = json.loads(block.get("text", ""))
                except json.JSONDecodeError:
                    continue
                if isinstance(value, dict):
                    values.append(value)
        if result.get("isError") or len(values) != 1:
            raise RuntimeError(f"MCP status is unavailable: {result}")
        self.validate_identity("get_status", values[0])
        return values[0]

    def wait_prepare(self, prepare_id, max_wait=30):
        """Observe one accepted startup without replaying the start command."""
        deadline = time.monotonic() + min(max_wait, self.timeout)
        last = None
        while time.monotonic() < deadline:
            last = self.call("get_gameplay_prepare", {"prepareId": prepare_id})
            if last.get("id") != prepare_id:
                raise RuntimeError(f"Startup result identity does not match: {last}")
            if last.get("state") == "active":
                return last
            if last.get("state") in {"failed", "cancelled"}:
                raise RuntimeError(f"Gameplay startup did not complete: {last}")
            time.sleep(0.1)
        raise RuntimeError(f"Gameplay startup timed out; keep querying {prepare_id}: {last}")

    def wait_scene(self, max_wait=30):
        """Wait on runtime scene/load/frame facts; pixel color is only a visual assertion."""
        deadline = time.monotonic() + min(max_wait, self.timeout)
        if self.prepare_id:
            self.wait_prepare(self.prepare_id, max_wait)
        last_status = None
        while time.monotonic() < deadline:
            last_status = self.status()
            if last_status.get("hostErrorCode"):
                raise RuntimeError(f"Runtime host failed: {last_status}")
            loads = [self.call("get_scene_load", {"loadId": identity})
                     for identity in last_status.get("loadIds", [])
                     if (identity.get("owner"), identity.get("request"))
                     not in self.scene_load_baseline]
            failed = [load for load in loads if load.get("state") == "failed"]
            if failed:
                raise RuntimeError(f"Scene preparation failed: {failed}")
            pending = [load for load in loads if
                       load.get("state") not in {"active", "cancelled", "failed"}]
            frame = last_status.get("frame") or {}
            if not pending and frame.get("kind") == "scene" and last_status.get("windowDrawable", True):
                active = [load for load in loads if load.get("targetAlive") and load.get("state") == "active"]
                self.scene_load_baseline.update(
                    (identity.get("owner"), identity.get("request"))
                    for identity in last_status.get("loadIds", []))
                return active[-1]["scope"] if active else {
                    "worldEpoch": frame["worldEpoch"], "value": frame["sceneId"]}
            time.sleep(0.1)
        raise RuntimeError(f"Scene readiness timed out; last status: {last_status}")

    def capture_scene(self, relative_path, max_wait=30):
        scope = self.wait_scene(max_wait)
        return [self.capture(relative_path, inspect_black=True, scope=scope)]
