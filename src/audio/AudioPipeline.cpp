#include "AudioPipeline.h"

#include "Utility.h"

#include "ofMain.h"

#include <algorithm>
#include <array>

namespace knot::audio {

namespace {
constexpr float kSelfGainDb = -15.0f;
constexpr float kNoiseGainDb = -24.0f;
} // namespace

void AudioPipeline::setup(double sampleRate, std::size_t bufferSize) {
    sampleRate_ = sampleRate;
    bufferSize_ = bufferSize;
    calibrationValues_[0] = {"CH1", 1.0f, 0.0f, 0};
    calibrationValues_[1] = {"CH2", 1.0f, 0.0f, 0};
    calibrationSession_.setup(sampleRate_, bufferSize_, 4);
    beatTimelines_[0].setup(sampleRate_, ParticipantId::Participant1);
    beatTimelines_[1].setup(sampleRate_, ParticipantId::Participant2);
    limiter_.setup(sampleRate_, -3.0f, 80.0f);
    rng_.seed(std::random_device{}());
    for (auto& channelBuffer : channelBuffers_) {
        channelBuffer.assign(bufferSize_, 0.0f);
    }
    noiseBuffer_.assign(bufferSize_, 0.0f);
    outputScratch_.assign(bufferSize_ * 2, 0.0f);
    totalSamplesProcessed_ = 0.0;
    limiterReductionDb_ = 0.0f;
    lastEnvelopeCalibration_ = {};
    envelopeCalibrationActive_ = false;
    newEnvelopeCalibrationAvailable_ = false;
    envelopeShortAvg_ = 0.0f;
    envelopeMidAvg_ = 0.0f;
    envelopeLongAvg_ = 0.0f;
    bpmAvg_ = 0.0f;
    lastRealBeatSample_ = 0.0;
    lastHealthUpdateSec_ = 0.0;
    fallbackActive_ = false;
    fallbackBlend_ = 0.0f;
    fallbackEnvelope_ = 0.0f;
    fallbackBpm_ = 60.0f;
    lastFallbackEmitSec_ = 0.0;
    signalHealth_ = {};
    metrics_ = {};
    for (auto& metrics : channelMetrics_) {
        metrics = {};
    }
    channelMetrics_[0].participantId = ParticipantId::Participant1;
    channelMetrics_[1].participantId = ParticipantId::Participant2;
    for (auto& pending : pendingEventsByChannel_) {
        pending.clear();
    }
    legacySequenceCounter_ = 0;
}

void AudioPipeline::setNoiseSeed(std::uint32_t seed) {
    std::lock_guard<std::mutex> lock(mutex_);
    rng_.seed(seed);
}

void AudioPipeline::setInputGainDb(float gainDb) {
    std::lock_guard<std::mutex> lock(mutex_);
    inputGainLinear_ = dbToLinear(gainDb);
}

void AudioPipeline::ensureBufferSizes(std::size_t numFrames) {
    for (auto& channelBuffer : channelBuffers_) {
        if (channelBuffer.size() < numFrames) {
            channelBuffer.assign(numFrames, 0.0f);
        }
    }
    if (noiseBuffer_.size() < numFrames) {
        noiseBuffer_.assign(numFrames, 0.0f);
    }
    if (outputScratch_.size() < numFrames * 4) {
        outputScratch_.assign(numFrames * 4, 0.0f);
    }
}

void AudioPipeline::loadCalibrationFile(const std::filesystem::path& path) {
    auto loaded = CalibrationFileIO::load(path);
    if (loaded) {
        calibrationValues_ = *loaded;
        calibrationCompleted_ = true;
    }
}

bool AudioPipeline::saveCalibrationFile(const std::filesystem::path& path) const {
    if (!calibrationCompleted_) {
        return false;
    }
    return CalibrationFileIO::save(path, calibrationValues_);
}

void AudioPipeline::startCalibration() {
    std::lock_guard<std::mutex> lock(mutex_);
    calibrationSession_.start();
    calibrationArmed_ = true;
    calibrationCompleted_ = false;
    limiter_.reset();
    beatTimelines_[0].setup(sampleRate_, ParticipantId::Participant1);
    beatTimelines_[1].setup(sampleRate_, ParticipantId::Participant2);
    metrics_ = {};
    for (auto& channelMetric : channelMetrics_) {
        channelMetric = {};
    }
    channelMetrics_[0].participantId = ParticipantId::Participant1;
    channelMetrics_[1].participantId = ParticipantId::Participant2;
    for (auto& pending : pendingEventsByChannel_) {
        pending.clear();
    }
    totalSamplesProcessed_ = 0.0;
    envelopeCalibrationActive_ = false;
    newEnvelopeCalibrationAvailable_ = false;
    envelopeShortAvg_ = 0.0f;
    envelopeMidAvg_ = 0.0f;
    envelopeLongAvg_ = 0.0f;
    bpmAvg_ = 0.0f;
    fallbackActive_ = false;
    fallbackBlend_ = 0.0f;
    fallbackEnvelope_ = 0.0f;
    lastFallbackEmitSec_ = 0.0;
    signalHealth_ = {};
    lastRealBeatSample_ = totalSamplesProcessed_;
    lastHealthUpdateSec_ = totalSamplesProcessed_ / sampleRate_;
    legacySequenceCounter_ = 0;
}

bool AudioPipeline::isCalibrationActive() const {
    return calibrationArmed_;
}

bool AudioPipeline::calibrationReady() const {
    return calibrationCompleted_;
}

void AudioPipeline::applyCalibration(float& ch1, float& ch2) const {
    ch1 *= calibrationValues_[0].gain;
    ch2 *= calibrationValues_[1].gain;
}

void AudioPipeline::startEnvelopeCalibration(double durationSec) {
    std::lock_guard<std::mutex> lock(mutex_);
    beatTimelines_[0].beginEnvelopeCalibration(durationSec);
    envelopeCalibrationActive_ = beatTimelines_[0].isEnvelopeCalibrating();
    newEnvelopeCalibrationAvailable_ = false;
}

bool AudioPipeline::isEnvelopeCalibrationActive() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return envelopeCalibrationActive_;
}

float AudioPipeline::envelopeCalibrationProgress() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return beatTimelines_[0].calibrationProgress();
}

EnvelopeCalibrationStats AudioPipeline::lastEnvelopeCalibration() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return lastEnvelopeCalibration_;
}

bool AudioPipeline::pollEnvelopeCalibrationStats(EnvelopeCalibrationStats& stats) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!newEnvelopeCalibrationAvailable_) {
        return false;
    }
    stats = lastEnvelopeCalibration_;
    newEnvelopeCalibrationAvailable_ = false;
    return true;
}

void AudioPipeline::audioIn(const ofSoundBuffer& buffer) {
    const auto numFrames = static_cast<std::size_t>(buffer.getNumFrames());
    if (buffer.getNumChannels() < 2 || numFrames == 0) {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    ensureBufferSizes(numFrames);
    const float* input = buffer.getBuffer().data();

    if (calibrationArmed_) {
        calibrationSession_.capture(input, numFrames);
        totalSamplesProcessed_ += static_cast<double>(numFrames);
        signalHealth_ = {};
    } else {
        const bool wasEnvelopeCalibrating = beatTimelines_[0].isEnvelopeCalibrating();
        for (std::size_t frame = 0; frame < numFrames; ++frame) {
            float ch1 = input[frame * 2];
            float ch2 = input[frame * 2 + 1];
            if (inputGainLinear_ != 1.0f) {
                ch1 = std::clamp(ch1 * inputGainLinear_, -1.0f, 1.0f);
                ch2 = std::clamp(ch2 * inputGainLinear_, -1.0f, 1.0f);
            }
            applyCalibration(ch1, ch2);
            channelBuffers_[0][frame] = ch1;
            channelBuffers_[1][frame] = ch2;
        }
        const double startSample = totalSamplesProcessed_;
        constexpr std::array<ParticipantId, 2> participants = {
            ParticipantId::Participant1,
            ParticipantId::Participant2};
        for (std::size_t channel = 0; channel < beatTimelines_.size(); ++channel) {
            beatTimelines_[channel].processBuffer(channelBuffers_[channel].data(), numFrames,
                                                  startSample);
            auto& channelMetric = channelMetrics_[channel];
            const auto participantId = participants[channel];
            channelMetric.bpm = beatTimelines_[channel].currentBpm();
            channelMetric.envelope = beatTimelines_[channel].currentEnvelope();
            channelMetric.timestampSec =
                (totalSamplesProcessed_ + static_cast<double>(numFrames)) / sampleRate_;
            channelMetric.triggered = beatTimelines_[channel].lastFrameTriggered();
            channelMetric.participantId = participantId;

            if (channelMetric.triggered) {
                const auto& events = beatTimelines_[channel].events();
                if (!events.empty()) {
                    pendingEventsByChannel_[channel].push_back(events.back());
                    if (pendingEventsByChannel_[channel].size() > 128) {
                        pendingEventsByChannel_[channel].pop_front();
                    }
                }
            }
        }
        totalSamplesProcessed_ += static_cast<double>(numFrames);

        metrics_.bpm = channelMetrics_[0].bpm;
        metrics_.envelope = channelMetrics_[0].envelope;
        metrics_.timestampSec = totalSamplesProcessed_ / sampleRate_;
        metrics_.triggered = channelMetrics_[0].triggered;
        if (metrics_.triggered) {
            if (metrics_.bpm > 1.0f) {
                bpmAvg_ = bpmAvg_ + 0.25f * (metrics_.bpm - bpmAvg_);
            }
            lastRealBeatSample_ = totalSamplesProcessed_;
        }
        const bool isEnvelopeCalibrating = beatTimelines_[0].isEnvelopeCalibrating();
        if (wasEnvelopeCalibrating && !isEnvelopeCalibrating) {
            lastEnvelopeCalibration_ = beatTimelines_[0].calibrationStats();
            envelopeCalibrationActive_ = false;
            newEnvelopeCalibrationAvailable_ = true;
        } else {
            envelopeCalibrationActive_ = isEnvelopeCalibrating;
        }

        const float env = metrics_.envelope;
        envelopeShortAvg_ = envelopeShortAvg_ + 0.35f * (env - envelopeShortAvg_);
        envelopeMidAvg_ = envelopeMidAvg_ + 0.12f * (env - envelopeMidAvg_);
        envelopeLongAvg_ = envelopeLongAvg_ + 0.03f * (env - envelopeLongAvg_);

        const double nowSec = totalSamplesProcessed_ / sampleRate_;
        const double dropoutSec = (totalSamplesProcessed_ - lastRealBeatSample_) / sampleRate_;
        const double deltaSec = std::max(0.0, nowSec - lastHealthUpdateSec_);
        lastHealthUpdateSec_ = nowSec;

        constexpr double kFallbackStartThreshold = 1.5;
        constexpr double kFallbackStopThreshold = 0.6;

        if (!fallbackActive_) {
            if (dropoutSec > kFallbackStartThreshold) {
                fallbackActive_ = true;
                fallbackBlend_ = 0.0f;
                fallbackBpm_ = std::clamp(bpmAvg_ > 1.0f ? bpmAvg_ : 60.0f, 20.0f, 140.0f);
                fallbackEnvelope_ = std::clamp(envelopeLongAvg_, 0.18f, 0.6f);
                const double interval = 60.0 / fallbackBpm_;
                lastFallbackEmitSec_ = std::max(nowSec - interval, 0.0);
            }
        } else {
            if (dropoutSec < kFallbackStopThreshold) {
                fallbackBlend_ = std::max(0.0f, fallbackBlend_ - static_cast<float>(deltaSec / 0.8));
                if (fallbackBlend_ <= 0.02f) {
                    fallbackActive_ = false;
                    fallbackBlend_ = 0.0f;
                }
            } else {
                fallbackBlend_ = std::min(1.0f, fallbackBlend_ + static_cast<float>(deltaSec / 1.0));
                const float targetEnv = std::clamp(envelopeLongAvg_, 0.18f, 0.6f);
                fallbackEnvelope_ = fallbackEnvelope_ + 0.1f * (targetEnv - fallbackEnvelope_);
                const double interval = 60.0 / fallbackBpm_;
                while (nowSec - lastFallbackEmitSec_ >= interval) {
                    lastFallbackEmitSec_ += interval;
                    BeatEvent evt;
                    evt.timestampSec = lastFallbackEmitSec_;
                    evt.bpm = fallbackBpm_;
                    evt.envelope = fallbackEnvelope_;
                    evt.participantId = ParticipantId::Participant1;
                    evt.sequenceId = legacySequenceCounter_++;
                    pendingEventsByChannel_[0].push_back(evt);
                    if (pendingEventsByChannel_[0].size() > 128) {
                        pendingEventsByChannel_[0].pop_front();
                    }
                }
            }
        }

        signalHealth_.envelopeShort = envelopeShortAvg_;
        signalHealth_.envelopeMid = envelopeMidAvg_;
        signalHealth_.envelopeLong = envelopeLongAvg_;
        signalHealth_.bpmAverage = bpmAvg_;
        signalHealth_.dropoutSeconds = static_cast<float>(dropoutSec);
        signalHealth_.fallbackActive = fallbackActive_;
        signalHealth_.fallbackBlend = fallbackBlend_;
        signalHealth_.fallbackEnvelope = fallbackActive_ ? fallbackEnvelope_ : envelopeLongAvg_;
    }
}

void AudioPipeline::audioOut(ofSoundBuffer& buffer) {
    const auto numFrames = static_cast<std::size_t>(buffer.getNumFrames());
    if (buffer.getNumChannels() < 2 || numFrames == 0) {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    ensureBufferSizes(numFrames);
    float* output = buffer.getBuffer().data();

    if (calibrationArmed_) {
        calibrationSession_.generate(output, numFrames);
        if (calibrationSession_.isComplete()) {
            calibrationValues_ = calibrationSession_.result();
            calibrationArmed_ = false;
            calibrationCompleted_ = true;
            limiter_.reset();
            beatTimelines_[0].setup(sampleRate_, ParticipantId::Participant1);
            beatTimelines_[1].setup(sampleRate_, ParticipantId::Participant2);
            totalSamplesProcessed_ = 0.0;
            metrics_ = {};
            for (auto& pending : pendingEventsByChannel_) {
                pending.clear();
            }
            for (auto& metric : channelMetrics_) {
                metric = {};
            }
            channelMetrics_[0].participantId = ParticipantId::Participant1;
            channelMetrics_[1].participantId = ParticipantId::Participant2;
        }
        return;
    }

    const float selfGain = dbToLinear(kSelfGainDb);
    const float noiseGain = dbToLinear(kNoiseGainDb);

    for (std::size_t frame = 0; frame < numFrames; ++frame) {
        noiseBuffer_[frame] = noiseDist_(rng_);
    }

    for (std::size_t frame = 0; frame < numFrames; ++frame) {
        const float heartbeatP1 =
            frame < channelBuffers_[0].size() ? channelBuffers_[0][frame] : 0.0f;
        const float heartbeatP2 =
            frame < channelBuffers_[1].size() ? channelBuffers_[1][frame] : 0.0f;
        const float noise = noiseBuffer_[frame] * noiseGain;

        float left = heartbeatP1 * selfGain + noise;
        float right = heartbeatP2 * selfGain + noise;

        const float detectionSample = (std::fabs(left) >= std::fabs(right)) ? left : right;
        limiter_.process(detectionSample);
        const float gain = limiter_.currentGain();
        left *= gain;
        right *= gain;

        output[frame * 2] = left;
        output[frame * 2 + 1] = right;
    }

    limiterReductionDb_ = limiter_.lastReductionDb();
}

AudioPipeline::SignalHealth AudioPipeline::signalHealth() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return signalHealth_;
}

AudioPipeline::BeatMetrics AudioPipeline::latestMetrics() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return metrics_;
}

std::vector<BeatEvent> AudioPipeline::pollBeatEvents() {
    std::lock_guard<std::mutex> lock(mutex_);
    const std::size_t totalSize =
        pendingEventsByChannel_[0].size() + pendingEventsByChannel_[1].size();
    std::vector<BeatEvent> events;
    events.reserve(totalSize);
    for (auto& pending : pendingEventsByChannel_) {
        events.insert(events.end(), pending.begin(), pending.end());
        pending.clear();
    }
    std::stable_sort(events.begin(), events.end(), [](const BeatEvent& a, const BeatEvent& b) {
        if (a.timestampSec == b.timestampSec) {
            return a.sequenceId < b.sequenceId;
        }
        return a.timestampSec < b.timestampSec;
    });
    return events;
}

AudioPipeline::ChannelMetrics AudioPipeline::channelMetrics(ParticipantId id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto idx = participantIndex(id);
    if (!idx) {
        return {};
    }
    return channelMetrics_[*idx];
}

std::vector<BeatEvent> AudioPipeline::pollBeatEvents(ParticipantId id) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto idx = participantIndex(id);
    if (!idx) {
        return {};
    }
    std::vector<BeatEvent> events(pendingEventsByChannel_[*idx].begin(),
                                  pendingEventsByChannel_[*idx].end());
    pendingEventsByChannel_[*idx].clear();
    return events;
}

std::optional<std::size_t> AudioPipeline::participantIndex(ParticipantId id) {
    switch (id) {
        case ParticipantId::Participant1:
            return 0;
        case ParticipantId::Participant2:
            return 1;
        default:
            return std::nullopt;
    }
}

} // namespace knot::audio
