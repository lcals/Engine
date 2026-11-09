#!/usr/bin/env python3
"""Export official Tracy data and produce reusable offline summaries for an explicit time window."""
from __future__ import annotations

import argparse
from collections import defaultdict
import csv
import hashlib
import json
import math
from pathlib import Path
import re
import statistics
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]


def read_rows(path: Path, delimiter: str = "\t") -> list[dict[str, str]]:
    # 上游 csvexport 不转义分隔符。发现嵌入 tab/newline 时拒绝错误数据，不能静默统计。
    with path.open(encoding="utf-8", newline="") as stream:
        reader = csv.DictReader(stream, delimiter=delimiter, quoting=csv.QUOTE_NONE)
        rows = list(reader)
    if any(None in row or any(value is None for value in row.values()) for row in rows):
        raise ValueError(f"Official export contains an unescaped delimiter or newline: {path.name}")
    return rows


def export(path: Path, command: list[str]) -> None:
    temporary = path.with_suffix(path.suffix + ".tmp")
    try:
        with temporary.open("w", encoding="utf-8") as stream:
            subprocess.run(command, stdout=stream, stderr=subprocess.PIPE, text=True,
                           check=True, timeout=120)
        temporary.replace(path)
    finally:
        temporary.unlink(missing_ok=True)


def percentile(values: list[float], quantile: float) -> float:
    position = quantile * (len(values) - 1)
    low, high = math.floor(position), math.ceil(position)
    return values[low] + (values[high] - values[low]) * (position - low)


def statistics_of(values: list[float]) -> dict:
    if not values:
        return {"count": 0}
    ordered = sorted(values)
    return {"count": len(values), "sum": sum(values), "mean": statistics.fmean(values),
            "min": ordered[0], "p50": percentile(ordered, 0.5),
            "p95": percentile(ordered, 0.95), "p99": percentile(ordered, 0.99),
            "max": ordered[-1]}


def file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def event_key(row: dict) -> tuple:
    return row["name"], row["src_file"], row["src_line"], row["thread"]


def event_start(row: dict) -> int:
    return int(row["ns_since_start"])


def event_end(row: dict) -> int:
    return event_start(row) + int(row["exec_time_ns"])


def task_queue_summary(events: list[dict]) -> dict:
    """Task.execute may have a dynamic name; read single-line queue annotations from the official value column."""
    groups = defaultdict(list)
    missing = invalid = 0
    pattern = re.compile(r"domain=(.*?); task=(.*); priority=(low|normal|high); queued_ns=(\d+)")
    for row in events:
        details = row.get("value", "")
        if row["name"] != "Task.execute" and not (
                row["src_file"].replace("\\", "/").endswith("/task_system.cpp")
                and details.startswith("domain=")):
            continue
        if not details:
            missing += 1
            continue
        match = pattern.fullmatch(details)
        if match is None:
            invalid += 1
            continue
        domain, task, priority, queued = match.groups()
        groups[domain, task, priority].append(
            (int(queued) / 1e6, int(row["exec_time_ns"]) / 1e6, row["thread"]))
    return {
        "groups": [{"domain": domain, "task": task, "priority": priority,
                    "threads": sorted({item[2] for item in samples}),
                    "queued_ms": statistics_of([item[0] for item in samples]),
                    "execution_ms": statistics_of([item[1] for item in samples])}
                   for (domain, task, priority), samples in sorted(groups.items())],
        "missing_metadata_events": missing, "invalid_metadata_events": invalid,
        "notes": ["queued_ms is submission-to-start wall time, including batch dependencies",
                  "execution_ms is instrumented thread elapsed time, not CPU utilization",
                  "only completed execution zones inside the selected window are included; queued work without a completed zone is absent"]}


def messages_window(args, messages: list[dict], zones: list[dict]) -> tuple[int, int, dict]:
    first = last = None
    expected = json.loads(args.start_value) if args.start_field else None
    for row in messages:
        text = row["MessageName"]
        if not text.startswith(args.message_prefix):
            continue
        payload = json.loads(text[len(args.message_prefix):].strip())
        if not isinstance(payload, dict):
            raise ValueError("Matching Tracy messages must contain JSON objects")
        if first is None:
            if args.start_field and payload.get(args.start_field) != expected:
                continue
            first = int(row["total_ns"])
        if payload.get(args.complete_field) is True:
            last = int(row["total_ns"])
            break
    if first is None or last is None or last <= first:
        raise ValueError("Complete start/end messages were not found; check prefix, start-field and complete-field")
    frames = [row for row in zones if row["name"] == args.frame_zone]
    containing_first = [row for row in frames if event_start(row) <= first < event_end(row)]
    containing_last = [row for row in frames if event_start(row) <= last < event_end(row)]
    if len(containing_first) != 1 or len(containing_last) != 1:
        raise ValueError("Start or end message has no unique complete frame-zone; extend capture boundaries or specify the window explicitly")
    if containing_first[0]["thread"] != containing_last[0]["thread"]:
        raise ValueError("Start and end messages belong to different frame-zone threads")
    return event_start(containing_first[0]), event_end(containing_last[0]), {
        "mode": "messages", "prefix": args.message_prefix,
        "first_message_ns": first, "complete_message_ns": last,
        "frame_zone": args.frame_zone,
        "expansion": "containing Host.frame start/end to keep whole frames"}


def analyze(args) -> Path:
    output = args.output_dir.resolve()
    if not output.is_relative_to((ROOT / "build").resolve()):
        raise ValueError("Analysis artifacts must be under the repository build/ directory")
    if output.exists() and any(output.iterdir()):
        raise ValueError("Output directory is not empty; select a new analysis directory to preserve earlier results")
    trace = args.trace.resolve(strict=True)
    csvexport = args.csvexport.resolve(strict=True)
    frameexport = args.frameexport.resolve(strict=True)
    version = subprocess.run([str(csvexport), "--version"], capture_output=True, text=True,
                             check=True, timeout=10).stdout.strip()
    if not version.startswith("tracy-csvexport 0.14.1 "):
        raise ValueError(f"Official Tracy 0.14.1 exporter is required; found: {version}")
    output.mkdir(parents=True, exist_ok=True)
    export(output / "zones.tsv", [str(csvexport), "-s", "\t", "-u", "-p", str(trace)])
    export(output / "self.tsv", [str(csvexport), "-s", "\t", "-u", "-e", str(trace)])
    export(output / "messages.tsv", [str(csvexport), "-s", "\t", "-m", str(trace)])
    export(output / "frames.csv", [str(frameexport), str(trace)])
    events = read_rows(output / "zones.tsv")
    own = read_rows(output / "self.tsv")
    frames = read_rows(output / "frames.csv", ",")
    # 没有消息时，上游输出一行说明而非表格；DictReader 得到空列表。
    messages = read_rows(output / "messages.tsv")
    zones = [row for row in events if row["exec_time_ns"] and int(row["exec_time_ns"]) >= 0]
    if not frames:
        raise ValueError("Capture has no complete FrameMark interval for frame-duration statistics")
    window = {"mode": "explicit" if args.start_ns is not None or args.end_ns is not None else "complete_frames"}
    start = args.start_ns if args.start_ns is not None else min(int(r["start_ns"]) for r in frames)
    end = args.end_ns if args.end_ns is not None else max(int(r["end_ns"]) for r in frames)
    if args.message_prefix:
        start, end, window = messages_window(args, messages, zones)
    if start < 0 or end <= start:
        raise ValueError("Time window must satisfy 0 <= start-ns < end-ns")
    window.update(start_ns=start, end_ns=end, seconds=(end - start) / 1e9)
    inclusive = defaultdict(list)
    self_times = defaultdict(list)
    allowed = set()
    plots = defaultdict(list)
    selected = []
    for row in events:
        timestamp = event_start(row)
        if not row["exec_time_ns"]:
            if start <= timestamp < end:
                value = float(row["value"])
                if not math.isfinite(value):
                    raise ValueError(f"Plot contains a nonfinite value: {row['name']}")
                plots[row["name"]].append((timestamp, value))
            continue
        duration = int(row["exec_time_ns"])
        if duration < 0 or timestamp < start or timestamp + duration > end:
            continue
        key = event_key(row)
        inclusive[key].append(duration / 1e6)
        allowed.add(key + (timestamp,))
        selected.append(row)
    for row in own:
        key = event_key(row)
        if key + (event_start(row),) in allowed:
            duration = int(row["exec_time_ns"])
            if duration < 0:
                raise ValueError("Complete zone has negative self time; refusing to produce misleading statistics")
            self_times[key].append(duration / 1e6)
    plot_stats = {}
    for name, samples in plots.items():
        samples.sort()
        plot_stats[name] = dict(statistics_of([v for _, v in samples]),
                                first=samples[0][1], last=samples[-1][1],
                                first_timestamp_ns=samples[0][0], last_timestamp_ns=samples[-1][0])
    host_work = plot_stats.get(args.host_work_plot, {}).get("sum")
    grouped = []
    for key, values in inclusive.items():
        name, source, line, thread = key
        if len(values) != len(self_times[key]):
            raise ValueError(f"Inclusive/self event counts differ: {name}")
        value = {"name": name, "source": source, "line": int(line), "thread": thread,
                 "inclusive_ms": statistics_of(values), "self_ms": statistics_of(self_times[key])}
        if host_work is not None and host_work > 0:
            value["self_percent_host_work"] = value["self_ms"]["sum"] / host_work * 100
            value["inclusive_percent_host_work"] = value["inclusive_ms"]["sum"] / host_work * 100
        grouped.append(value)
    grouped.sort(key=lambda item: item["self_ms"]["sum"], reverse=True)
    frame_values = [int(row["duration_ns"]) / 1e6 for row in frames
                    if int(row["start_ns"]) >= start and int(row["end_ns"]) <= end]
    if not frame_values:
        raise ValueError("Selected window has no complete FrameMark interval")
    worst = sorted((row for row in selected if row["name"] == args.frame_zone),
                   key=lambda row: int(row["exec_time_ns"]), reverse=True)[:args.worst_frames]
    worst_frames = []
    for frame in worst:
        children = defaultdict(list)
        for row in selected:
            if (row["thread"] == frame["thread"] and event_start(frame) <= event_start(row)
                    and event_end(row) <= event_end(frame)):
                children[row["name"]].append(int(row["exec_time_ns"]) / 1e6)
        worst_frames.append({"start_ns": event_start(frame), "end_ns": event_end(frame),
                             "duration_ms": int(frame["exec_time_ns"]) / 1e6,
                             "thread": frame["thread"],
                             "inclusive_zones_ms": sorted(
                                 ({"name": name, **statistics_of(values)} for name, values in children.items()),
                                 key=lambda item: item["sum"], reverse=True)})
    result = {"trace": str(trace), "trace_sha256": file_sha256(trace),
              "exporter": version, "window": window,
              "excluded_zone_events": sum(bool(r["exec_time_ns"]) for r in events) - len(selected),
              "frame_ms": statistics_of(frame_values),
              "frame_fps": 1000 / statistics.fmean(frame_values),
              "frames_over_50_ms": sum(v > 50 for v in frame_values),
              "frames_over_100_ms": sum(v > 100 for v in frame_values),
              "zones": grouped, "plots": plot_stats, "worst_frames": worst_frames,
              "task_queue": task_queue_summary(selected),
              "messages": [row for row in messages if start <= int(row["total_ns"]) < end],
              "notes": ["CPU zones are thread elapsed wall time, not CPU cycles or utilization",
                        "inclusive parent/child totals overlap; self subtracts instrumented children only",
                        "only complete zones and adjacent FrameMarks wholly inside window are included",
                        "frame intervals include limiter/presentation waits; Host work excludes explicit limiter only",
                        "plot statistics are sample-weighted; bgfx samples may be delayed",
                        "percentiles use linear interpolation with no outlier removal",
                        "host-work percentages across multiple threads can exceed 100%"]}
    path = output / "summary.json"
    path.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    return path


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--trace", type=Path, required=True)
    parser.add_argument("--csvexport", type=Path, required=True)
    parser.add_argument("--frameexport", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True, help="Empty directory under the repository build/ directory")
    parser.add_argument("--start-ns", type=int)
    parser.add_argument("--end-ns", type=int)
    parser.add_argument("--message-prefix", help="Tracy message prefix followed by a JSON object, for example Rocket range sample")
    parser.add_argument("--start-field", help="Optional field that must equal start-value for a starting message")
    parser.add_argument("--start-value", default="1", help="JSON value; defaults to 1")
    parser.add_argument("--complete-field", default="complete", help="Finish when this field is JSON true")
    parser.add_argument("--frame-zone", default="Host.frame")
    parser.add_argument("--host-work-plot", default="Host work ms")
    parser.add_argument("--worst-frames", type=int, default=3)
    args = parser.parse_args()
    if args.message_prefix and (args.start_ns is not None or args.end_ns is not None):
        parser.error("message-prefix cannot be combined with an explicit time window")
    if args.start_field and not args.message_prefix:
        parser.error("start-field requires message-prefix")
    if not 0 <= args.worst_frames <= 20:
        parser.error("worst-frames must be between 0 and 20")
    try:
        print(analyze(args))
    except (OSError, ValueError, KeyError, subprocess.SubprocessError) as error:
        detail = getattr(error, "stderr", None)
        print(f"Analysis failed: {error}" + (f"\n{detail.strip()}" if detail else ""), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
