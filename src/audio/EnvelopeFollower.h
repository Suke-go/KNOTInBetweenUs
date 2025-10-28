#pragma once

#include <cmath>

namespace knot::audio {

class EnvelopeFollower {
public:
    void setup(double sampleRate, float attackMs, float releaseMs) {
        sampleRate_ = sampleRate;
        attackCoeff_ = attackMs <= 0.0f ? 0.0f
                                        : std::exp(-1.0f / (0.001f * attackMs * static_cast<float>(sampleRate_)));
        releaseCoeff_ = releaseMs <= 0.0f ? 0.0f
                                          : std::exp(-1.0f / (0.001f * releaseMs * static_cast<float>(sampleRate_)));
        value_ = 0.0f;
    }

    inline float process(float input) {
        const float rectified = std::fabs(input);
        const bool isAttack = rectified > value_;
        const float coeff = isAttack ? attackCoeff_ : releaseCoeff_;
        value_ = (1.0f - coeff) * rectified + coeff * value_;
        return value_;
    }

    float value() const { return value_; }

    void reset() { value_ = 0.0f; }

private:
    double sampleRate_ = 48000.0;
    float attackCoeff_ = 0.0f;
    float releaseCoeff_ = 0.0f;
    float value_ = 0.0f;
};

} // namespace knot::audio

