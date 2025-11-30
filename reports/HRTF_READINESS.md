# HRTF Rendering Readiness Assessment

## Assets
- MIT KEMAR SOFA file is present at `bin/data/hrir/mit_kemar_normal_pinna.sofa` but it is not referenced by the runtime code.

## Code Path Review
- `HrtfProcessor::loadHrtfData` is stubbed out and always returns false, so no HRIR data are parsed or retained even if the SOFA asset exists.
- `HrtfProcessor::processBlock` and `processSpatial` fall back to the simplified distance attenuation, low-pass filtering, and constant-power panning rather than true HRIR convolution.
- `AudioRouter::setup` constructs per-listener `HrtfProcessor` instances but hardcodes `useHrtf_ = false`, meaning the HRTF path is never executed at runtime.
- `AudioRouter::applySpatialAudio` checks `useHrtf_` and `isLoaded()` and therefore always routes into the simplified spatialization branch.

## Conclusion
With the current code, binaural/HRTF playback cannot be enabled even though an MIT KEMAR SOFA file exists. Implementing SOFA parsing (e.g., via libmysofa or an existing FIR loader), wiring `useHrtf_` to a configuration flag, and replacing the simplified spatial branch with HRIR convolution are required before a true HRTF experience can be constructed.
