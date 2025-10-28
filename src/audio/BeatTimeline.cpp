#include "BeatTimeline.h"

#include <cmath>

namespace knot::audio {

void BeatTimeline::setup(double sampleRate) {
    sampleRate_ = sampleRate;
    bandPass1_.setup(BiquadFilter::Type::HighPass, sampleRate_, 20.0, 0.707);
    bandPass2_.setup(BiquadFilter::Type::LowPass, sampleRate_, 150.0, 0.707);
    envelopeFollower_.setup(sampleRate_, 5.0f, 60.0f);
    adaptiveThreshold_ = 0.0f;
    thresholdHoldMs_ = 120.0f;
    holdSamples_ = static_cast<std::size_t>(sampleRate_ * (thresholdHoldMs_ * 0.001f));
    holdCounter_ = 0;
    lastTriggerSample_ = 0.0;
    currentBpm_ = 0.0f;
    events_.clear();
}

void BeatTimeline::processBuffer(const float* monoInput, std::size_t numFrames, double startSampleIndex) {
    if (!monoInput || numFrames == 0) {
        lastTrigger_ = false;
        return;
    }

    lastTrigger_ = false;
    for (std::size_t i = 0; i < numFrames; ++i) {
        const float sample = monoInput[i];
        float filtered = bandPass1_.process(sample);
        filtered = bandPass2_.process(filtered);

        const float env = envelopeFollower_.process(filtered);
        const float lpfCoeff = 0.005f;
        adaptiveThreshold_ = (1.0f - lpfCoeff) * adaptiveThreshold_ + lpfCoeff * env;

        if (holdCounter_ > 0) {
            --holdCounter_;
            continue;
        }

        const float dynamicThreshold = adaptiveThreshold_ * 1.45f + 1e-5f;
        if (env > dynamicThreshold) {
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
            events_.push_back(evt);
            if (events_.size() > kMaxEvents) {
                events_.pop_front();
            }

            holdCounter_ = holdSamples_;
            lastTrigger_ = true;
        }
    }
}

} // namespace knot::audio

