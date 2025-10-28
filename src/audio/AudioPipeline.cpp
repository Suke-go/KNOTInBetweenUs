#include "AudioPipeline.h"

#include "Utility.h"

#include "ofMain.h"

#include <algorithm>

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
    beatTimeline_.setup(sampleRate_);
    limiter_.setup(sampleRate_, -3.0f, 80.0f);
    rng_.seed(std::random_device{}());
    monoBuffer_.assign(bufferSize_, 0.0f);
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
    if (monoBuffer_.size() < numFrames) {
        monoBuffer_.assign(numFrames, 0.0f);
    }
    if (noiseBuffer_.size() < numFrames) {
        noiseBuffer_.assign(numFrames, 0.0f);
    }
    if (outputScratch_.size() < numFrames * 2) {
        outputScratch_.assign(numFrames * 2, 0.0f);
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
    beatTimeline_.setup(sampleRate_);
    metrics_ = {};
    pendingEvents_.clear();
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
    beatTimeline_.beginEnvelopeCalibration(durationSec);
    envelopeCalibrationActive_ = beatTimeline_.isEnvelopeCalibrating();
    newEnvelopeCalibrationAvailable_ = false;
}

bool AudioPipeline::isEnvelopeCalibrationActive() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return envelopeCalibrationActive_;
}

float AudioPipeline::envelopeCalibrationProgress() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return beatTimeline_.calibrationProgress();
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
        const bool wasEnvelopeCalibrating = beatTimeline_.isEnvelopeCalibrating();
        for (std::size_t frame = 0; frame < numFrames; ++frame) {
            float ch1 = input[frame * 2];
            float ch2 = input[frame * 2 + 1];
            if (inputGainLinear_ != 1.0f) {
                ch1 = std::clamp(ch1 * inputGainLinear_, -1.0f, 1.0f);
                ch2 = std::clamp(ch2 * inputGainLinear_, -1.0f, 1.0f);
            }
            applyCalibration(ch1, ch2);
            monoBuffer_[frame] = 0.5f * (ch1 + ch2);
        }
        const double startSample = totalSamplesProcessed_;
        beatTimeline_.processBuffer(monoBuffer_.data(), numFrames, startSample);
        totalSamplesProcessed_ += static_cast<double>(numFrames);
        metrics_.bpm = beatTimeline_.currentBpm();
        metrics_.envelope = beatTimeline_.currentEnvelope();
        metrics_.timestampSec = totalSamplesProcessed_ / sampleRate_;
        metrics_.triggered = beatTimeline_.lastFrameTriggered();
        if (metrics_.triggered) {
            const auto& events = beatTimeline_.events();
            if (!events.empty()) {
                pendingEvents_.push_back(events.back());
                if (pendingEvents_.size() > 128) {
                    pendingEvents_.pop_front();
                }
            }
            if (metrics_.bpm > 1.0f) {
                bpmAvg_ = bpmAvg_ + 0.25f * (metrics_.bpm - bpmAvg_);
            }
            lastRealBeatSample_ = totalSamplesProcessed_;
        }
        const bool isEnvelopeCalibrating = beatTimeline_.isEnvelopeCalibrating();
        if (wasEnvelopeCalibrating && !isEnvelopeCalibrating) {
            lastEnvelopeCalibration_ = beatTimeline_.calibrationStats();
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
                    pendingEvents_.push_back(evt);
                    if (pendingEvents_.size() > 128) {
                        pendingEvents_.pop_front();
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
            beatTimeline_.setup(sampleRate_);
            totalSamplesProcessed_ = 0.0;
            metrics_ = {};
            pendingEvents_.clear();
        }
        return;
    }

    const float selfGain = dbToLinear(kSelfGainDb);
    const float noiseGain = dbToLinear(kNoiseGainDb);

    for (std::size_t frame = 0; frame < numFrames; ++frame) {
        noiseBuffer_[frame] = noiseDist_(rng_);
    }

    for (std::size_t frame = 0; frame < numFrames; ++frame) {
        const float heartbeat = frame < monoBuffer_.size() ? monoBuffer_[frame] : 0.0f;
        const float selfSample = heartbeat * selfGain;
        const float noise = noiseBuffer_[frame] * noiseGain;

        float left = selfSample + noise;
        float right = selfSample + noise;

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
    std::vector<BeatEvent> events(pendingEvents_.begin(), pendingEvents_.end());
    pendingEvents_.clear();
    return events;
}

} // namespace knot::audio
