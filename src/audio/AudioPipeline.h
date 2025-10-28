#pragma once

#include "BeatTimeline.h"
#include "Calibration.h"
#include "SimpleLimiter.h"
#include "Utility.h"

#include "ofSoundBuffer.h"

#include <array>
#include <deque>
#include <filesystem>
#include <mutex>
#include <random>
#include <string>
#include <vector>

namespace knot::audio {

class AudioPipeline {
public:
    void setup(double sampleRate, std::size_t bufferSize);
    void loadCalibrationFile(const std::filesystem::path& path);
    bool saveCalibrationFile(const std::filesystem::path& path) const;

    void startCalibration();
    bool isCalibrationActive() const;
    bool calibrationReady() const;
    const std::array<ChannelCalibrationValue, 2>& calibrationResult() const { return calibrationValues_; }

    void audioIn(const ofSoundBuffer& buffer);
    void audioOut(ofSoundBuffer& buffer);

    const BeatTimeline& beatTimeline() const { return beatTimeline_; }
    float lastLimiterReductionDb() const { return limiterReductionDb_; }
    struct BeatMetrics {
        float bpm = 0.0f;
        float envelope = 0.0f;
        double timestampSec = 0.0;
        bool triggered = false;
    };
    BeatMetrics latestMetrics() const;
    std::vector<BeatEvent> pollBeatEvents();

private:
    double sampleRate_ = 48000.0;
    std::size_t bufferSize_ = 512;
    std::array<ChannelCalibrationValue, 2> calibrationValues_{};

    CalibrationSession calibrationSession_{};
    bool calibrationArmed_ = false;
    bool calibrationCompleted_ = false;

    BeatTimeline beatTimeline_{};
    SimpleLimiter limiter_{};

    mutable std::mutex mutex_;
    std::vector<float> monoBuffer_;
    std::vector<float> outputScratch_;
    std::vector<float> noiseBuffer_;
    std::mt19937 rng_;
    std::normal_distribution<float> noiseDist_{0.0f, 1.0f};
    BeatMetrics metrics_{};
    std::deque<BeatEvent> pendingEvents_;

    double totalSamplesProcessed_ = 0.0;
    float limiterReductionDb_ = 0.0f;

    void applyCalibration(float& ch1, float& ch2) const;
    void ensureBufferSizes(std::size_t numFrames);
};

} // namespace knot::audio
