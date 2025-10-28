#include "BiquadFilter.h"

#include <cmath>

namespace knot::audio {

void BiquadFilter::setup(Type type, double sampleRate, double freqHz, double q) {
    type_ = type;
    sampleRate_ = sampleRate;
    freqHz_ = freqHz;
    q_ = q;
    computeCoefficients();
    reset();
}

void BiquadFilter::computeCoefficients() {
    const double omega = 2.0 * M_PI * freqHz_ / sampleRate_;
    const double sin_omega = std::sin(omega);
    const double cos_omega = std::cos(omega);
    const double alpha = sin_omega / (2.0 * q_);

    double b0 = 0.0;
    double b1 = 0.0;
    double b2 = 0.0;
    double a0 = 1.0;
    double a1 = 0.0;
    double a2 = 0.0;

    switch (type_) {
        case Type::BandPass:
            b0 = alpha;
            b1 = 0.0;
            b2 = -alpha;
            a0 = 1.0 + alpha;
            a1 = -2.0 * cos_omega;
            a2 = 1.0 - alpha;
            break;
        case Type::LowPass:
            b0 = (1.0 - cos_omega) * 0.5;
            b1 = 1.0 - cos_omega;
            b2 = (1.0 - cos_omega) * 0.5;
            a0 = 1.0 + alpha;
            a1 = -2.0 * cos_omega;
            a2 = 1.0 - alpha;
            break;
        case Type::HighPass:
            b0 = (1.0 + cos_omega) * 0.5;
            b1 = -(1.0 + cos_omega);
            b2 = (1.0 + cos_omega) * 0.5;
            a0 = 1.0 + alpha;
            a1 = -2.0 * cos_omega;
            a2 = 1.0 - alpha;
            break;
    }

    const double inv_a0 = 1.0 / a0;
    b0_ = static_cast<float>(b0 * inv_a0);
    b1_ = static_cast<float>(b1 * inv_a0);
    b2_ = static_cast<float>(b2 * inv_a0);
    a1_ = static_cast<float>(a1 * inv_a0);
    a2_ = static_cast<float>(a2 * inv_a0);
}

} // namespace knot::audio

