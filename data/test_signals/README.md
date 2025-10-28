# Test Signal Assets

The setup script can generate the following WAV files for calibration and validation:

- `tone_1kHz_-12dBFS_5s.wav`: 5 second 1 kHz sine tone at -12 dBFS for gain calibration.
- `rect_pulse_512samples_-18dBFS.wav`: 512-sample rectangular pulse at -18 dBFS for delay estimation.
- `heartbeat_demo.wav`: Demo heartbeat recording (synthetic) for pipeline validation.

Generated files are stored in 48 kHz / 24-bit PCM WAV format to match the wireless microphone transport characteristics.
