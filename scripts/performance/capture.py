#!/usr/bin/env python3
"""Capture with official Tracy tools and optionally record local process RSS without controlling the engine or replaying gameplay."""

import argparse
from datetime import datetime, timezone
import errno
import hashlib
import json
import math
import os
from pathlib import Path
import select
import signal
import subprocess
import sys
import time


def utc_now():
    return datetime.now(timezone.utc).isoformat()


def process_sample(pid):
    # macOS/Linux ps 的 rss 单位为 KiB；lstart + comm 防止 PID 被复用。
    result = subprocess.run(
        ["ps", "-p", str(pid), "-o", "lstart=", "-o", "rss=", "-o", "comm="],
        capture_output=True, text=True, check=True,
        env={**os.environ, "LC_ALL": "C"}, timeout=3,
    )
    parts = result.stdout.strip().split(None, 6)
    if len(parts) != 7:
        raise RuntimeError("Cannot read target process identity or RSS; verify --pid")
    identity = {"pid": pid, "started": " ".join(parts[:5]), "command": parts[6]}
    return identity, int(parts[5]) * 1024


def write_report(path, report):
    temporary = path.with_suffix(".tmp")
    temporary.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n",
                         encoding="utf-8")
    temporary.replace(path)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--capture", type=Path, required=True, help="Official capture tool matching the client version")
    parser.add_argument("--output", type=Path, required=True, help="New directory, preferably build/performance/<run>")
    parser.add_argument("--metadata", type=Path, required=True, help="JSON metadata for device, project, scene, revision and operation settings")
    parser.add_argument("--seconds", type=int, default=35, help="Sampling duration in seconds after connection")
    parser.add_argument("--address", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8086)
    parser.add_argument("--pid", type=int, help="Optional local engine process PID for read-only RSS sampling")
    parser.add_argument("--rss-interval", type=float, default=0.5)
    parser.add_argument("--connect-timeout", type=float, default=20)
    args = parser.parse_args()
    if sys.platform not in ("darwin", "linux"):
        parser.error("This wrapper uses macOS/Linux PTYs; on other hosts use official capture directly, then run the analysis script")
    if (args.seconds < 1 or not math.isfinite(args.connect_timeout) or
            args.connect_timeout <= 0 or not math.isfinite(args.rss_interval) or
            args.rss_interval < 0.1 or
            not 1 <= args.port <= 65535 or (args.pid is not None and args.pid <= 0)):
        parser.error("Invalid duration, sampling interval, port or PID")
    import pty

    child = None
    master = None
    report_path = None
    report = {"schema_version": 1, "started_utc": utc_now(), "success": False, "rss": []}
    try:
        capture = args.capture.resolve(strict=True)
        metadata = json.loads(args.metadata.read_text(encoding="utf-8"))
        if not isinstance(metadata, dict):
            raise ValueError("metadata must be a JSON object")
        identity = process_sample(args.pid)[0] if args.pid else None
        output = args.output.resolve()
        output.mkdir(parents=True, exist_ok=False)
        report_path = output / "capture.json"
        trace = output / "capture.tracy"
        command = [str(capture), "-a", args.address, "-p", str(args.port),
                   "-s", str(args.seconds), "-o", str(trace)]
        report.update(metadata=metadata, process=identity, command=command,
                      address=args.address, port=args.port, seconds=args.seconds,
                      trace=trace.name, log="capture.log", rss_interval_seconds=args.rss_interval)
        write_report(report_path, report)
        with (output / "capture.log").open("xb") as log:
            master, slave = pty.openpty()
            try:
                child = subprocess.Popen(command, stdin=subprocess.DEVNULL,
                                         stdout=slave, stderr=subprocess.STDOUT,
                                         start_new_session=True)
            finally:
                os.close(slave)
            report["capture_pid"] = child.pid
            os.set_blocking(master, False)
            begin = time.monotonic()
            connected = None
            next_sample = begin
            tail = b""
            connection_seen = False
            eof = False
            while True:
                if not eof and select.select([master], [], [], 0.1)[0]:
                    try:
                        chunk = os.read(master, 65536)
                    except OSError as error:
                        if error.errno != errno.EIO:
                            raise
                        chunk = b""
                    if chunk:
                        log.write(chunk)
                        log.flush()
                        # 标记可能跨块，或位于大块输出开头；先检查再截断尾部。
                        connection_seen |= b"Timer resolution:" in tail + chunk
                        tail = (tail + chunk)[-4096:]
                    else:
                        eof = True
                elif eof:
                    time.sleep(0.1)
                now = time.monotonic()
                if connected is None and connection_seen:
                    connected = now
                    report["connected_utc"] = utc_now()
                    write_report(report_path, report)
                    print(json.dumps({"event": "connected", "report": str(report_path)},
                                     ensure_ascii=False), flush=True)
                if connected is not None and args.pid and now >= next_sample:
                    current, rss = process_sample(args.pid)
                    if current != identity:
                        raise RuntimeError("Target process identity changed; stopping sampling without operating on the process")
                    report["rss"].append({"elapsed_seconds": now - connected, "bytes": rss})
                    next_sample = now + args.rss_interval
                # 子进程退出后 PTY 仍可能有尾部输出，读到 EOF 才判定完成。
                if child.poll() is not None and eof:
                    break
                if connected is None and now - begin > args.connect_timeout:
                    raise RuntimeError("Tracy connection timed out; check client build options, address and port")
                if connected is not None and now - connected > args.seconds + 30:
                    raise RuntimeError("Official capture tool did not finish on time; logs and received records are preserved")
            report["capture_exit_code"] = child.returncode
            if connected is None or child.returncode != 0 or not trace.is_file():
                raise RuntimeError("Official capture tool did not produce a valid record; inspect capture.log and retry in a new output directory")
            if trace.stat().st_size == 0:
                raise RuntimeError("Capture file is empty; inspect capture.log")
            digest = hashlib.sha256()
            with trace.open("rb") as stream:
                for chunk in iter(lambda: stream.read(1024 * 1024), b""):
                    digest.update(chunk)
            report.update(success=True, trace_bytes=trace.stat().st_size,
                          trace_sha256=digest.hexdigest())
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError, KeyboardInterrupt) as error:
        report["error"] = str(error) or "Sampling interrupted"
        print(f"Sampling did not complete: {report['error']}. Existing records will not be overwritten.", file=sys.stderr)
    finally:
        # 只结束本工具创建的 capture 子进程，不向目标引擎发送信号。
        if child is not None and child.poll() is None:
            child.send_signal(signal.SIGINT)
            try:
                child.wait(timeout=10)
            except subprocess.TimeoutExpired:
                child.kill()
                child.wait()
        if master is not None:
            os.close(master)
        if child is not None:
            report["capture_exit_code"] = child.returncode
        report["ended_utc"] = utc_now()
        if report["rss"]:
            values = [item["bytes"] for item in report["rss"]]
            report["rss_summary_bytes"] = {"first": values[0], "last": values[-1],
                                           "min": min(values), "max": max(values)}
        if report_path is not None:
            try:
                write_report(report_path, report)
            except OSError as error:
                report["success"] = False
                print(f"Cannot save the sampling report: {error}. Preserve the output directory and inspect logs and file permissions.",
                      file=sys.stderr)
    if report["success"]:
        print(json.dumps({"event": "complete", "report": str(report_path)},
                         ensure_ascii=False), flush=True)
    return 0 if report["success"] else 1


if __name__ == "__main__":
    sys.exit(main())
