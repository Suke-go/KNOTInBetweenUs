#include "Calibration.h"

#include "ofMain.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace knot::audio {

namespace {
constexpr double kTwoPi = M_PI * 2.0;
} // namespace

void CalibrationSignalGenerator::setup(const CalibrationPlan& plan) {
    plan_ = plan;
    tonePhaseIncrement_ = kTwoPi * plan_.toneFrequencyHz / plan_.sampleRate;
    reset();
}

void CalibrationSignalGenerator::reset() {
    tonePhase_ = 0.0;
    sampleCursor_ = 0;
    finished_ = false;
    for (auto& state : pulseStates_) {
        state.index = 0;
        state.endSample = 0;
        state.active = false;
    }
}

void CalibrationSignalGenerator::generate(float* interleavedStereo, std::size_t numFrames) {
    if (!interleavedStereo || numFrames == 0) {
        return;
    }

    for (std::size_t i = 0; i < numFrames; ++i) {
        float output[2] = {0.0f, 0.0f};

        if (sampleCursor_ < plan_.toneSamples) {
            const std::uint64_t swapIndex = plan_.toneSwapInterval > 0 ? sampleCursor_ / plan_.toneSwapInterval : 0;
            const int activeChannel = static_cast<int>(swapIndex % 2);
            const float toneSample = static_cast<float>(std::sin(tonePhase_) * plan_.toneAmplitude);
            output[activeChannel] = toneSample;
        } else if (sampleCursor_ >= plan_.pulseStartSample && sampleCursor_ < plan_.totalSamples) {
            for (int ch = 0; ch < 2; ++ch) {
                auto& state = pulseStates_[ch];
                if (state.active && sampleCursor_ >= state.endSample) {
                    state.active = false;
                    ++state.index;
                }
                if (!state.active && state.index < plan_.pulseOffsets[ch].size()) {
                    const std::uint64_t pulseStart =
                        plan_.pulseStartSample + plan_.pulseOffsets[ch][state.index];
                    if (sampleCursor_ >= pulseStart) {
                        state.active = true;
                        state.endSample = pulseStart + plan_.pulseLengthSamples;
                    }
                }
                if (state.active) {
                    output[ch] = plan_.toneAmplitude;
                }
            }
        }

        interleavedStereo[i * 2] = output[0];
        interleavedStereo[i * 2 + 1] = output[1];

        tonePhase_ += tonePhaseIncrement_;
        if (tonePhase_ >= kTwoPi) {
            tonePhase_ -= kTwoPi;
        }

        ++sampleCursor_;
        if (sampleCursor_ >= plan_.totalSamples) {
            finished_ = true;
        }
    }
}

void CalibrationAnalyzer::setup(const CalibrationPlan& plan) {
    plan_ = plan;
    tonePhaseIncrement_ = kTwoPi * plan_.toneFrequencyHz / plan_.sampleRate;
    pulseGuardSamples_ = static_cast<std::uint64_t>(plan_.sampleRate * 0.00065); // 約0.65ms ≒ 31 samples @48k
    reset();
}

void CalibrationAnalyzer::reset() {
    sampleCursor_ = 0;
    tonePhase_ = 0.0;
    for (auto& stats : toneStats_) {
        stats = ToneStats{};
    }
    for (auto& vec : pulseResults_) {
        vec.clear();
    }
    for (auto& st : pulseStates_) {
        st = PulseState{};
    }
}

void CalibrationAnalyzer::ingest(const float* interleavedStereo, std::size_t numFrames) {
    if (!interleavedStereo || numFrames == 0) {
        return;
    }

    for (std::size_t i = 0; i < numFrames; ++i) {
        const float sampleL = interleavedStereo[i * 2];
        const float sampleR = interleavedStereo[i * 2 + 1];
        const float channelSamples[2] = {sampleL, sampleR};

        if (sampleCursor_ < plan_.toneSamples) {
            const std::uint64_t swapIndex =
                plan_.toneSwapInterval > 0 ? sampleCursor_ / plan_.toneSwapInterval : 0;
            const int activeChannel = static_cast<int>(swapIndex % 2);
            const float sinVal = static_cast<float>(std::sin(tonePhase_));
            const float cosVal = static_cast<float>(std::cos(tonePhase_));

            auto& stats = toneStats_[activeChannel];
            const float sample = channelSamples[activeChannel];
            stats.sumSquares += static_cast<double>(sample) * static_cast<double>(sample);
            ++stats.sampleCount;
            stats.dotSin += static_cast<double>(sample) * static_cast<double>(sinVal);
            stats.dotCos += static_cast<double>(sample) * static_cast<double>(cosVal);
        } else if (sampleCursor_ >= plan_.pulseStartSample && sampleCursor_ < plan_.totalSamples) {
            for (int ch = 0; ch < 2; ++ch) {
                auto& state = pulseStates_[ch];
                if (!state.windowActive && state.index < plan_.pulseOffsets[ch].size()) {
                    const std::uint64_t pulseStart =
                        plan_.pulseStartSample + plan_.pulseOffsets[ch][state.index];
                    const std::uint64_t guard = pulseGuardSamples_;
                    const std::uint64_t windowStart = pulseStart > guard ? pulseStart - guard : 0;
                    const std::uint64_t windowEnd = pulseStart + plan_.pulseLengthSamples + guard;
                    if (sampleCursor_ >= windowStart) {
                        state.windowActive = true;
                        state.windowEnd = windowEnd;
                        if (pulseResults_[ch].size() <= state.index) {
                            pulseResults_[ch].resize(state.index + 1);
                        }
                        pulseResults_[ch][state.index].expectedSample = pulseStart;
                        pulseResults_[ch][state.index].maxSample = pulseStart;
                        pulseResults_[ch][state.index].maxAbs = 0.0f;
                    }
                }

                if (state.windowActive) {
                    const float value = channelSamples[ch];
                    const float absValue = std::fabs(value);
                    auto& capture = pulseResults_[ch][state.index];
                    if (absValue > capture.maxAbs) {
                        capture.maxAbs = absValue;
                        capture.maxSample = sampleCursor_;
                    }
                    if (sampleCursor_ >= state.windowEnd) {
                        state.windowActive = false;
                        ++state.index;
                    }
                }
            }
        }

        tonePhase_ += tonePhaseIncrement_;
        if (tonePhase_ >= kTwoPi) {
            tonePhase_ -= kTwoPi;
        }

        ++sampleCursor_;
    }
}

std::array<ChannelCalibrationValue, 2> CalibrationAnalyzer::finalize() const {
    std::array<ChannelCalibrationValue, 2> values;
    const double expectedRms = plan_.toneAmplitude * M_SQRT1_2;
    for (int ch = 0; ch < 2; ++ch) {
        ChannelCalibrationValue val;
        val.name = ch == 0 ? "CH1" : "CH2";

        const auto& stats = toneStats_[ch];
        const double measuredRms =
            stats.sampleCount > 0 ? std::sqrt(stats.sumSquares / static_cast<double>(stats.sampleCount)) : 0.0;
        if (measuredRms > 0.0) {
            val.gain = static_cast<float>(expectedRms / measuredRms);
        } else {
            val.gain = 1.0f;
        }

        if (stats.sampleCount > 0) {
            const double phaseRad = std::atan2(stats.dotCos, stats.dotSin);
            val.phaseDeg = static_cast<float>(phaseRad * 180.0 / M_PI);
        } else {
            val.phaseDeg = 0.0f;
        }

        const auto& pulses = pulseResults_[ch];
        if (!pulses.empty()) {
            double delaySamples = 0.0;
            for (const auto& pulse : pulses) {
                delaySamples += static_cast<double>(static_cast<int64_t>(pulse.maxSample) -
                                                    static_cast<int64_t>(pulse.expectedSample));
            }
            delaySamples /= static_cast<double>(pulses.size());
            val.delaySamples = static_cast<int>(std::llround(delaySamples));
        } else {
            val.delaySamples = 0;
        }

        values[ch] = val;
    }
    return values;
}

float CalibrationAnalyzer::measuredGainDbDelta() const {
    const auto results = finalize();
    if (results.size() < 2) {
        return 0.0f;
    }
    const float ratio = results[0].gain > 0.0f ? results[1].gain / results[0].gain : 1.0f;
    return linearToDb(ratio);
}

bool CalibrationFileIO::save(const std::filesystem::path& path,
                             const std::array<ChannelCalibrationValue, 2>& values) {
    ofJson json;
    json["version"] = 1;
    json["createdUtc"] = ofGetTimestampString("%FT%TZ");
    json["channels"] = ofJson::array();
    for (const auto& value : values) {
        ofJson ch;
        ch["name"] = value.name;
        ch["gain"] = value.gain;
        ch["phaseDeg"] = value.phaseDeg;
        ch["delaySamples"] = value.delaySamples;
        json["channels"].push_back(ch);
    }

    ofFile file(path, ofFile::WriteOnly, true);
    if (!file.is_open()) {
        return false;
    }
    file << json.dump(2);
    return true;
}

std::optional<std::array<ChannelCalibrationValue, 2>> CalibrationFileIO::load(
    const std::filesystem::path& path) {
    ofFile file(path, ofFile::ReadOnly);
    if (!file.exists()) {
        return std::nullopt;
    }
    ofJson json;
    file >> json;
    std::array<ChannelCalibrationValue, 2> values;
    const auto& channels = json["channels"];
    for (std::size_t i = 0; i < 2; ++i) {
        if (i < channels.size()) {
            values[i].name = channels[i]["name"].get<std::string>();
            values[i].gain = channels[i]["gain"].get<float>();
            values[i].phaseDeg = channels[i]["phaseDeg"].get<float>();
            values[i].delaySamples = channels[i]["delaySamples"].get<int>();
        }
    }
    return values;
}

void CalibrationSession::setup(double sampleRate, std::uint64_t toneSwapInterval, std::size_t pulsePairs) {
    plan_.sampleRate = sampleRate;
    plan_.toneFrequencyHz = 1000.0;
    plan_.toneAmplitude = 0.25f;
    plan_.toneSamples = static_cast<std::uint64_t>(sampleRate * 5.0);
    plan_.toneSwapInterval = toneSwapInterval > 0 ? toneSwapInterval : 512;
    plan_.pulseLengthSamples = 256;
    plan_.pulseSpacingSamples = static_cast<std::uint64_t>(sampleRate * 0.25); // 250ms 間隔
    plan_.pulseStartSample = plan_.toneSamples + static_cast<std::uint64_t>(sampleRate * 0.5);

    plan_.pulseOffsets = {};
    for (std::size_t i = 0; i < pulsePairs; ++i) {
        const std::uint64_t offsetBase = static_cast<std::uint64_t>(i) * plan_.pulseSpacingSamples;
        plan_.pulseOffsets[0].push_back(offsetBase);
        plan_.pulseOffsets[1].push_back(offsetBase + plan_.pulseSpacingSamples / 2);
    }

    const std::uint64_t lastPulseOffset =
        pulsePairs > 0 ? plan_.pulseOffsets[1].back() : 0;
    plan_.totalSamples = plan_.pulseStartSample + lastPulseOffset + plan_.pulseLengthSamples +
                         static_cast<std::uint64_t>(sampleRate * 0.25);

    generator_.setup(plan_);
    analyzer_.setup(plan_);
    complete_ = false;
    running_ = false;
    result_ = {};
    result_[0].name = "CH1";
    result_[1].name = "CH2";
}

void CalibrationSession::start() {
    generator_.reset();
    analyzer_.reset();
    running_ = true;
    complete_ = false;
}

void CalibrationSession::generate(float* interleavedStereo, std::size_t numFrames) {
    if (!running_) {
        if (interleavedStereo) {
            std::fill(interleavedStereo, interleavedStereo + numFrames * 2, 0.0f);
        }
        return;
    }
    generator_.generate(interleavedStereo, numFrames);
}

void CalibrationSession::capture(const float* interleavedStereo, std::size_t numFrames) {
    if (!running_) {
        return;
    }
    analyzer_.ingest(interleavedStereo, numFrames);
    if (generator_.isFinished()) {
        running_ = false;
        complete_ = true;
        result_ = analyzer_.finalize();
    }
}

} // namespace knot::audio
