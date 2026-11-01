#!/usr/bin/env python3
"""在已有 Tracy 分析窗口内统计地形同步碰撞提交预算，不启动引擎或重新导出 trace。"""
from __future__ import annotations

import argparse
from bisect import bisect_left, bisect_right
from collections import defaultdict
import csv
from dataclasses import dataclass
import json
from pathlib import Path
import sys

# 复用统计口径时也不在源码目录生成 __pycache__。
sys.dont_write_bytecode = True
from summarize_trace import statistics_of

ROOT = Path(__file__).resolve().parents[2]
BUDGET_NS = 500_000
COMMIT = "Terrain.commit"
FRAME = "Host.frame"
PREPARE = "Terrain.prepare_collision"
RELEASE = "Terrain.release_previous_volume"
SET_NAMES = ("Physics.set_mesh_parts", "Physics.set_mesh")
SHAPE_NAMES = ("Physics.create_mesh_compound", "Physics.create_shape")
CHUNK = "Terrain.mesh_chunk"
FRAGMENT = "Terrain.fragment_mesh"
COLLAPSE = "Terrain.collapse"
SELECTED_NAMES = {COMMIT, FRAME, PREPARE, RELEASE, CHUNK, FRAGMENT, COLLAPSE,
                  *SET_NAMES, *SHAPE_NAMES}


@dataclass(frozen=True, slots=True)
class Event:
    name: str
    thread: str
    start: int
    end: int

    @property
    def duration(self) -> int:
        return self.end - self.start


class ZoneIndex:
    """只保留相关 zone；区间查询按线程和名称二分，避免为每次提交重扫原始 TSV。"""

    def __init__(self, events: list[Event]):
        self.groups: dict[tuple[str, str], list[Event]] = defaultdict(list)
        self.names: dict[str, list[Event]] = defaultdict(list)
        for event in events:
            self.groups[event.thread, event.name].append(event)
            self.names[event.name].append(event)
        self.starts = {}
        for key, group in self.groups.items():
            group.sort(key=lambda event: (event.start, event.end))
            self.starts[key] = [event.start for event in group]
            # containing() 只用于这些不递归的同线程父区间，重叠会造成错误归属。
            if key[1] in (COMMIT, FRAME, FRAGMENT, COLLAPSE):
                if any(left.end > right.start for left, right in zip(group, group[1:])):
                    raise ValueError(f"同线程父区间发生重叠，无法唯一归属：{key[1]}")
        for group in self.names.values():
            group.sort(key=lambda event: (event.start, event.thread, event.end))

    def children(self, parent: Event, name: str) -> list[Event]:
        key = parent.thread, name
        group = self.groups.get(key, ())
        starts = self.starts.get(key, ())
        first = bisect_left(starts, parent.start)
        last = bisect_right(starts, parent.end)
        return [event for event in group[first:last] if event.end <= parent.end]

    def containing(self, child: Event, name: str) -> Event | None:
        key = child.thread, name
        position = bisect_right(self.starts.get(key, ()), child.start) - 1
        if position >= 0:
            parent = self.groups[key][position]
            if child.end <= parent.end:
                return parent
        return None


def read_events(path: Path, start: int, end: int) -> tuple[list[Event], dict]:
    events = []
    counts = {"rows": 0, "ignored_plot_or_empty_time_rows": 0,
              "excluded_relevant_zone_rows": 0, "selected_zone_rows": 0}
    with path.open(encoding="utf-8", newline="") as stream:
        reader = csv.DictReader(stream, delimiter="\t", quoting=csv.QUOTE_NONE)
        required = {"name", "thread", "ns_since_start", "exec_time_ns"}
        if not required.issubset(reader.fieldnames or ()):
            raise ValueError("zones.tsv 缺少 name/thread/ns_since_start/exec_time_ns 列")
        for row in reader:
            counts["rows"] += 1
            if None in row or any(value is None for value in row.values()):
                raise ValueError(f"zones.tsv 第 {reader.line_num} 行包含未转义的分隔符或换行")
            # 官方 plot 行没有 exec_time_ns，部分导出行也没有时间戳。
            if not row["ns_since_start"].strip() or not row["exec_time_ns"].strip():
                counts["ignored_plot_or_empty_time_rows"] += 1
                continue
            if row["name"] not in SELECTED_NAMES:
                continue
            try:
                timestamp = int(row["ns_since_start"])
                duration = int(row["exec_time_ns"])
            except ValueError as error:
                raise ValueError(f"zones.tsv 第 {reader.line_num} 行的时间不是整数") from error
            if duration < 0 or timestamp < start or timestamp + duration > end:
                counts["excluded_relevant_zone_rows"] += 1
                continue
            if not row["thread"]:
                raise ValueError(f"zones.tsv 第 {reader.line_num} 行的 zone 缺少线程")
            events.append(Event(row["name"], row["thread"], timestamp, timestamp + duration))
    counts["selected_zone_rows"] = len(events)
    return events, counts


def timing(values: list[int]) -> dict:
    result = statistics_of([value / 1e6 for value in values])
    result.update(over_0_5_ms=sum(value > BUDGET_NS for value in values),
                  at_or_over_budget=sum(value >= BUDGET_NS for value in values))
    return result


def sum_disjoint(events: list[Event]) -> int:
    ordered = sorted(events, key=lambda event: (event.start, event.end))
    if any(left.end > right.start for left, right in zip(ordered, ordered[1:])):
        raise ValueError("计入预算的 prepare_collision/set/release 区间发生重叠，拒绝重复累计")
    return sum(event.duration for event in ordered)


def commit_record(index: ZoneIndex, commit: Event, identifier: int,
                  frame_ids: dict[Event, int]) -> dict:
    preparation = index.children(commit, PREPARE)
    releases = index.children(commit, RELEASE)
    submissions = sorted((event for name in SET_NAMES for event in index.children(commit, name)),
                         key=lambda event: event.start)
    shape_counts = {name: sum(len(index.children(event, name)) for event in submissions)
                    for name in SHAPE_NAMES}
    collision_ns = sum_disjoint(preparation + submissions + releases)
    release_ns = sum(event.duration for event in releases) if releases else None
    budget_complete = bool(releases) and bool(submissions)
    coarse_chunks = len(index.children(commit, CHUNK))
    frame = index.containing(commit, FRAME)
    return {
        "id": identifier, "start_ns": commit.start, "end_ns": commit.end,
        "thread": commit.thread, "commit_ms": commit.duration / 1e6,
        "host_frame_id": frame_ids.get(frame),
        "geometry_group": "changed" if any(shape_counts.values()) else "no_change",
        "rebuilt_coarse_chunks": coarse_chunks,
        "full_32_chunk_rebuild": coarse_chunks == 32,
        "within_collapse": index.containing(commit, COLLAPSE) is not None,
        "prepare_collision_calls": len(preparation),
        "release_previous_volume_calls": len(releases),
        "set_calls": {name: sum(event.name == name for event in submissions) for name in SET_NAMES},
        "shape_creation_calls": shape_counts,
        "prepare_collision_ms": sum(event.duration for event in preparation) / 1e6,
        "set_mesh_ms": sum(event.duration for event in submissions) / 1e6,
        "release_previous_volume_ns": release_ns,
        "release_previous_volume_ms": release_ns / 1e6 if release_ns is not None else None,
        "budget_complete": budget_complete,
        "collision_ns": collision_ns, "collision_ms": collision_ns / 1e6,
        "over_0_5_ms": collision_ns > BUDGET_NS,
        "meets_budget": (collision_ns < BUDGET_NS if budget_complete
                         else False if collision_ns >= BUDGET_NS else None),
    }


def records_summary(records: list[dict]) -> dict:
    return {
        "commit_ms": statistics_of([record["commit_ms"] for record in records]),
        "collision_ms": timing([record["collision_ns"] for record in records]),
        "prepare_collision_ms": timing(
            [round(record["prepare_collision_ms"] * 1e6) for record in records]),
        "set_mesh_ms": timing([round(record["set_mesh_ms"] * 1e6) for record in records]),
        "release_previous_volume_ms": timing(
            [record["release_previous_volume_ns"] for record in records
             if record["release_previous_volume_ns"] is not None]),
        "rebuilt_coarse_chunks": statistics_of([record["rebuilt_coarse_chunks"] for record in records]),
    }


def analyze(args) -> Path:
    requested = args.output
    if requested.exists() or requested.is_symlink():
        raise ValueError("输出文件已存在；请指定新文件，不能覆盖已有分析结果")
    output = requested.resolve()
    if not output.is_relative_to((ROOT / "build").resolve()):
        raise ValueError("输出文件必须位于仓库 build/ 下")
    analysis = args.analysis_dir.resolve(strict=True)
    if not analysis.is_dir():
        raise ValueError("analysis-dir 必须是包含 summary.json 和 zones.tsv 的分析目录")
    summary = json.loads((analysis / "summary.json").read_text(encoding="utf-8"))
    window = summary["window"]
    start, end = window["start_ns"], window["end_ns"]
    if type(start) is not int or type(end) is not int or start < 0 or end <= start:
        raise ValueError("summary.json 的窗口必须满足整数 0 <= start_ns < end_ns")
    events, input_counts = read_events(analysis / "zones.tsv", start, end)
    index = ZoneIndex(events)
    commits = index.names.get(COMMIT, ())
    if not commits:
        raise ValueError("summary.json 窗口内没有完整 Terrain.commit 事件")
    frames = index.names.get(FRAME, ())
    frame_ids = {frame: identifier for identifier, frame in enumerate(frames)}
    records = [commit_record(index, commit, identifier, frame_ids)
               for identifier, commit in enumerate(commits)]
    by_frame = defaultdict(list)
    for record in records:
        by_frame[record["host_frame_id"]].append(record)
    frame_records = []
    for frame in frames:
        identifier = frame_ids[frame]
        children = by_frame[identifier]
        collision_ns = sum(record["collision_ns"] for record in children)
        frame_records.append({
            "id": identifier, "start_ns": frame.start, "end_ns": frame.end,
            "thread": frame.thread, "host_frame_ms": frame.duration / 1e6,
            "commit_ids": [record["id"] for record in children], "commit_count": len(children),
            "commit_ms": sum(record["commit_ms"] for record in children),
            "collision_ns": collision_ns, "collision_ms": collision_ns / 1e6,
            "rebuilt_coarse_chunks": sum(record["rebuilt_coarse_chunks"] for record in children),
            "full_32_chunk_rebuilds": sum(record["full_32_chunk_rebuild"] for record in children),
            "geometry_changed_commits": sum(record["geometry_group"] == "changed" for record in children),
            "geometry_no_change_commits": sum(record["geometry_group"] == "no_change" for record in children),
            "budget_complete": all(record["budget_complete"] for record in children),
            "over_0_5_ms": collision_ns > BUDGET_NS,
        })
    external = [event for event in index.names.get(PREPARE, ())
                if index.containing(event, COMMIT) is None]
    fragments = [event for event in external if index.containing(event, FRAGMENT) is not None]
    full = [record for record in records if record["full_32_chunk_rebuild"]]
    result = {
        "analysis_dir": str(analysis), "source_trace_sha256": summary.get("trace_sha256"),
        "window": window, "budget_ms": BUDGET_NS / 1e6,
        "budget_rule": "collision_ms < 0.5", "percentile_method": "linear_interpolation",
        "budget_components": [PREPARE, "Physics.set_mesh_parts or Physics.set_mesh", RELEASE],
        "budget_coverage": {
            "complete": all(record["budget_complete"] for record in records),
            "commits_with_release_previous_volume": sum(
                bool(record["release_previous_volume_calls"]) for record in records),
            "missing_release_previous_volume_commit_ids": [
                record["id"] for record in records if not record["release_previous_volume_calls"]],
            "missing_set_commit_ids": [record["id"] for record in records
                                       if not any(record["set_calls"].values())],
        },
        "input_rows": input_counts,
        "per_commit": records,
        "commit_summary": records_summary(records),
        "geometry_groups": {group: records_summary(
            [record for record in records if record["geometry_group"] == group])
            for group in ("changed", "no_change")},
        "rebuild_groups": {
            "full_32_chunks": records_summary(full),
            "other": records_summary([record for record in records if not record["full_32_chunk_rebuild"]]),
            "full_32_chunk_commit_ids": [record["id"] for record in full],
        },
        "per_host_frame": frame_records,
        "frame_summary": {
            "all_complete_frames_collision_ms": timing([record["collision_ns"] for record in frame_records]),
            "frames_with_commits_collision_ms": timing(
                [record["collision_ns"] for record in frame_records if record["commit_count"]]),
            "unframed_commit_ids": [record["id"] for record in by_frame[None]],
        },
        "external_prepare_collision_ms": {
            "all": timing([event.duration for event in external]),
            "inside_fragment_mesh": timing([event.duration for event in fragments]),
            "outside_fragment_mesh": timing([event.duration for event in external
                                             if index.containing(event, FRAGMENT) is None]),
        },
        "notes": [
            "只纳入 summary 窗口内完整 zone，按同线程父区间归属；plot 和空时间行不参加统计。",
            "每提交碰撞预算为 Terrain.commit 内 prepare_collision、set_mesh_parts/set_mesh 与 release_previous_volume 的 inclusive 耗时之和。",
            "release_previous_volume 包含旧几何最后引用释放，保守计入旧密度释放；缺埋点的旧 trace 不估算该项，budget_coverage 明示缺项。",
            "缺项提交的 collision_ms 仅是已测部分；低于预算时 meets_budget 为 null，已测部分超标时仍为 false。",
            "collision_compare 已包含在 prepare_collision 中；create_shape/create_mesh_compound 已包含在 set 中，均不重复相加。",
            "geometry_group 由 set 内 create_mesh_compound/create_shape 事件判断，是碰撞形状重建代理，不能证明三角形数据变化。",
            "rebuilt_coarse_chunks 统计 commit 内 Terrain.mesh_chunk 次数，不是细分碰撞 part 数；32 块全量重建仍纳入总体。",
            "外部 fragment_mesh 的 prepare_collision 单列，未计入地形提交预算。",
            "逐帧累计使用完整 Host.frame；无法归属完整帧的提交保留在总体并列出 id。",
            "P95 使用与 summarize_trace 相同的线性插值；不删除全量提交或其他慢样本。",
            "完整 Terrain.commit 耗时始终单列，包含 mesh_chunk 等其余工作；不将整个 commit 误称为碰撞成本。",
            "zone 为线程经过的墙钟时间，不是 CPU cycles；低于 0.5 ms 才满足单次提交最低预算。",
        ],
    }
    payload = json.dumps(result, ensure_ascii=False, indent=2) + "\n"
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("x", encoding="utf-8") as stream:
        stream.write(payload)
    return output


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--analysis-dir", type=Path, required=True,
                        help="summarize_trace.py 已生成的分析目录")
    parser.add_argument("--output", type=Path, required=True,
                        help="仓库 build/ 下尚不存在的 JSON 文件")
    args = parser.parse_args()
    try:
        print(analyze(args))
    except (OSError, ValueError, KeyError, TypeError) as error:
        print(f"碰撞分析失败：{error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
