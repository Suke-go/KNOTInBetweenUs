# HRTF Path Risk Assessment

## Summary
The current implementation prioritizes libmysofa-based HRTF rendering but automatically falls back to simplified spatialization when HRIR data cannot be served. The risks below highlight where binaural playback can silently degrade or introduce runtime instability.

## Risk Register
- **Dependency availability (High probability / Medium impact)**  
  The HRTF path relies on libmysofa being present at build and runtime. If the library is absent, all processors mark themselves as not loaded and the router logs a warning before using the simplified spatial mix for every source. This preserves audio output but eliminates HRTF cues.  
  _Mitigation:_ Package or vendor libmysofa with the target runtime; add CI checks to detect missing headers/libraries early.

- **SOFA asset discoverability (Medium probability / Medium impact)**  
  Each processor attempts to load `hrir/mit_kemar_normal_pinna.sofa` at startup. If the asset is missing or unreadable, `useHrtf_` is disabled globally and processing stays on the fallback path.  
  _Mitigation:_ Bundle the SOFA file with the application payload and verify readability on launch.

- **Per-listener load divergence (Low probability / Medium impact)**  
  `useHrtf_` becomes true if at least one processor loads successfully. Later, any processor whose filter load fails flips `hrtfLoaded_` to false and silently reverts to simplified spatialization while other listeners may keep HRTF active. This can create inconsistent imaging between participants.  
  _Mitigation:_ Track load failures per listener and surface them in logs/telemetry; optionally disable the global HRTF flag if any processor drops out.

- **Runtime handle reopening (Low probability / Low impact)**  
  If the mysofa handle is lost after initial load, the processor reopens it on the next update. Repeated reopen attempts during audio processing can introduce glitches and increased CPU usage.  
  _Mitigation:_ Guard reopen attempts with throttling and move handle recovery off the audio thread.

- **Filter length and delay bounds (Low probability / High impact)**  
  HRIRs longer than 1024 samples or with unusual delay values cause the processor to mark HRTF as unavailable, dropping to the fallback path. This protects against buffer overruns but can disable HRTF on unexpected datasets.  
  _Mitigation:_ Size history buffers dynamically from the returned length/delay and log out-of-range metadata for triage.

- **Per-sample filter selection cost (Medium probability / Low impact)**  
  The filter lookup and convolution run inside `processSpatial`, which is called for every sample. Direction or distance changes trigger HRIR reloads and history resizing mid-stream. On constrained hardware, this may increase CPU load or cause XR audio underruns.  
  _Mitigation:_ Cache filter selection at block boundaries, prefetch HRIRs for expected trajectories, or move direction changes to a control-rate thread.

## Residual Risk
When libmysofa and the MIT KEMAR asset are available, the main residual risks are CPU overhead from per-sample filter management and inconsistent HRTF availability across listeners if individual processors fail mid-session. Both scenarios degrade quality rather than causing crashes, but they warrant monitoring in profiling and logging.
