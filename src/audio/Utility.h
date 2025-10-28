#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace knot::audio {

inline float dbToLinear(float db) {
    return std::pow(10.0f, db / 20.0f);
}

inline float linearToDb(float linear) {
    constexpr float kMinLinear = 1e-12f;
    linear = std::max(linear, kMinLinear);
    return 20.0f * std::log10(linear);
}

inline float clamp01(float value) {
    return std::clamp(value, 0.0f, 1.0f);
}

inline float sampleToSeconds(std::uint64_t sampleIndex, double sampleRate) {
    return static_cast<float>(sampleIndex / sampleRate);
}

inline float millisecondsToSamples(float ms, double sampleRate) {
    return static_cast<float>(ms * 0.001 * sampleRate);
}

} // namespace knot::audio

