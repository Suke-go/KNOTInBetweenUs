#!/usr/bin/env python3
import math
import sys
import wave


def generate_bell(output_path: str, duration: float = 4.0, sample_rate: int = 48_000) -> None:
    """Generate a hang drum / handpan-like bell sound with soft attack and rich harmonics."""
    try:
        import numpy as np
    except ImportError as exc:  # pragma: no cover - numpy should exist, but fail gracefully
        raise SystemExit("numpy is required to run generate_bell.py") from exc

    total_samples = int(sample_rate * duration)
    time = np.linspace(0.0, duration, total_samples, endpoint=False)

    # Soft attack envelope (100ms rise) + slow decay
    attack_time = 0.1
    attack_samples = int(sample_rate * attack_time)
    attack_env = np.linspace(0.0, 1.0, attack_samples)
    decay_env = np.exp(-time[attack_samples:] * 0.8)
    envelope = np.concatenate([attack_env, decay_env[:total_samples - attack_samples]])

    # Hang drum-like harmonic series (fundamental + overtones)
    fundamental = 220.0  # A3
    signal = np.zeros_like(time)

    # Fundamental with slow decay
    signal += np.sin(2.0 * math.pi * fundamental * time) * envelope * 0.5

    # 2nd harmonic (octave) - prominent in hang drums
    signal += np.sin(2.0 * math.pi * fundamental * 2.0 * time) * envelope * 0.25

    # Minor 3rd overtone (characteristic of hang drums)
    signal += np.sin(2.0 * math.pi * fundamental * 2.4 * time) * envelope * np.exp(-time * 1.2) * 0.15

    # Perfect 5th overtone
    signal += np.sin(2.0 * math.pi * fundamental * 3.0 * time) * envelope * np.exp(-time * 1.5) * 0.12

    # High shimmer (fast decay)
    signal += np.sin(2.0 * math.pi * fundamental * 5.0 * time) * envelope * np.exp(-time * 3.0) * 0.08

    # Subtle low rumble for warmth
    signal += np.sin(2.0 * math.pi * fundamental * 0.5 * time) * envelope * np.exp(-time * 0.5) * 0.1

    # Normalize and apply gentle compression
    signal = signal / np.max(np.abs(signal)) * 0.6
    amplitude = 10 ** (-6.0 / 20.0)  # -6 dBFS for comfortable listening
    signal *= amplitude

    pcm = (signal * 32767.0).astype(np.int16)

    with wave.open(output_path, "wb") as wav:
        wav.setnchannels(1)
        wav.setsampwidth(2)
        wav.setframerate(sample_rate)
        wav.writeframes(pcm.tobytes())

    print(f"✅ Generated dummy bell: {output_path}")


def main() -> None:
    if len(sys.argv) != 2:
        print("Usage: generate_bell.py <output.wav>")
        raise SystemExit(1)
    generate_bell(sys.argv[1])


if __name__ == "__main__":
    main()
