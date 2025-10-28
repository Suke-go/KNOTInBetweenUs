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
    void setNoiseSeed(std::uint32_t seed);

    void startEnvelopeCalibration(double durationSec);
    bool isEnvelopeCalibrationActive() const;
    float envelopeCalibrationProgress() const;
    EnvelopeCalibrationStats lastEnvelopeCalibration() const;
    bool pollEnvelopeCalibrationStats(EnvelopeCalibrationStats& stats);
    void setInputGainDb(float gainDb);

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
    struct SignalHealth {
        float envelopeShort = 0.0f;
        float envelopeMid = 0.0f;
        float envelopeLong = 0.0f;
        float bpmAverage = 0.0f;
        float dropoutSeconds = 0.0f;
        bool fallbackActive = false;
        float fallbackBlend = 0.0f;
        float fallbackEnvelope = 0.0f;
    };
    SignalHealth signalHealth() const;

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
    float inputGainLinear_ = 1.0f;
    BeatMetrics metrics_{};
    std::deque<BeatEvent> pendingEvents_;
    EnvelopeCalibrationStats lastEnvelopeCalibration_{};
    bool envelopeCalibrationActive_ = false;
    bool newEnvelopeCalibrationAvailable_ = false;

    double totalSamplesProcessed_ = 0.0;
    float limiterReductionDb_ = 0.0f;
    float envelopeShortAvg_ = 0.0f;
    float envelopeMidAvg_ = 0.0f;
    float envelopeLongAvg_ = 0.0f;
    float bpmAvg_ = 0.0f;
    double lastRealBeatSample_ = 0.0;
    double lastHealthUpdateSec_ = 0.0;
    bool fallbackActive_ = false;
    float fallbackBlend_ = 0.0f;
    float fallbackEnvelope_ = 0.0f;
    float fallbackBpm_ = 60.0f;
    double lastFallbackEmitSec_ = 0.0;
    SignalHealth signalHealth_{};

    void applyCalibration(float& ch1, float& ch2) const;
    void ensureBufferSizes(std::size_t numFrames);
};

} // namespace knot::audio
