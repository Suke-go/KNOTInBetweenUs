#!/usr/bin/env python3
import math
import sys
import wave


def generate_bell(output_path: str, duration: float = 2.0, sample_rate: int = 48_000) -> None:
    """Generate a simple exponentially decaying 1 kHz sine as a placeholder bell sound."""
    try:
        import numpy as np
    except ImportError as exc:  # pragma: no cover - numpy should exist, but fail gracefully
        raise SystemExit("numpy is required to run generate_bell.py") from exc

    total_samples = int(sample_rate * duration)
    time = np.linspace(0.0, duration, total_samples, endpoint=False)
    envelope = np.exp(-time * 2.0)
    amplitude = 10 ** (-12.0 / 20.0)  # -12 dBFS ≈ 0.25
    signal = np.sin(2.0 * math.pi * 1_000.0 * time) * envelope * amplitude

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
