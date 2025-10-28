# Logging Output

- `proto_session.csv`: 250ms interval telemetry (timestamp, bpm, envelopePeak, sceneId).
- `proto_summary.json`: aggregate statistics for a session (avgBpm, sdnn, rmssd, durationSec).
- `haptic_events.csv`: heartbeat-triggered haptic payloads.

Log writers should prefer append mode and flush buffers at least once per second to avoid data loss on crashes.

