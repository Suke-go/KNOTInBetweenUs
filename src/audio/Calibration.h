#pragma once

#include "Utility.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace knot::audio {

struct ChannelCalibrationValue {
    std::string name;
    float gain = 1.0f;
    float phaseDeg = 0.0f;
    int delaySamples = 0;
};

struct CalibrationPlan {
    double sampleRate = 48000.0;
    std::uint64_t toneSamples = 0;
    std::uint64_t toneSwapInterval = 0;
    double toneFrequencyHz = 1000.0;
    float toneAmplitude = 0.25f;
    std::uint64_t pulseStartSample = 0;
    std::uint64_t pulseLengthSamples = 0;
    std::uint64_t pulseSpacingSamples = 0;
    std::array<std::vector<std::uint64_t>, 2> pulseOffsets;
    std::uint64_t totalSamples = 0;
};

class CalibrationSignalGenerator {
public:
    void setup(const CalibrationPlan& plan);
    void reset();
    void generate(float* interleavedStereo, std::size_t numFrames);
    bool isFinished() const { return finished_; }
    std::uint64_t sampleCursor() const { return sampleCursor_; }
    const CalibrationPlan& plan() const { return plan_; }

private:
    CalibrationPlan plan_{};
    double tonePhase_ = 0.0;
    double tonePhaseIncrement_ = 0.0;
    std::uint64_t sampleCursor_ = 0;
    bool finished_ = false;

    struct PulseState {
        std::size_t index = 0;
        std::uint64_t endSample = 0;
        bool active = false;
    };
    std::array<PulseState, 2> pulseStates_{};
};

class CalibrationAnalyzer {
public:
    void setup(const CalibrationPlan& plan);
    void reset();
    void ingest(const float* interleavedStereo, std::size_t numFrames);
    std::array<ChannelCalibrationValue, 2> finalize() const;

    float measuredGainDbDelta() const;

private:
    std::uint64_t sampleCursor_ = 0;
    CalibrationPlan plan_{};
    double tonePhase_ = 0.0;
    double tonePhaseIncrement_ = 0.0;
    std::uint64_t pulseGuardSamples_ = 32;

    struct ToneStats {
        double sumSquares = 0.0;
        std::uint64_t sampleCount = 0;
        double dotSin = 0.0;
        double dotCos = 0.0;
    };
    std::array<ToneStats, 2> toneStats_{};

    struct PulseCapture {
        std::uint64_t expectedSample = 0;
        std::uint64_t maxSample = 0;
        float maxAbs = 0.0f;
    };

    struct PulseState {
        std::size_t index = 0;
        bool windowActive = false;
        std::uint64_t windowEnd = 0;
    };

    std::array<std::vector<PulseCapture>, 2> pulseResults_{};
    std::array<PulseState, 2> pulseStates_{};
};

class CalibrationFileIO {
public:
    static bool save(const std::filesystem::path& path,
                     const std::array<ChannelCalibrationValue, 2>& values);
    static std::optional<std::array<ChannelCalibrationValue, 2>> load(const std::filesystem::path& path);
};

class CalibrationSession {
public:
    void setup(double sampleRate, std::uint64_t toneSwapInterval, std::size_t pulsePairs);
    void start();
    void generate(float* interleavedStereo, std::size_t numFrames);
    void capture(const float* interleavedStereo, std::size_t numFrames);
    bool isRunning() const { return running_; }
    bool isComplete() const { return complete_; }
    const std::array<ChannelCalibrationValue, 2>& result() const { return result_; }

private:
    CalibrationPlan plan_{};
    CalibrationSignalGenerator generator_{};
    CalibrationAnalyzer analyzer_{};
    bool running_ = false;
    bool complete_ = false;
    std::array<ChannelCalibrationValue, 2> result_{};
};

} // namespace knot::audio
