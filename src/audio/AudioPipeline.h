#pragma once

#include "BeatTimeline.h"
#include "Calibration.h"
#include "ParticipantId.h"
#include "PinkNoiseFilter.h"
#include "SimpleLimiter.h"
#include "Utility.h"
#include "BiquadFilter.h"

#include "ofSoundBuffer.h"

#include <array>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <random>
#include <string>
#include <vector>

class ofxFft;

namespace knot::audio {

class AudioPipeline {
public:
    enum class NoiseMode {
        Raw = 0,
        Gate = 1,
        SpecSub = 2,
    };

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
    void setNoiseControlMode(NoiseMode mode);
    void setNoiseGate(float threshold, float attenuation);
    void setSpectralSubtraction(float alpha, float floor, float smoothing);

    void audioIn(const ofSoundBuffer& buffer);
    void audioOut(ofSoundBuffer& buffer);

    struct ChannelMetrics {
        float bpm = 0.0f;
        float envelope = 0.0f;
        double timestampSec = 0.0;
        bool triggered = false;
        ParticipantId participantId = ParticipantId::None;
    };

    const BeatTimeline& beatTimeline() const { return beatTimelines_[0]; }
    float lastLimiterReductionDb() const { return limiterReductionDb_; }
    struct BeatMetrics {
        float bpm = 0.0f;
        float envelope = 0.0f;
        double timestampSec = 0.0;
        bool triggered = false;
    };
    BeatMetrics latestMetrics() const;
    std::vector<BeatEvent> pollBeatEvents();
    ChannelMetrics channelMetrics(ParticipantId id) const;
    std::vector<BeatEvent> pollBeatEvents(ParticipantId id);
    struct SignalHealth {
        float envelopeShort = 0.0f;
        float envelopeMid = 0.0f;
        float envelopeLong = 0.0f;
        float bpmAverage = 0.0f;
        float dropoutSeconds = 0.0f;
        bool fallbackActive = false;
        float fallbackBlend = 0.0f;
        float fallbackEnvelope = 0.0f;
        float noiseRms = 0.0f;
        bool noiseChannelPresent = false;
        float noiseGateGain = 1.0f;
        bool noiseGateEngaged = false;
        bool specSubActive = false;
        bool specSubReady = false;
        bool specSubAppliedCh1 = false;
        bool specSubAppliedCh2 = false;
        float specSubAlpha = 0.0f;
        float specSubFloor = 0.0f;
        float specSubSmoothing = 0.0f;
    };
    SignalHealth signalHealth() const;

private:
    double sampleRate_ = 48000.0;
    std::size_t bufferSize_ = 512;
    std::array<ChannelCalibrationValue, 2> calibrationValues_{};

    CalibrationSession calibrationSession_{};
    bool calibrationArmed_ = false;
    bool calibrationCompleted_ = false;

    std::array<BeatTimeline, 2> beatTimelines_{};
    SimpleLimiter limiter_{};

    mutable std::mutex mutex_;
    std::array<std::vector<float>, 3> channelBuffers_;
    std::vector<float> outputScratch_;
    std::vector<float> inputStereoScratch_;
    std::array<PinkNoiseFilter, 2> pinkNoiseFilters_;  // Stereo pink noise (L/R)
    float inputGainLinear_ = 1.0f;
    float targetInputGainLinear_ = 1.0f;
    float smoothedInputGainLinear_ = 1.0f;
    static constexpr float kGainSmoothingCoeff = 0.01f;  // 約100ms @48kHz
    NoiseMode noiseMode_ = NoiseMode::Raw;
    float noiseGateThreshold_ = 0.2f;
    float noiseGateAttenuation_ = 0.0f;
    float noiseGateGain_ = 1.0f;
    static constexpr float kNoiseGateSmoothingCoeff = 0.2f;
    float specSubAlpha_ = 1.5f;
    float specSubFloor_ = 0.01f;
    float specSubNoiseSmoothing_ = 0.6f;
    std::unique_ptr<ofxFft> specSubSignalFft_;
    std::unique_ptr<ofxFft> specSubNoiseFft_;
    std::vector<float> specSubAmplitude_;
    std::vector<float> specSubNoiseMagSmoothed_;
    std::size_t specSubFftSize_ = 0;
    BeatMetrics metrics_{};
    std::array<std::deque<BeatEvent>, 2> pendingEventsByChannel_;
    std::array<ChannelMetrics, 2> channelMetrics_{};
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
    std::uint64_t legacySequenceCounter_ = 0;

    // Gentle boost around ~100 Hz for heartbeat presence (per channel)
    BiquadFilter lowBandEnhance_[2]{};
    float lowBandEnhanceMix_ = 0.05f; // small parallel band boost (~+1 dB perceived at center)
    // High-pass to remove sub-bass/rumble when input is poor
    BiquadFilter rumbleHighPass_[2]{};
    // Notch filter to remove power line noise (50/60Hz)
    std::array<BiquadFilter, 3> notchFilters_{};

    void applyCalibration(float& ch1, float& ch2) const;
    void ensureBufferSizes(std::size_t numFrames);
    static std::optional<std::size_t> participantIndex(ParticipantId id);
    bool ensureSpecSubFft(std::size_t fftSize);
};

} // namespace knot::audio
