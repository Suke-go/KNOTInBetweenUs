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

struct EnvelopeCalibrationStats {
    double durationSec = 0.0;
    float mean = 0.0f;
    float peak = 0.0f;
    float suggestedTriggerRatio = 1.25f;
    std::size_t sampleCount = 0;
    bool valid = false;
};

class BeatTimeline {
public:
    void setup(double sampleRate);

    void processBuffer(const float* monoInput, std::size_t numFrames, double startSampleIndex);

    float currentBpm() const { return currentBpm_; }
    float currentEnvelope() const { return envelopeFollower_.value(); }
    const std::deque<BeatEvent>& events() const { return events_; }
    bool lastFrameTriggered() const { return lastTrigger_; }
    void beginEnvelopeCalibration(double durationSec);
    void finalizeEnvelopeCalibration();
    bool isEnvelopeCalibrating() const { return envelopeCalibrating_; }
    float calibrationProgress() const;
    const EnvelopeCalibrationStats& calibrationStats() const { return calibrationStats_; }

private:
    double sampleRate_ = 48000.0;
    BiquadFilter bandPass1_;
    BiquadFilter bandPass2_;
    EnvelopeFollower envelopeFollower_;

    float adaptiveThreshold_ = 0.0f;
    float thresholdHoldMs_ = 120.0f;
    std::size_t holdSamples_ = 0;
    std::size_t holdCounter_ = 0;
    std::size_t refractorySamples_ = 0;
    std::size_t refractoryCounter_ = 0;
    float minTriggerRatio_ = 1.25f;
    double lastTriggerSample_ = 0.0;
    float lastEnvelope_ = 0.0f;
    float currentBpm_ = 0.0f;
    bool lastTrigger_ = false;
    std::deque<BeatEvent> events_;
    std::size_t noTriggerCounter_ = 0;
    std::size_t noTriggerRelaxSamples_ = 0;
    float minTriggerRatioDefault_ = 1.25f;
    float minTriggerRatioMin_ = 1.05f;
    float minTriggerRatioMax_ = 1.6f;

    bool envelopeCalibrating_ = false;
    std::size_t calibrationSamplesTotal_ = 0;
    std::size_t calibrationSamplesRemaining_ = 0;
    std::size_t calibrationSampleCount_ = 0;
    double calibrationSum_ = 0.0;
    float calibrationMax_ = 0.0f;
    EnvelopeCalibrationStats calibrationStats_;

    static constexpr std::size_t kMaxEvents = 256;
};

} // namespace knot::audio
