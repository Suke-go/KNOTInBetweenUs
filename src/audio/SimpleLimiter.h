#pragma once

#include <algorithm>
#include <cmath>

namespace knot::audio {

class SimpleLimiter {
public:
    void setup(double sampleRate, float thresholdDb, float releaseMs) {
        sampleRate_ = sampleRate;
        threshold_ = std::pow(10.0f, thresholdDb / 20.0f);
        releaseCoeff_ = releaseMs <= 0.0f
                            ? 0.0f
                            : std::exp(-1.0f / (releaseMs * 0.001f * static_cast<float>(sampleRate_)));
        gain_ = 1.0f;
        maxGainReductionDb_ = 0.0f;
        lastReductionDb_ = 0.0f;
    }

    inline float process(float sample) {
        const float absSample = std::fabs(sample);
        if (absSample > threshold_) {
            const float targetGain = threshold_ / absSample;
            gain_ = std::min(gain_, targetGain);
        } else {
            gain_ += (1.0f - gain_) * (1.0f - releaseCoeff_);
        }
        const float clampedGain = std::clamp(gain_, 0.0f, 1.0f);
        const float processed = sample * clampedGain;
        const float reductionDb = clampedGain <= 0.0f ? -96.0f : 20.0f * std::log10(clampedGain);
        lastReductionDb_ = reductionDb;
        maxGainReductionDb_ = std::min(maxGainReductionDb_, reductionDb);
        return processed;
    }

    void reset() {
        gain_ = 1.0f;
        maxGainReductionDb_ = 0.0f;
        lastReductionDb_ = 0.0f;
    }

    float lastReductionDb() const { return lastReductionDb_; }
    float maxGainReductionDb() const { return maxGainReductionDb_; }
    float currentGain() const { return gain_; }

private:
    double sampleRate_ = 48000.0;
    float threshold_ = 0.8f;
    float releaseCoeff_ = 0.0f;
    float gain_ = 1.0f;
    float maxGainReductionDb_ = 0.0f;
    float lastReductionDb_ = 0.0f;
};

} // namespace knot::audio
