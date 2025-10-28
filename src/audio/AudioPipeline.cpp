#include "AudioPipeline.h"

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
}

void AudioPipeline::setNoiseSeed(std::uint32_t seed) {
    std::lock_guard<std::mutex> lock(mutex_);
    rng_.seed(seed);
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

BeatTimeline::EnvelopeCalibrationStats AudioPipeline::lastEnvelopeCalibration() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return lastEnvelopeCalibration_;
}

bool AudioPipeline::pollEnvelopeCalibrationStats(BeatTimeline::EnvelopeCalibrationStats& stats) {
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
    } else {
        const bool wasEnvelopeCalibrating = beatTimeline_.isEnvelopeCalibrating();
        for (std::size_t frame = 0; frame < numFrames; ++frame) {
            float ch1 = input[frame * 2];
            float ch2 = input[frame * 2 + 1];
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
        }
        const bool isEnvelopeCalibrating = beatTimeline_.isEnvelopeCalibrating();
        if (wasEnvelopeCalibrating && !isEnvelopeCalibrating) {
            lastEnvelopeCalibration_ = beatTimeline_.calibrationStats();
            envelopeCalibrationActive_ = false;
            newEnvelopeCalibrationAvailable_ = true;
        } else {
            envelopeCalibrationActive_ = isEnvelopeCalibrating;
        }
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
