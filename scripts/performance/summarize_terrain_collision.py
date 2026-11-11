#!/usr/bin/env python3
"""Summarize terrain collision submissions in an existing Tracy window, separating synchronous cost from asynchronous publication."""
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
PUBLISH = "Terrain.publish"
ASYNC_PREPARE = "Terrain.async_prepare"
FRAME = "Host.frame"
PREPARE = "Terrain.prepare_collision"
RELEASE = "Terrain.release_previous_volume"
SET_NAMES = ("Physics.set_mesh_parts", "Physics.set_mesh")
SHAPE_NAMES = ("Physics.create_mesh_compound", "Physics.create_shape")
CHUNK = "Terrain.mesh_chunk"
FRAGMENT = "Terrain.fragment_mesh"
COLLAPSE = "Terrain.collapse"
SELECTED_NAMES = {COMMIT, PUBLISH, ASYNC_PREPARE, FRAME, PREPARE, RELEASE, CHUNK, FRAGMENT, COLLAPSE,
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
    """Keep relevant zones and search intervals by thread/name without rescanning the raw TSV for each submission."""

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
            if key[1] in (COMMIT, PUBLISH, FRAME, FRAGMENT, COLLAPSE):
                if any(left.end > right.start for left, right in zip(group, group[1:])):
                    raise ValueError(f"Same-thread parent intervals overlap and cannot be attributed uniquely: {key[1]}")
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
            raise ValueError("zones.tsv is missing name/thread/ns_since_start/exec_time_ns columns")
        for row in reader:
            counts["rows"] += 1
            if None in row or any(value is None for value in row.values()):
                raise ValueError(f"zones.tsv row {reader.line_num} contains an unescaped delimiter or newline")
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
                raise ValueError(f"zones.tsv row {reader.line_num} has a noninteger timestamp") from error
            if duration < 0 or timestamp < start or timestamp + duration > end:
                counts["excluded_relevant_zone_rows"] += 1
                continue
            if not row["thread"]:
                raise ValueError(f"zones.tsv row {reader.line_num} contains a zone without a thread")
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
        raise ValueError("Budgeted prepare_collision/set/release intervals overlap; refusing to count them twice")
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
    # 独立 publish 不包含 worker 准备；没有请求 ID，不能按时间猜测跨线程配对。
    preparation_unattributed = commit.name == PUBLISH
    budget_complete = bool(releases) and bool(submissions) and not preparation_unattributed
    coarse_chunks = len(index.children(commit, CHUNK))
    frame = index.containing(commit, FRAME)
    return {
        "id": identifier, "start_ns": commit.start, "end_ns": commit.end,
        "thread": commit.thread, "commit_ms": commit.duration / 1e6,
        "scope": commit.name, "preparation_unattributed": preparation_unattributed,
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


def submission_events(index: ZoneIndex) -> list[Event]:
    # 同步 commit 已包含 publish，保留其完整准备成本；只补充无 commit 父级的发布。
    events = list(index.names.get(COMMIT, ()))
    events.extend(event for event in index.names.get(PUBLISH, ())
                  if index.containing(event, COMMIT) is None)
    return sorted(events, key=lambda event: (event.start, event.thread))


def analyze(args) -> Path:
    requested = args.output
    if requested.exists() or requested.is_symlink():
        raise ValueError("Output file exists; select a new file to preserve earlier analysis")
    output = requested.resolve()
    if not output.is_relative_to((ROOT / "build").resolve()):
        raise ValueError("Output file must be under the repository build/ directory")
    analysis = args.analysis_dir.resolve(strict=True)
    if not analysis.is_dir():
        raise ValueError("analysis-dir must contain summary.json and zones.tsv")
    summary = json.loads((analysis / "summary.json").read_text(encoding="utf-8"))
    window = summary["window"]
    start, end = window["start_ns"], window["end_ns"]
    if type(start) is not int or type(end) is not int or start < 0 or end <= start:
        raise ValueError("summary.json window must use integers with 0 <= start_ns < end_ns")
    events, input_counts = read_events(analysis / "zones.tsv", start, end)
    index = ZoneIndex(events)
    commits = submission_events(index)
    if not commits:
        raise ValueError("summary.json window has no complete Terrain.commit or Terrain.publish event")
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
                if index.containing(event, COMMIT) is None
                and index.containing(event, PUBLISH) is None]
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
            "unattributed_preparation_commit_ids": [record["id"] for record in records
                                                   if record["preparation_unattributed"]],
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
        "async_prepare_ms": timing([event.duration
                                    for event in index.names.get(ASYNC_PREPARE, ())]),
        "notes": [
            "Include only complete zones within the summary window, attributed to same-thread parents; exclude plots and rows without timestamps.",
            "Sum inclusive prepare_collision, set_mesh_parts/set_mesh and release_previous_volume time per submission; count publish inside synchronous commit only once.",
            "Standalone Terrain.publish covers only main-thread publication. Report worker preparation separately; do not infer pairing without request markers or treat missing preparation as zero.",
            "release_previous_volume includes final old-geometry references and is conservatively counted as old-density release. Do not estimate it for older traces without markers; budget_coverage reports omissions.",
            "collision_ms for incomplete submissions includes only measured work; meets_budget is null when below budget and false when the measured portion already exceeds it.",
            "collision_compare is included in prepare_collision; create_shape/create_mesh_compound are included in set. Do not add them again.",
            "geometry_group uses create_mesh_compound/create_shape events inside set as a proxy for collision shape rebuilding, not proof of changed triangle data.",
            "rebuilt_coarse_chunks counts Terrain.mesh_chunk events inside commit, not subdivided collision parts; full 32-chunk rebuilds remain included.",
            "For standalone publish, rebuilt_coarse_chunks includes only publication-thread events; zero does not imply no worker rebuild. See preparation_unattributed.",
            "External fragment_mesh prepare_collision is listed separately and excluded from the terrain submission budget.",
            "Per-frame totals use complete Host.frame intervals; submissions outside complete frames remain in overall totals with their IDs listed.",
            "P95 uses the same linear interpolation as summarize_trace; full submissions and slow samples are not removed.",
            "commit_ms records actual scope wall time. Terrain.commit includes preparation while standalone Terrain.publish covers only publication, so they are not directly comparable speedups.",
            "Zones measure elapsed thread wall time, not CPU cycles; a submission must remain below 0.5 ms to meet the minimum budget.",
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
                        help="Analysis directory produced by summarize_trace.py")
    parser.add_argument("--output", type=Path, required=True,
                        help="New JSON file under the repository build/ directory")
    args = parser.parse_args()
    try:
        print(analyze(args))
    except (OSError, ValueError, KeyError, TypeError) as error:
        print(f"Collision analysis failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
