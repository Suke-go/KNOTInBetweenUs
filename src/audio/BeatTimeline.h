#pragma once

#include "BiquadFilter.h"
#include "EnvelopeFollower.h"

#include <deque>
#include <optional>
#include <vector>

namespace knot::audio {

struct BeatEvent {
    double timestampSec = 0.0;
    float bpm = 0.0f;
    float envelope = 0.0f;
};

class BeatTimeline {
public:
    void setup(double sampleRate);

    void processBuffer(const float* monoInput, std::size_t numFrames, double startSampleIndex);

    float currentBpm() const { return currentBpm_; }
    float currentEnvelope() const { return envelopeFollower_.value(); }
    const std::deque<BeatEvent>& events() const { return events_; }
    bool lastFrameTriggered() const { return lastTrigger_; }

private:
    double sampleRate_ = 48000.0;
    BiquadFilter bandPass1_;
    BiquadFilter bandPass2_;
    EnvelopeFollower envelopeFollower_;

    float adaptiveThreshold_ = 0.0f;
    float thresholdHoldMs_ = 120.0f;
    std::size_t holdSamples_ = 0;
    std::size_t holdCounter_ = 0;
    double lastTriggerSample_ = 0.0;
    float currentBpm_ = 0.0f;
    bool lastTrigger_ = false;
    std::deque<BeatEvent> events_;

    static constexpr std::size_t kMaxEvents = 256;
};

} // namespace knot::audio

