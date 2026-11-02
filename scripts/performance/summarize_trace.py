#!/usr/bin/env python3
"""导出官方 Tracy 数据，按明确时间窗口生成可复用的离线性能摘要。"""
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
        raise ValueError(f"官方导出包含未转义的分隔符或换行：{path.name}")
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
    """Task.execute 可有动态名称；从官方 value 列读取单行排队注释。"""
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
            raise ValueError("匹配的 Tracy 消息必须包含 JSON 对象")
        if first is None:
            if args.start_field and payload.get(args.start_field) != expected:
                continue
            first = int(row["total_ns"])
        if payload.get(args.complete_field) is True:
            last = int(row["total_ns"])
            break
    if first is None or last is None or last <= first:
        raise ValueError("找不到完整的开始/结束消息；请检查 prefix、start-field 和 complete-field")
    frames = [row for row in zones if row["name"] == args.frame_zone]
    containing_first = [row for row in frames if event_start(row) <= first < event_end(row)]
    containing_last = [row for row in frames if event_start(row) <= last < event_end(row)]
    if len(containing_first) != 1 or len(containing_last) != 1:
        raise ValueError("开始或结束消息没有唯一的完整 frame-zone；请延长采集边界或显式指定窗口")
    if containing_first[0]["thread"] != containing_last[0]["thread"]:
        raise ValueError("开始和结束消息位于不同的 frame-zone 线程")
    return event_start(containing_first[0]), event_end(containing_last[0]), {
        "mode": "messages", "prefix": args.message_prefix,
        "first_message_ns": first, "complete_message_ns": last,
        "frame_zone": args.frame_zone,
        "expansion": "containing Host.frame start/end to keep whole frames"}


def analyze(args) -> Path:
    output = args.output_dir.resolve()
    if not output.is_relative_to((ROOT / "build").resolve()):
        raise ValueError("分析产物目录必须位于仓库 build/ 下")
    if output.exists() and any(output.iterdir()):
        raise ValueError("输出目录非空；请选新的分析目录，避免覆盖已有结果")
    trace = args.trace.resolve(strict=True)
    csvexport = args.csvexport.resolve(strict=True)
    frameexport = args.frameexport.resolve(strict=True)
    version = subprocess.run([str(csvexport), "--version"], capture_output=True, text=True,
                             check=True, timeout=10).stdout.strip()
    if not version.startswith("tracy-csvexport 0.14.1 "):
        raise ValueError(f"需要官方 Tracy 0.14.1 导出器，实际为：{version}")
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
        raise ValueError("采集没有完整 FrameMark 区间，不能生成帧时长统计")
    window = {"mode": "explicit" if args.start_ns is not None or args.end_ns is not None else "complete_frames"}
    start = args.start_ns if args.start_ns is not None else min(int(r["start_ns"]) for r in frames)
    end = args.end_ns if args.end_ns is not None else max(int(r["end_ns"]) for r in frames)
    if args.message_prefix:
        start, end, window = messages_window(args, messages, zones)
    if start < 0 or end <= start:
        raise ValueError("时间窗口必须满足 0 <= start-ns < end-ns")
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
                    raise ValueError(f"plot 含非有限值：{row['name']}")
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
                raise ValueError("完整 zone 的 self time 小于零，拒绝生成误导统计")
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
            raise ValueError(f"inclusive/self 事件数量不一致：{name}")
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
        raise ValueError("所选窗口内没有完整 FrameMark 区间")
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
    parser.add_argument("--output-dir", type=Path, required=True, help="仓库 build/ 下的空目录")
    parser.add_argument("--start-ns", type=int)
    parser.add_argument("--end-ns", type=int)
    parser.add_argument("--message-prefix", help="前缀后是 JSON 对象的 Tracy 消息，例如 Rocket range sample")
    parser.add_argument("--start-field", help="可选：只将此字段等于 start-value 的消息作为起点")
    parser.add_argument("--start-value", default="1", help="JSON 值，默认 1")
    parser.add_argument("--complete-field", default="complete", help="此字段为 JSON true 时结束")
    parser.add_argument("--frame-zone", default="Host.frame")
    parser.add_argument("--host-work-plot", default="Host work ms")
    parser.add_argument("--worst-frames", type=int, default=3)
    args = parser.parse_args()
    if args.message_prefix and (args.start_ns is not None or args.end_ns is not None):
        parser.error("message-prefix 与显式时间窗口不能同时使用")
    if args.start_field and not args.message_prefix:
        parser.error("start-field 需要 message-prefix")
    if not 0 <= args.worst_frames <= 20:
        parser.error("worst-frames 必须在 0..20 之间")
    try:
        print(analyze(args))
    except (OSError, ValueError, KeyError, subprocess.SubprocessError) as error:
        detail = getattr(error, "stderr", None)
        print(f"分析失败：{error}" + (f"\n{detail.strip()}" if detail else ""), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
