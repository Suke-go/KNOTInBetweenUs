#include "BeatVisualizer.h"

#include <algorithm>

BeatEnvelopeHistory::BeatEnvelopeHistory() = default;

void BeatEnvelopeHistory::setHorizon(double seconds) noexcept {
    horizonSeconds_ = std::max(1.0, seconds);
}

void BeatEnvelopeHistory::addSample(double timestampSec, float envelopeValue, float bpmValue) {
    BeatVisualMetrics sample;
    sample.timestampSec = timestampSec;
    sample.envelope = envelopeValue;
    sample.bpm = bpmValue;
    samples_.push_back(sample);
    prune(timestampSec);
}

const std::deque<BeatVisualMetrics>& BeatEnvelopeHistory::samples() const noexcept {
    return samples_;
}

void BeatEnvelopeHistory::prune(double nowSeconds) {
    const double threshold = nowSeconds - horizonSeconds_;
    while (!samples_.empty() && samples_.front().timestampSec < threshold) {
        samples_.pop_front();
    }
}
