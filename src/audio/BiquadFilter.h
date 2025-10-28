#pragma once

#include <cmath>

namespace knot::audio {

class BiquadFilter {
public:
    enum class Type {
        BandPass,
        LowPass,
        HighPass
    };

    void setup(Type type, double sampleRate, double freqHz, double q);

    inline float process(float inSample) {
        const float out = b0_ * inSample + b1_ * x1_ + b2_ * x2_ - a1_ * y1_ - a2_ * y2_;
        x2_ = x1_;
        x1_ = inSample;
        y2_ = y1_;
        y1_ = out;
        return out;
    }

    void reset() {
        x1_ = x2_ = y1_ = y2_ = 0.0f;
    }

private:
    double sampleRate_ = 48000.0;
    double freqHz_ = 100.0;
    double q_ = 0.707;
    Type type_ = Type::BandPass;

    float a1_ = 0.0f;
    float a2_ = 0.0f;
    float b0_ = 1.0f;
    float b1_ = 0.0f;
    float b2_ = 0.0f;

    float x1_ = 0.0f;
    float x2_ = 0.0f;
    float y1_ = 0.0f;
    float y2_ = 0.0f;

    void computeCoefficients();
};

} // namespace knot::audio

