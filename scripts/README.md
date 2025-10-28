# Scripts

- `generate_test_signals.py`: Produce calibration/test WAV files at 48 kHz / 24-bit PCM. Run from repo root with `python3 scripts/generate_test_signals.py`.
- `validate_logs.py`: Check `logs/proto_session.csv`, `logs/proto_summary.json`, and `logs/haptic_events.csv` for structural consistency. Typical usage:  
  `python3 scripts/validate_logs.py --session logs/proto_session.csv --summary logs/proto_summary.json --haptic logs/haptic_events.csv`.

Add new tooling under this directory and document invocation alongside assumptions (dependencies, inputs, outputs).
