"""带实例核对、完成回执及调用记录的本地编辑器 MCP 会话。"""

from datetime import datetime, timezone
import json
from pathlib import Path
import struct
import subprocess
import threading
import time

from mcp_client import receive, send


def utc_now():
    return datetime.now(timezone.utc).isoformat()


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
                raise RuntimeError("MCP 协议版本不匹配")
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
            raise RuntimeError(f"MCP {name} 回执项目不匹配")
        if receipt.get("runtimeDir") != str(self.runtime_dir):
            raise RuntimeError(f"MCP {name} 回执运行目录不匹配")
        session_id = receipt.get("session")
        if not isinstance(session_id, str) or not session_id:
            raise RuntimeError(f"MCP {name} 回执缺少编辑器会话身份")
        if self.session_id is None:
            self.session_id = session_id
        elif session_id != self.session_id:
            raise RuntimeError("巡览过程中编辑器实例已变化，停止采集")

    def call(self, name, arguments=None):
        if name not in self.tools:
            raise RuntimeError(f"当前 MCP 未注册工具 {name}")
        deadline = time.monotonic() + self.timeout
        result, receipt = self.request_tool(name, arguments or {})
        while receipt is not None:
            self.validate_identity(name, receipt)
            if receipt.get("activation") != "pending":
                break
            identity = {"session": receipt.get("session"), "requestId": receipt.get("requestId")}
            if not all(isinstance(value, str) and value for value in identity.values()):
                raise RuntimeError(f"MCP {name} 待处理回执缺少请求身份")
            if time.monotonic() >= deadline:
                raise RuntimeError(f"MCP {name} 仍在处理；原请求已记录，不重复发送")
            time.sleep(0.1)
            result, receipt = self.request_tool("get_editor_receipt", identity)
            if receipt and any(receipt.get(key) != value for key, value in identity.items()):
                raise RuntimeError(f"MCP {name} 返回了其他请求的回执")
        succeeded = receipt.get("success", receipt.get("lastRequestSucceeded")) if receipt else False
        if result.get("isError") or succeeded is not True:
            raise RuntimeError(f"MCP {name} 失败：{json.dumps(receipt or result, ensure_ascii=False)}")
        if receipt.get("activation") != "completed":
            raise RuntimeError(f"MCP {name} 未完成，停止采集")
        return receipt.get("result", {})

    def capture(self, relative_path):
        cache = (self.project / "cache").resolve()
        expected = (cache / relative_path).resolve()
        if not expected.is_relative_to(cache) or expected.exists():
            raise RuntimeError("截图路径越出项目 cache 或已存在")
        deadline = time.monotonic() + self.timeout
        result = self.call("capture_frame", {"path": relative_path})
        while True:
            if result.get("path") != str(expected):
                raise RuntimeError("截图回执路径与请求不一致")
            state = result.get("state")
            if state == "complete":
                with expected.open("rb") as source:
                    header = source.read(24)
                if (len(header) != 24 or header[:8] != b"\x89PNG\r\n\x1a\n"
                        or header[12:16] != b"IHDR"):
                    raise RuntimeError("截图不是有效的 PNG 文件")
                width, height = struct.unpack(">II", header[16:24])
                if width == 0 or height == 0:
                    raise RuntimeError("截图尺寸无效")
                return {"path": str(expected), "state": state, "width": width,
                        "height": height, "bytes": expected.stat().st_size}
            if state != "pending":
                raise RuntimeError(f"截图未成功完成：{state}")
            if time.monotonic() >= deadline:
                raise RuntimeError("截图仍在处理；请查询 get_frame_capture，不重复发送截图请求")
            time.sleep(0.1)
            result = self.call("get_frame_capture")
