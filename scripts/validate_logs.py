#!/usr/bin/env python3
"""
Validate proto session logs for structural consistency.

Usage:
    python3 scripts/validate_logs.py \
        --session logs/proto_session.csv \
        --summary logs/proto_summary.json \
        --haptic logs/haptic_events.csv
"""

from __future__ import annotations

import argparse
import csv
import json
import math
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Optional

EXPECTED_HEADER = ["timestampMicros", "bpm", "envelopePeak", "sceneId"]
EXPECTED_HAPTIC_HEADER = ["timestampMicros", "label", "intensity"]


@dataclass
class SessionStats:
    rows: int = 0
    first_ts: Optional[int] = None
    last_ts: Optional[int] = None
    min_interval_us: Optional[int] = None
    max_interval_us: Optional[int] = None
    gaps_exceeding_ms: int = 0


def read_session_csv(path: Path, expected_interval_ms: float) -> SessionStats:
    stats = SessionStats()
    previous_ts: Optional[int] = None
    with path.open("r", newline="") as handle:
        reader = csv.reader(handle)
        header = next(reader, None)
        if header != EXPECTED_HEADER:
            raise ValueError(f"Unexpected session header {header!r}. Expected {EXPECTED_HEADER!r}")

        for row_index, row in enumerate(reader, start=2):
            if len(row) != 4:
                raise ValueError(f"Row {row_index} in session CSV has {len(row)} columns (expected 4)")
            timestamp = int(row[0])
            _ = float(row[1])
            _ = float(row[2])
            _ = row[3]

            if stats.rows == 0:
                stats.first_ts = timestamp
            stats.last_ts = timestamp
            stats.rows += 1

            if previous_ts is not None:
                delta = timestamp - previous_ts
                stats.min_interval_us = delta if stats.min_interval_us is None else min(stats.min_interval_us, delta)
                stats.max_interval_us = delta if stats.max_interval_us is None else max(stats.max_interval_us, delta)

                if expected_interval_ms > 0.0:
                    if delta > (expected_interval_ms * 1000.0 * 2.5):
                        stats.gaps_exceeding_ms += 1
                    if delta <= 0:
                        raise ValueError(f"Non-increasing timestamp at row {row_index}: {timestamp} <= {previous_ts}")
            previous_ts = timestamp

    return stats


def read_summary_json(path: Path) -> dict:
    with path.open("r", encoding="utf-8") as handle:
        data = json.load(handle)
    if "sampleCount" not in data or not isinstance(data["sampleCount"], int):
        raise ValueError("summary JSON missing integer field 'sampleCount'")
    for key in ("avgBpm", "sdnnMs", "rmssdMs", "durationSec"):
        if key not in data:
            raise ValueError(f"summary JSON missing '{key}'")
        if not isinstance(data[key], (int, float)) or math.isnan(data[key]):
            raise ValueError(f"summary JSON field '{key}' must be finite number")
    return data


def validate_haptic_csv(path: Path) -> int:
    count = 0
    with path.open("r", newline="") as handle:
        reader = csv.reader(handle)
        header = next(reader, None)
        if header != EXPECTED_HAPTIC_HEADER:
            raise ValueError(f"Unexpected haptic header {header!r}. Expected {EXPECTED_HAPTIC_HEADER!r}")
        for row_index, row in enumerate(reader, start=2):
            if len(row) != 3:
                raise ValueError(f"Haptic row {row_index} has {len(row)} columns (expected 3)")
            _ = int(row[0])
            _ = str(row[1])
            _ = float(row[2])
            count += 1
    return count


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description="Validate KNOT proto session logs.")
    parser.add_argument("--session", type=Path, required=True, help="Path to proto_session.csv")
    parser.add_argument("--summary", type=Path, required=True, help="Path to proto_summary.json")
    parser.add_argument("--haptic", type=Path, help="Optional haptic_events.csv path")
    parser.add_argument("--interval-ms", type=float, default=250.0, help="Expected session logging interval (ms)")
    args = parser.parse_args(argv)

    if not args.session.exists():
        raise SystemExit(f"Session CSV not found: {args.session}")
    if not args.summary.exists():
        raise SystemExit(f"Summary JSON not found: {args.summary}")

    stats = read_session_csv(args.session, args.interval_ms)
    summary = read_summary_json(args.summary)

    print("Session CSV:")
    print(f"  rows: {stats.rows}")
    print(f"  first timestamp: {stats.first_ts}")
    print(f"  last timestamp: {stats.last_ts}")
    if stats.min_interval_us is not None:
        print(f"  min interval (ms): {stats.min_interval_us / 1000.0:.2f}")
    if stats.max_interval_us is not None:
        print(f"  max interval (ms): {stats.max_interval_us / 1000.0:.2f}")
    if stats.gaps_exceeding_ms:
        print(f"  gaps > {args.interval_ms * 2.5:.0f} ms: {stats.gaps_exceeding_ms}")

    print("\nSummary JSON:")
    print(json.dumps(summary, indent=2))

    if stats.rows != summary.get("sampleCount"):
        print(
            f"WARNING: session rows ({stats.rows}) != summary sampleCount ({summary.get('sampleCount')})",
            file=sys.stderr,
        )

    if args.haptic:
        if not args.haptic.exists():
            raise SystemExit(f"Haptic log not found: {args.haptic}")
        haptic_rows = validate_haptic_csv(args.haptic)
        print(f"\nHaptic log rows: {haptic_rows}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
