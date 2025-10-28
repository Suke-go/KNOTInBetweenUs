#pragma once

#include <deque>
#include <utility>

struct BeatVisualMetrics {
    float bpm = 0.0f;
    float envelope = 0.0f;
    double timestampSec = 0.0;
};

class BeatEnvelopeHistory {
public:
    BeatEnvelopeHistory();

    void setHorizon(double seconds) noexcept;
    void addSample(double timestampSec, float envelopeValue, float bpmValue);
    [[nodiscard]] const std::deque<BeatVisualMetrics>& samples() const noexcept;
    void prune(double nowSeconds);

private:
    double horizonSeconds_ = 15.0;
    std::deque<BeatVisualMetrics> samples_;
};
