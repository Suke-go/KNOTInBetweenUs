# Noise Reduction Implementation Plan

**Status:** Phase 1 (side-chain noise gate) is implemented using CH3 as the reference microphone. The gate attenuates both heartbeat channels when the CH3 RMS level exceeds the configured threshold, with a smoothed gain to avoid clicks. Phase 2 (spectral subtraction) is now available behind the `SpecSub` mode; it performs Hamming-windowed FFTs over each buffer (CH1 vs. CH3), subtracts a smoothed magnitude estimate scaled by `alpha`, floors the result to `floor`, and reconstructs CH1 before beat detection. If CH3 or FFT initialization is unavailable, the buffer falls back to passthrough and surfaces a warning.

This note evaluates the feasibility of the two-step noise control proposal (gate first, spectral subtraction second) and outlines how to add it with minimal disruption to the current audio pipeline.

## Feasibility overview
- **Phase 1 (Noise gate via side-chain on CH3):** Straightforward with current buffers. The pipeline already separates CH1/CH2/CH3 inside `AudioPipeline::audioIn`, so we can compute an RMS for CH3 per buffer and attenuate CH1/CH2 before they enter beat detection. No additional dependencies are required.
- **Phase 2 (Spectral subtraction using FFT):** Technically possible, but requires introducing an FFT library (e.g., `ofxFft`). This will add latency equal to the FFT hop/buffer size and introduce more state. It is optional and can be guarded behind a mode flag to preserve the existing path.

## Minimal-change implementation outline

### Phase 1: Noise gate (implemented)
1. **Parameters** (GUI/Config): gate threshold (0–1 normalized), attenuation factor (e.g., 0 for full mute or 0.1 for heavy ducking), and a mode selector (`Raw / Gate`). Defaults keep the gate disabled to preserve raw behavior.
2. **Processing** inside `AudioPipeline::audioIn`:
   - Accumulate CH3 (noise mic) RMS per buffer.
   - Smooth the RMS-driven gain and attenuate both heartbeat channels when RMS exceeds the threshold.
   - Leave CH3 unused for output; it is only a reference and never sent to audioOut.
3. **Safety:** The gate operates entirely on the input path; output still mixes only the processed CH1/CH2 heartbeats and pink noise. `audioOut` continues to ignore the noise channel so the “never pass CH3 through” rule is preserved.

### Phase 2: Spectral subtraction (implementation plan)
Goal: Improve SNR beyond the gate by subtracting a spectral estimate of ambient noise (CH3) from the heartbeat mic (CH1). The design keeps the current pipeline intact by enabling this path only when explicitly selected.

1. **Modes and parameters**
   - Extend `NoiseMode` to include `SpecSub`. Keep `Raw` and `Gate` unchanged for backward compatibility.
   - Reuse existing GUI/config entries for threshold/attenuation and add spectral parameters: `SS Alpha` (0–5, default 1.5), `SS Floor` (0–0.1, default 0.01), and an optional `Smoothing` factor (0–1) for temporal averaging of noise magnitude.
   - Persist new parameters alongside existing noise settings to avoid surprises on restart.

2. **Dependencies and setup**
   - Add `ofxFft` (or a similarly light FFT helper) as a project dependency.
   - Initialize two FFT instances sized to the current buffer (512/1024) for CH1 and CH3. Allocate reusable buffers for time-domain input/output, magnitude, and phase to avoid per-buffer allocations.
   - Provide an initialization guard so the app continues to run in `Raw`/`Gate` modes if FFT setup fails (no hard dependency for existing paths).

3. **Processing path inside `AudioPipeline::audioIn` when mode = SpecSub**
   - **Preconditioning:** After calibration/gain and notch filters, copy CH1/CH3 into FFT input buffers. Optionally apply a mild window (e.g., Hann) to reduce spectral leakage.
   - **Forward FFT:** Compute complex spectra for CH1 (signal) and CH3 (noise reference).
   - **Magnitude estimation:**
     - Compute magnitudes `|S|` and `|N|`. If smoothing is enabled, update a running average of `|N|` with `N_smoothed = (1 - smooth) * N_smoothed + smooth * |N|`.
   - **Subtraction per bin:**
     - `mag_out = max(|S| - alpha * N_estimate, floor)` where `N_estimate` is `|N|` or the smoothed version.
     - Retain CH1 phase for reconstruction.
   - **Inverse FFT:** Rebuild the time-domain CH1 buffer with the modified magnitudes and original phases. Normalize by FFT size and replace `channelBuffers_[0]` with the reconstructed signal. Leave CH2 untouched.
   - **Safety rails:**
     - Clamp `alpha` and `floor` to configured ranges to prevent negative magnitudes.
     - If FFT overruns or produces NaN/inf, fall back to passthrough for that buffer and emit a telemetry warning rather than crashing audio.

4. **Integration points (minimal disruption)**
   - **Mode switch:** Insert a simple branch in `audioIn` after the existing gate. `Raw/Gate` keep the current path; `SpecSub` runs the FFT block and writes the modified CH1 buffer before beat detection.
   - **Telemetry:** Extend `SignalHealth` with an optional flag `specSubActive` and the current `noiseRms`/`alpha` used, to surface state in logging without altering downstream consumers.
   - **GUI/config:** Add the new mode and parameters to the existing noise GUI panel and config load/save logic. Default to `Gate` so current deployments behave the same until operators opt-in.

5. **Testing and tuning plan**
   - **Unit-style checks:** Verify FFT init on both 48k/44.1k and buffer sizes 512/1024. Add debug asserts that reconstructed buffers stay finite.
   - **Listening tests:**
     - Quiet room: confirm `Raw` matches current behavior.
     - Steady ambient noise on CH3: enable `SpecSub`, sweep `alpha` (1.0–2.0) and confirm SNR improvement without musical noise; adjust `floor` to suppress chirping.
   - **Latency sanity:** Measure one-buffer latency increase (~10–20ms) and confirm visualization remains in sync.
   - **Failure drills:** Unplug CH3 while in `SpecSub` to confirm graceful fallback to passthrough and clear operator warning.

## Impact on existing code
- **Touch points:** `AudioPipeline::audioIn` for the mode branch and FFT invocation; noise GUI/config plumbing for new parameters; optional telemetry flag. No changes are required to calibration or beat detection logic beyond consuming the cleaned CH1 buffer.
- **State additions:** FFT plans/buffers, spectral parameters, and a mode enum value. Gate-related state remains unchanged.
- **Testing path:** Follow the plan above—phase in with listening tests and guardrails before enabling in production.

## Recommendation
Keep Phase 1 as the default for safety. Land Phase 2 behind the explicit `SpecSub` mode, with robust fallbacks, so existing setups remain stable while enabling higher SNR when operators are ready to tune it.
