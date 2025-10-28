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
from dataclasses import dataclass, field
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
    bpm_sum: float = 0.0
    bpm_values: list[float] = field(default_factory=list)


def read_session_csv(path: Path, expected_interval_ms: float) -> SessionStats:
    stats = SessionStats(bpm_values=[])
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
            bpm_value = float(row[1])
            _ = row[3]

            if stats.rows == 0:
                stats.first_ts = timestamp
            stats.last_ts = timestamp
            stats.rows += 1
            stats.bpm_sum += bpm_value
            stats.bpm_values.append(bpm_value)

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


def validate_calibration_json(path: Path, gain_tolerance_db: float, delay_tolerance_samples: int) -> None:
    if not path.exists():
        raise SystemExit(f"Calibration JSON not found: {path}")

    with path.open("r", encoding="utf-8") as handle:
        data = json.load(handle)

    channels = data.get("channels", [])
    if len(channels) != 2:
        raise ValueError(f"Calibration JSON must contain 2 channels, found {len(channels)}")

    for index, channel in enumerate(channels, start=1):
        name = channel.get("name", f"CH{index}")
        gain = float(channel.get("gain", 0.0))
        if gain <= 0.0:
            raise ValueError(f"Calibration gain for {name} must be positive (value={gain})")
        gain_db = 20.0 * math.log10(gain)
        if abs(gain_db) > gain_tolerance_db:
            raise ValueError(
                f"Calibration gain for {name} out of tolerance: {gain_db:.2f} dB (limit ±{gain_tolerance_db} dB)"
            )

        delay = int(channel.get("delaySamples", 0))
        if abs(delay) > delay_tolerance_samples:
            raise ValueError(
                f"Calibration delay for {name} out of tolerance: {delay} samples (limit ±{delay_tolerance_samples})"
            )


def validate_calibration_report(path: Path, min_ratio: float) -> dict:
    if not path.exists():
        raise SystemExit(f"Calibration report not found: {path}")

    last_row: Optional[list[str]] = None
    with path.open("r", newline="") as handle:
        reader = csv.reader(handle)
        header = next(reader, None)
        if header is None:
            raise ValueError("Calibration report is empty")
        for row in reader:
            if row:
                last_row = row

    if last_row is None:
        raise ValueError("Calibration report has no data rows")

    if len(last_row) < 19:
        raise ValueError(f"Calibration report row has unexpected column count: {len(last_row)}")

    def parse_float(value: str) -> float:
        try:
            return float(value)
        except (TypeError, ValueError):
            return math.nan

    mean = parse_float(last_row[15])
    peak = parse_float(last_row[16])
    ratio = parse_float(last_row[17])
    spec = last_row[18]

    if spec != "NA":
        if math.isnan(ratio):
            raise ValueError("Envelope ratio in calibration report is not a number")
        if ratio < min_ratio:
            raise ValueError(
                f"Envelope ratio {ratio:.2f} is below minimum threshold {min_ratio:.2f}."
            )

    return {
        "mean": mean,
        "peak": peak,
        "ratio": ratio,
        "spec": spec,
    }


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description="Validate KNOT proto session logs.")
    parser.add_argument("--session", type=Path, required=True, help="Path to proto_session.csv")
    parser.add_argument("--summary", type=Path, required=True, help="Path to proto_summary.json")
    parser.add_argument("--haptic", type=Path, help="Optional haptic_events.csv path")
    parser.add_argument("--interval-ms", type=float, default=250.0, help="Expected session logging interval (ms)")
    parser.add_argument(
        "--expected-bpm",
        type=float,
        help="Reference BPM for accuracy check (fails if |avg - reference| > 3 BPM)",
    )
    parser.add_argument(
        "--calibration",
        type=Path,
        help="Calibration JSON to validate (gain ±3 dB / delay ±2 samples by default)",
    )
    parser.add_argument(
        "--calibration-gain-tolerance",
        type=float,
        default=3.0,
        help="Calibration gain tolerance in dB",
    )
    parser.add_argument(
        "--calibration-delay-tolerance",
        type=int,
        default=2,
        help="Calibration delay tolerance in samples",
    )
    parser.add_argument(
        "--calibration-report",
        type=Path,
        help="Optional calibration_report.csv for envelope baseline validation",
    )
    parser.add_argument(
        "--baseline-ratio-min",
        type=float,
        default=1.15,
        help="Minimum acceptable envelope ratio (peak/mean)",
    )
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

    if stats.rows > 0:
        avg_bpm = stats.bpm_sum / stats.rows
        print(f"  average BPM (session): {avg_bpm:.2f}")
        if args.expected_bpm is not None:
            bpm_error = abs(avg_bpm - args.expected_bpm)
            print(f"  BPM error vs expected ({args.expected_bpm}): {bpm_error:.2f} BPM")
            if bpm_error > 3.0:
                raise ValueError(
                    f"Average BPM {avg_bpm:.2f} differs from expected {args.expected_bpm:.2f} by {bpm_error:.2f} BPM"
                )

    print("\nSummary JSON:")
    print(json.dumps(summary, indent=2))

    if stats.rows != summary.get("sampleCount"):
        print(
            f"WARNING: session rows ({stats.rows}) != summary sampleCount ({summary.get('sampleCount')})",
            file=sys.stderr,
        )

    if args.calibration:
        validate_calibration_json(
            args.calibration,
            gain_tolerance_db=args.calibration_gain_tolerance,
            delay_tolerance_samples=args.calibration_delay_tolerance,
        )
        print(
            f"\nCalibration JSON '{args.calibration}' passed (gain ±{args.calibration_gain_tolerance} dB, "
            f"delay ±{args.calibration_delay_tolerance} samples)"
        )

    if args.calibration_report:
        report = validate_calibration_report(args.calibration_report, args.baseline_ratio_min)
        print(
            f"\nCalibration report '{args.calibration_report}' — envelopeMean={report['mean']:.4f} "
            f"envelopePeak={report['peak']:.4f} ratio={report['ratio']:.3f} spec={report['spec']}"
        )

    if args.haptic:
        if not args.haptic.exists():
            raise SystemExit(f"Haptic log not found: {args.haptic}")
        haptic_rows = validate_haptic_csv(args.haptic)
        print(f"\nHaptic log rows: {haptic_rows}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
