#include "BeatTimeline.h"

#include <algorithm>
#include <cmath>

namespace knot::audio {

void BeatTimeline::setup(double sampleRate) {
    setup(sampleRate, ParticipantId::None);
}

void BeatTimeline::setup(double sampleRate, ParticipantId participantId) {
    sampleRate_ = sampleRate;
    participantId_ = participantId;
    bandPass1_.setup(BiquadFilter::Type::HighPass, sampleRate_, 20.0, 0.707);
    bandPass2_.setup(BiquadFilter::Type::LowPass, sampleRate_, 150.0, 0.707);
    envelopeFollower_.setup(sampleRate_, 5.0f, 60.0f);
    adaptiveThreshold_ = 0.0f;
    thresholdHoldMs_ = 120.0f;
    holdSamples_ = static_cast<std::size_t>(sampleRate_ * (thresholdHoldMs_ * 0.001f));
    holdCounter_ = 0;
    const double refractorySec = 0.35;
    refractorySamples_ = static_cast<std::size_t>(std::max(1.0, refractorySec * sampleRate_));
    refractoryCounter_ = 0;
    lastTriggerSample_ = 0.0;
    lastEnvelope_ = 0.0f;
    currentBpm_ = 0.0f;
    events_.clear();
    eventSequence_ = 0;
    noTriggerCounter_ = 0;
    noTriggerRelaxSamples_ = static_cast<std::size_t>(sampleRate_ * 3.0);
    minTriggerRatio_ = minTriggerRatioDefault_;
    calibrationStats_ = {};
    envelopeCalibrating_ = false;
}

void BeatTimeline::beginEnvelopeCalibration(double durationSec) {
    calibrationStats_ = {};
    calibrationSum_ = 0.0;
    calibrationMax_ = 0.0f;
    calibrationSampleCount_ = 0;
    calibrationSamplesTotal_ =
        static_cast<std::size_t>(std::max(1.0, std::round(durationSec * sampleRate_)));
    calibrationSamplesRemaining_ = calibrationSamplesTotal_;
    envelopeCalibrating_ = calibrationSamplesTotal_ > 0;
    if (!envelopeCalibrating_) {
        return;
    }
    // Reset tracking so calibration is not influenced by stale values.
    holdCounter_ = 0;
    refractoryCounter_ = 0;
}

void BeatTimeline::finalizeEnvelopeCalibration() {
    if (!envelopeCalibrating_) {
        return;
    }
    envelopeCalibrating_ = false;
    EnvelopeCalibrationStats stats;
    stats.sampleCount = calibrationSampleCount_;
    stats.durationSec =
        calibrationSampleCount_ > 0 ? calibrationSampleCount_ / sampleRate_ : 0.0;
    if (calibrationSampleCount_ > 0) {
        stats.mean = static_cast<float>(calibrationSum_ / static_cast<double>(calibrationSampleCount_));
        stats.peak = calibrationMax_;
        const float mean = std::max(stats.mean, 1e-6f);
        const float ratio = stats.peak / mean;
        stats.suggestedTriggerRatio =
            std::clamp(ratio * 0.85f, minTriggerRatioMin_, minTriggerRatioMax_);
        stats.valid = stats.peak > 0.0f;
        adaptiveThreshold_ = stats.mean;
        minTriggerRatio_ = stats.suggestedTriggerRatio;
        noTriggerCounter_ = 0;
    } else {
        stats.mean = 0.0f;
        stats.peak = 0.0f;
        stats.suggestedTriggerRatio = minTriggerRatioDefault_;
        stats.valid = false;
    }
    calibrationStats_ = stats;
    calibrationSamplesRemaining_ = 0;
}

float BeatTimeline::calibrationProgress() const {
    if (!envelopeCalibrating_ || calibrationSamplesTotal_ == 0) {
        return 0.0f;
    }
    return std::clamp(static_cast<float>(calibrationSampleCount_) /
                          static_cast<float>(calibrationSamplesTotal_),
                      0.0f, 1.0f);
}

void BeatTimeline::processBuffer(const float* monoInput, std::size_t numFrames, double startSampleIndex) {
    if (!monoInput || numFrames == 0) {
        lastTrigger_ = false;
        return;
    }

    lastTrigger_ = false;
    bool triggeredThisBuffer = false;
    for (std::size_t i = 0; i < numFrames; ++i) {
        const float sample = monoInput[i];
        float filtered = bandPass1_.process(sample);
        filtered = bandPass2_.process(filtered);

        const float env = envelopeFollower_.process(filtered);
        const float lpfCoeff = 0.005f;
        adaptiveThreshold_ = (1.0f - lpfCoeff) * adaptiveThreshold_ + lpfCoeff * env;

        if (envelopeCalibrating_) {
            calibrationSum_ += static_cast<double>(env);
            calibrationMax_ = std::max(calibrationMax_, env);
            ++calibrationSampleCount_;
            if (calibrationSamplesRemaining_ > 0) {
                --calibrationSamplesRemaining_;
            }
            if (calibrationSamplesRemaining_ == 0) {
                finalizeEnvelopeCalibration();
            }
            continue;
        }

        if (holdCounter_ > 0) {
            --holdCounter_;
            continue;
        }
        if (refractoryCounter_ > 0) {
            --refractoryCounter_;
            continue;
        }

        const float dynamicThreshold = adaptiveThreshold_ * 1.45f + 1e-5f;
        const float ratio = dynamicThreshold > 0.0f ? env / dynamicThreshold : 0.0f;
        if (env > dynamicThreshold && ratio >= minTriggerRatio_) {
            const double triggerSample = startSampleIndex + static_cast<double>(i);
            if (lastTriggerSample_ > 0.0) {
                const double deltaSamples = triggerSample - lastTriggerSample_;
                if (deltaSamples > sampleRate_ * 0.25) { // avoid unrealistic high BPM
                    currentBpm_ = static_cast<float>(60.0 * sampleRate_ / deltaSamples);
                }
            }
            lastTriggerSample_ = triggerSample;

            BeatEvent evt;
            evt.timestampSec = triggerSample / sampleRate_;
            evt.bpm = currentBpm_;
            evt.envelope = env;
            evt.participantId = participantId_;
            evt.sequenceId = eventSequence_++;
            events_.push_back(evt);
            if (events_.size() > kMaxEvents) {
                events_.pop_front();
            }

            holdCounter_ = holdSamples_;
            refractoryCounter_ = refractorySamples_;
            lastEnvelope_ = env;
            lastTrigger_ = true;
            triggeredThisBuffer = true;
            noTriggerCounter_ = 0;
            minTriggerRatio_ = std::min(minTriggerRatioMax_, minTriggerRatio_ + 0.01f);
        } else {
            lastTrigger_ = false;
        }
    }

    if (!triggeredThisBuffer && !envelopeCalibrating_) {
        noTriggerCounter_ = std::min<std::size_t>(noTriggerCounter_ + numFrames,
                                                  noTriggerRelaxSamples_ * 4);
        if (noTriggerCounter_ >= noTriggerRelaxSamples_) {
            minTriggerRatio_ =
                std::max(minTriggerRatioMin_, minTriggerRatio_ - 0.03f);
            adaptiveThreshold_ *= 0.99f;
            noTriggerCounter_ = std::min(noTriggerCounter_, noTriggerRelaxSamples_);
        }
    }
}

} // namespace knot::audio
