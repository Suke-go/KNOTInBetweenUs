#!/usr/bin/env python3
"""
Generate calibration and demo audio assets at 48 kHz / 24-bit PCM.

Outputs:
  - tone_1kHz_-12dBFS_5s.wav
  - rect_pulse_512samples_-18dBFS.wav
  - heartbeat_demo.wav
"""

import argparse
import math
import os
import wave
from typing import Iterable


SAMPLE_RATE = 48_000
MAX_24BIT = (1 << 23) - 1  # 0x7FFFFF
MIN_24BIT = -(1 << 23)


def float_to_24bit_pcm(sample: float) -> bytes:
    """Clamp float sample [-1, 1] to 24-bit little-endian PCM bytes."""
    clamped = max(-1.0, min(1.0, sample))
    scaled = int(round(clamped * MAX_24BIT))
    if scaled > MAX_24BIT:
        scaled = MAX_24BIT
    if scaled < MIN_24BIT:
        scaled = MIN_24BIT
    if scaled < 0:
        scaled += 1 << 24  # two's complement for negative values
    return bytes((scaled & 0xFF, (scaled >> 8) & 0xFF, (scaled >> 16) & 0xFF))


def write_wave24(path: str, samples: Iterable[float]) -> None:
    """Write mono iterable of float samples to 24-bit WAV file."""
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with wave.open(path, "wb") as wav:
        wav.setnchannels(1)
        wav.setsampwidth(3)  # 24-bit PCM
        wav.setframerate(SAMPLE_RATE)
        frames = bytearray()
        for sample in samples:
            frames.extend(float_to_24bit_pcm(sample))
        wav.writeframes(frames)


def generate_sine(duration_sec: float, frequency_hz: float, level_dbfs: float) -> list[float]:
    """Generate mono sine wave samples."""
    amplitude = math.pow(10.0, level_dbfs / 20.0)
    total_samples = int(duration_sec * SAMPLE_RATE)
    return [
        amplitude * math.sin(2.0 * math.pi * frequency_hz * n / SAMPLE_RATE)
        for n in range(total_samples)
    ]


def generate_rect_pulse(num_samples: int, level_dbfs: float) -> list[float]:
    """Generate rectangular pulse (single on-block)."""
    amplitude = math.pow(10.0, level_dbfs / 20.0)
    return [amplitude if n < num_samples else 0.0 for n in range(num_samples)]


def generate_heartbeat(duration_sec: float = 10.0, bpm: float = 60.0) -> list[float]:
    """Synthesize simple dual-peak heartbeat pattern."""
    total_samples = int(duration_sec * SAMPLE_RATE)
    samples = [0.0] * total_samples
    interval_samples = int((60.0 / bpm) * SAMPLE_RATE)
    primary_amp = math.pow(10.0, -14.0 / 20.0)
    secondary_amp = primary_amp * 0.6
    decay = 0.003

    for beat_start in range(0, total_samples, interval_samples):
        if beat_start >= total_samples:
            break
        for idx, amp in ((0, primary_amp), (int(0.07 * SAMPLE_RATE), secondary_amp)):
            hit = beat_start + idx
            if hit >= total_samples:
                continue
            for n in range(int(0.04 * SAMPLE_RATE)):
                t = hit + n
                if t >= total_samples:
                    break
                env = math.exp(-n / (decay * SAMPLE_RATE))
                samples[t] += amp * env * math.sin(2.0 * math.pi * 80.0 * n / SAMPLE_RATE)
    # Normalize to stay within -12 dBFS
    peak = max(abs(s) for s in samples) or 1.0
    scale = (math.pow(10.0, -12.0 / 20.0)) / peak
    return [s * scale for s in samples]


def main() -> None:
    parser = argparse.ArgumentParser(description="Generate calibration WAV files.")
    parser.add_argument(
        "-o",
        "--output",
        default="data/test_signals",
        help="Output directory (default: data/test_signals)",
    )
    args = parser.parse_args()

    tone_path = os.path.join(args.output, "tone_1kHz_-12dBFS_5s.wav")
    pulse_path = os.path.join(args.output, "rect_pulse_512samples_-18dBFS.wav")
    heartbeat_path = os.path.join(args.output, "heartbeat_demo.wav")

    write_wave24(tone_path, generate_sine(5.0, 1000.0, -12.0))
    write_wave24(pulse_path, generate_rect_pulse(512, -18.0))
    write_wave24(heartbeat_path, generate_heartbeat())

    print(f"Generated test signals in {args.output}")


if __name__ == "__main__":
    main()

