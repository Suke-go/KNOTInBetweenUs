#include "AudioPipeline.h"

#include "Utility.h"
#include "ofxFft.h"

#include "ofMain.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace knot::audio {

namespace {
constexpr float kSelfGainDb = -16.0f;
constexpr float kNoiseGainDb = -23.0f;
constexpr std::size_t kParticipantChannels = 2;
constexpr std::size_t kNoiseChannelIndex = 2;
} // namespace

void AudioPipeline::setup(double sampleRate, std::size_t bufferSize) {
    sampleRate_ = sampleRate;
    bufferSize_ = bufferSize;
    // Ensure default pass-through gains so mic input is audible before calibration
    calibrationValues_[0] = {"CH1", 1.0f, 0.0f, 0};
    calibrationValues_[1] = {"CH2", 1.0f, 0.0f, 0};
    calibrationSession_.setup(sampleRate_, bufferSize_, 4);
    beatTimelines_[0].setup(sampleRate_, ParticipantId::Participant1);
    beatTimelines_[1].setup(sampleRate_, ParticipantId::Participant2);
    limiter_.setup(sampleRate_, -3.0f, 80.0f);
    for (auto& channelBuffer : channelBuffers_) {
        channelBuffer.assign(bufferSize_, 0.0f);
    }
    for (auto& filter : pinkNoiseFilters_) {
        filter.reset();
    }
    // Setup gentle enhancement filters around ~100 Hz (parallel bandpass)
    lowBandEnhance_[0].setup(BiquadFilter::Type::BandPass, sampleRate_, 100.0, 1.0);
    lowBandEnhance_[1].setup(BiquadFilter::Type::BandPass, sampleRate_, 100.0, 1.0);
    // Setup rumble high-pass (~50 Hz) to avoid sub-bass build-up
    rumbleHighPass_[0].setup(BiquadFilter::Type::HighPass, sampleRate_, 50.0, 0.707);
    rumbleHighPass_[1].setup(BiquadFilter::Type::HighPass, sampleRate_, 50.0, 0.707);
    // Setup notch filters to remove power line noise (50Hz, Q=10.0 for narrow notch)
    notchFilters_[0].setup(BiquadFilter::Type::Notch, sampleRate_, 50.0, 10.0);
    notchFilters_[1].setup(BiquadFilter::Type::Notch, sampleRate_, 50.0, 10.0);
    notchFilters_[2].setup(BiquadFilter::Type::Notch, sampleRate_, 50.0, 10.0);
    outputScratch_.assign(bufferSize_ * 2, 0.0f);
    inputStereoScratch_.assign(bufferSize_ * 2, 0.0f);
    totalSamplesProcessed_ = 0.0;
    limiterReductionDb_ = 0.0f;
    lastEnvelopeCalibration_ = {};
    envelopeCalibrationActive_ = false;
    newEnvelopeCalibrationAvailable_ = false;
    envelopeShortAvg_ = 0.0f;
    envelopeMidAvg_ = 0.0f;
    envelopeLongAvg_ = 0.0f;
    bpmAvg_ = 0.0f;
    noiseGateGain_ = 1.0f;
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
    targetInputGainLinear_ = 1.0f;
    smoothedInputGainLinear_ = 1.0f;
    specSubFftSize_ = 0;
    specSubAmplitude_.clear();
    specSubNoiseMagSmoothed_.clear();
    specSubSignalFft_.reset();
    specSubNoiseFft_.reset();
}

void AudioPipeline::setNoiseSeed(std::uint32_t seed) {
    std::lock_guard<std::mutex> lock(mutex_);
    // Reset pink noise filters to ensure deterministic behavior
    for (auto& filter : pinkNoiseFilters_) {
        filter.reset();
    }
}

void AudioPipeline::setInputGainDb(float gainDb) {
    std::lock_guard<std::mutex> lock(mutex_);
    targetInputGainLinear_ = dbToLinear(gainDb);
    // smoothedInputGainLinear_ は audioIn() 内で段階的に更新
}

void AudioPipeline::setNoiseControlMode(NoiseMode mode) {
    std::lock_guard<std::mutex> lock(mutex_);
    noiseMode_ = mode;
    if (noiseMode_ != NoiseMode::Gate) {
        noiseGateGain_ = 1.0f;
    }
}

void AudioPipeline::setNoiseGate(float threshold, float attenuation) {
    std::lock_guard<std::mutex> lock(mutex_);
    noiseGateThreshold_ = std::max(0.0f, threshold);
    noiseGateAttenuation_ = std::clamp(attenuation, 0.0f, 1.0f);
}

void AudioPipeline::setSpectralSubtraction(float alpha, float floor, float smoothing) {
    std::lock_guard<std::mutex> lock(mutex_);
    specSubAlpha_ = std::clamp(alpha, 0.0f, 5.0f);
    specSubFloor_ = std::clamp(floor, 0.0f, 1.0f);
    specSubNoiseSmoothing_ = std::clamp(smoothing, 0.0f, 1.0f);
}

void AudioPipeline::ensureBufferSizes(std::size_t numFrames) {
    for (auto& channelBuffer : channelBuffers_) {
        if (channelBuffer.size() < numFrames) {
            channelBuffer.assign(numFrames, 0.0f);
        }
    }
    if (outputScratch_.size() < numFrames * 4) {
        outputScratch_.assign(numFrames * 4, 0.0f);
    }
    if (inputStereoScratch_.size() < numFrames * 2) {
        inputStereoScratch_.assign(numFrames * 2, 0.0f);
    }
}

bool AudioPipeline::ensureSpecSubFft(std::size_t fftSize) {
    if (fftSize == 0) {
        return false;
    }
    if (specSubFftSize_ == fftSize && specSubSignalFft_ && specSubNoiseFft_) {
        return true;
    }

    specSubSignalFft_.reset();
    specSubNoiseFft_.reset();
    specSubAmplitude_.clear();
    specSubNoiseMagSmoothed_.clear();

    specSubSignalFft_.reset(ofxFft::create(static_cast<int>(fftSize), OF_FFT_WINDOW_HAMMING));
    specSubNoiseFft_.reset(ofxFft::create(static_cast<int>(fftSize), OF_FFT_WINDOW_HAMMING));

    if (!specSubSignalFft_ || !specSubNoiseFft_) {
        ofLogError("AudioPipeline::ensureSpecSubFft")
            << "Failed to initialize FFT for spectral subtraction with size " << fftSize;
        specSubFftSize_ = 0;
        return false;
    }

    specSubFftSize_ = fftSize;
    const auto bins = static_cast<std::size_t>(specSubSignalFft_->getBinSize());
    specSubAmplitude_.assign(bins, 0.0f);
    specSubNoiseMagSmoothed_.assign(bins, 0.0f);
    return true;
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
    // Guard against accidental zeroed gains (treat as unity)
    const float g1 = (calibrationValues_[0].gain > 0.0f) ? calibrationValues_[0].gain : 1.0f;
    const float g2 = (calibrationValues_[1].gain > 0.0f) ? calibrationValues_[1].gain : 1.0f;
    ch1 *= g1;
    ch2 *= g2;
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
    
    // Debug: Log input status periodically (every ~100 calls, roughly 2 seconds at 48kHz/512 frames)
    static std::size_t audioPipelineInCallCount = 0;
    audioPipelineInCallCount++;
    const bool shouldLog = (audioPipelineInCallCount % 100 == 0);
    
    if (buffer.getNumChannels() < kParticipantChannels || numFrames == 0) {
        if (shouldLog) {
            ofLogWarning("AudioPipeline::audioIn") << "Invalid input buffer - channels: "
                                                    << buffer.getNumChannels()
                                                    << ", frames: " << numFrames;
        }
        return;
    }

    const std::size_t inputChannels = buffer.getNumChannels();
    const bool hasNoiseChannel = inputChannels > kNoiseChannelIndex;
    
    if (shouldLog) {
        ofLogNotice("AudioPipeline::audioIn") << "=== AudioPipeline Input Debug (call " << audioPipelineInCallCount << ") ===";
        ofLogNotice("AudioPipeline::audioIn") << "Input channels: " << buffer.getNumChannels();
        ofLogNotice("AudioPipeline::audioIn") << "Input frames: " << numFrames;
        ofLogNotice("AudioPipeline::audioIn") << "calibrationArmed_: " << (calibrationArmed_ ? "YES" : "NO");
    }

    std::lock_guard<std::mutex> lock(mutex_);
    ensureBufferSizes(numFrames);
    
    // Smooth gain changes to prevent sudden jumps from rubbing sounds
    const float gainDiff = targetInputGainLinear_ - smoothedInputGainLinear_;
    smoothedInputGainLinear_ += gainDiff * kGainSmoothingCoeff;
    inputGainLinear_ = smoothedInputGainLinear_;
    
    const float* input = buffer.getBuffer().data();
    
    // Debug: Log input signal level periodically (calculate before processing)
    if (shouldLog) {
        float maxCh1 = 0.0f, maxCh2 = 0.0f, maxNoise = 0.0f;
        float rmsCh1 = 0.0f, rmsCh2 = 0.0f, rmsNoise = 0.0f;
        for (std::size_t frame = 0; frame < numFrames; ++frame) {
            const float ch1 = input[frame * inputChannels];
            const float ch2 = input[frame * inputChannels + 1];
            const float noise = hasNoiseChannel ? input[frame * inputChannels + kNoiseChannelIndex] : 0.0f;
            maxCh1 = std::max(maxCh1, std::fabs(ch1));
            maxCh2 = std::max(maxCh2, std::fabs(ch2));
            maxNoise = std::max(maxNoise, std::fabs(noise));
            rmsCh1 += ch1 * ch1;
            rmsCh2 += ch2 * ch2;
            rmsNoise += noise * noise;
        }
        rmsCh1 = std::sqrt(rmsCh1 / numFrames);
        rmsCh2 = std::sqrt(rmsCh2 / numFrames);
        rmsNoise = std::sqrt(rmsNoise / numFrames);
        ofLogNotice("AudioPipeline::audioIn") << "Raw input level - CH1: max=" << maxCh1 << " rms=" << rmsCh1
                                               << " | CH2: max=" << maxCh2 << " rms=" << rmsCh2
                                               << (hasNoiseChannel ? " | CH3(noise): max=" + ofToString(maxNoise) + " rms=" +
                                                                         ofToString(rmsNoise)
                                                                  : " | CH3(noise): N/A");
        ofLogNotice("AudioPipeline::audioIn") << "inputGainLinear_: " << inputGainLinear_;
    }

    if (calibrationArmed_) {
        const float* calibrationInput = input;
        if (inputChannels > kParticipantChannels) {
            for (std::size_t frame = 0; frame < numFrames; ++frame) {
                inputStereoScratch_[frame * 2] = input[frame * inputChannels];
                inputStereoScratch_[frame * 2 + 1] = input[frame * inputChannels + 1];
            }
            calibrationInput = inputStereoScratch_.data();
        }
        calibrationSession_.capture(calibrationInput, numFrames);
        totalSamplesProcessed_ += static_cast<double>(numFrames);
        signalHealth_ = {};
    } else {
        const bool wasEnvelopeCalibrating = beatTimelines_[0].isEnvelopeCalibrating();
        double noiseSumSquares = 0.0;
        for (std::size_t frame = 0; frame < numFrames; ++frame) {
            float ch1 = input[frame * inputChannels];
            float ch2 = input[frame * inputChannels + 1];
            float noiseRef = hasNoiseChannel ? input[frame * inputChannels + kNoiseChannelIndex] : 0.0f;
            // Apply notch filter to remove power line noise (50/60Hz)
            ch1 = notchFilters_[0].process(ch1);
            ch2 = notchFilters_[1].process(ch2);
            noiseRef = notchFilters_[2].process(noiseRef);
            if (inputGainLinear_ != 1.0f) {
                ch1 = std::clamp(ch1 * inputGainLinear_, -1.0f, 1.0f);
                ch2 = std::clamp(ch2 * inputGainLinear_, -1.0f, 1.0f);
                noiseRef = std::clamp(noiseRef * inputGainLinear_, -1.0f, 1.0f);
            }
            applyCalibration(ch1, ch2);
            channelBuffers_[0][frame] = ch1;
            channelBuffers_[1][frame] = ch2;
            channelBuffers_[kNoiseChannelIndex][frame] = noiseRef;
            noiseSumSquares += static_cast<double>(noiseRef) * static_cast<double>(noiseRef);
        }
        const float noiseRms = hasNoiseChannel && numFrames > 0
                                   ? static_cast<float>(std::sqrt(noiseSumSquares / numFrames))
                                   : 0.0f;

        // Apply side-chain noise gate (Phase 1 safety) when enabled
        float targetGateGain = 1.0f;
        bool gateEngaged = false;
        if (noiseMode_ == NoiseMode::Gate && hasNoiseChannel) {
            if (noiseRms > noiseGateThreshold_) {
                targetGateGain = noiseGateAttenuation_;
                gateEngaged = true;
            }
        }
        noiseGateGain_ = noiseGateGain_ +
                         kNoiseGateSmoothingCoeff * (targetGateGain - noiseGateGain_);
        if (std::fabs(noiseGateGain_ - targetGateGain) < 1e-4f) {
            noiseGateGain_ = targetGateGain;
        }
        if (noiseGateGain_ < 0.999f) {
            for (std::size_t frame = 0; frame < numFrames; ++frame) {
                channelBuffers_[0][frame] *= noiseGateGain_;
                channelBuffers_[1][frame] *= noiseGateGain_;
            }
        }

        // Spectral subtraction (Phase 2) when explicitly enabled and CH3 is present
        bool specSubReady = false;
        bool specSubApplied = false;
        bool specSubAppliedCh1 = false;
        bool specSubAppliedCh2 = false;
        if (noiseMode_ == NoiseMode::SpecSub) {
            if (hasNoiseChannel && ensureSpecSubFft(numFrames)) {
                specSubReady = true;
                const auto bins = static_cast<std::size_t>(specSubSignalFft_->getBinSize());
                if (specSubAmplitude_.size() < bins) {
                    specSubAmplitude_.assign(bins, 0.0f);
                }
                if (specSubNoiseMagSmoothed_.size() < bins) {
                    specSubNoiseMagSmoothed_.assign(bins, 0.0f);
                }

                // Noise FFT (shared across participant channels)
                specSubNoiseFft_->setSignal(channelBuffers_[kNoiseChannelIndex].data());
                const float* noiseAmplitude = specSubNoiseFft_->getAmplitude();
                const float smooth = specSubNoiseSmoothing_;
                for (std::size_t i = 0; i < bins; ++i) {
                    const float noiseMag = noiseAmplitude[i];
                    float& smoothed = specSubNoiseMagSmoothed_[i];
                    if (smooth > 0.0f) {
                        smoothed = smoothed + smooth * (noiseMag - smoothed);
                    } else {
                        smoothed = noiseMag;
                    }
                }

                // Apply spectral subtraction to each participant channel independently
                for (std::size_t channel = 0; channel < kParticipantChannels; ++channel) {
                    specSubSignalFft_->setSignal(channelBuffers_[channel].data());
                    const float* signalAmplitude = specSubSignalFft_->getAmplitude();
                    const float* signalPhase = specSubSignalFft_->getPhase();

                    for (std::size_t i = 0; i < bins; ++i) {
                        float magnitude = signalAmplitude[i] - specSubAlpha_ * specSubNoiseMagSmoothed_[i];
                        magnitude = std::max(magnitude, specSubFloor_);
                        specSubAmplitude_[i] = magnitude;
                    }

                    specSubSignalFft_->setPolar(specSubAmplitude_.data(), signalPhase);
                    float* reconstructed = specSubSignalFft_->getSignal();
                    for (std::size_t frame = 0; frame < numFrames; ++frame) {
                        float sample = reconstructed[frame];
                        if (!std::isfinite(sample)) {
                            sample = 0.0f;
                        }
                        channelBuffers_[channel][frame] = std::clamp(sample, -1.0f, 1.0f);
                    }
                    specSubAppliedCh1 = specSubAppliedCh1 || channel == 0;
                    specSubAppliedCh2 = specSubAppliedCh2 || channel == 1;
                }
                specSubApplied = true;
            } else {
                if (!specSubNoiseMagSmoothed_.empty()) {
                    std::fill(specSubNoiseMagSmoothed_.begin(), specSubNoiseMagSmoothed_.end(), 0.0f);
                }
                if (shouldLog) {
                    ofLogWarning("AudioPipeline::audioIn")
                        << "Spectral subtraction requested but noise channel or FFT unavailable.";
                }
            }
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
            
            // Debug: Log envelope values periodically
            if (shouldLog) {
                ofLogNotice("AudioPipeline::audioIn") << "Channel " << channel 
                                                       << " (P" << (channel + 1) << ")"
                                                       << " - envelope: " << channelMetric.envelope
                                                       << " | BPM: " << channelMetric.bpm
                                                       << " | triggered: " << (channelMetric.triggered ? "YES" : "NO");
            }

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
        signalHealth_.noiseRms = noiseRms;
        signalHealth_.noiseChannelPresent = hasNoiseChannel;
        signalHealth_.noiseGateGain = noiseGateGain_;
        signalHealth_.noiseGateEngaged = gateEngaged;
        signalHealth_.specSubActive = specSubApplied;
        signalHealth_.specSubReady = specSubReady;
        signalHealth_.specSubAppliedCh1 = specSubAppliedCh1;
        signalHealth_.specSubAppliedCh2 = specSubAppliedCh2;
        signalHealth_.specSubAlpha = specSubAlpha_;
        signalHealth_.specSubFloor = specSubFloor_;
        signalHealth_.specSubSmoothing = specSubNoiseSmoothing_;
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
    const float baseNoiseGain = dbToLinear(kNoiseGainDb);

    // Get current envelopes for dynamic noise adjustment
    const float envP1 = channelMetrics_[0].envelope;
    const float envP2 = channelMetrics_[1].envelope;
    const float maxEnv = std::max(envP1, envP2);

    // Dynamic noise gain: reduce noise when heartbeat is strong
    // Also gate noise down significantly when no heartbeat is detected
    float dynamicNoiseGain = baseNoiseGain * (1.0f - 0.7f * maxEnv);
    if (maxEnv < 0.02f) {
        dynamicNoiseGain = baseNoiseGain * 0.12f; // keep very low when input/envelope is absent
    }

    // Gate enhancement when no heartbeat present
    const float enhanceMix = (maxEnv < 0.02f) ? 0.0f : std::clamp(lowBandEnhanceMix_, 0.0f, 0.45f);

    for (std::size_t frame = 0; frame < numFrames; ++frame) {
        const float heartbeatP1 =
            frame < channelBuffers_[0].size() ? channelBuffers_[0][frame] : 0.0f;
        const float heartbeatP2 =
            frame < channelBuffers_[1].size() ? channelBuffers_[1][frame] : 0.0f;

        // Parallel band-limited enhancement around ~100 Hz (gentle)
        const float bandP1 = lowBandEnhance_[0].process(heartbeatP1);
        const float bandP2 = lowBandEnhance_[1].process(heartbeatP2);
        float enhancedP1 = heartbeatP1 + enhanceMix * bandP1;
        float enhancedP2 = heartbeatP2 + enhanceMix * bandP2;
        // Remove sub-bass/rumble
        enhancedP1 = rumbleHighPass_[0].process(enhancedP1);
        enhancedP2 = rumbleHighPass_[1].process(enhancedP2);

        // Generate independent stereo pink noise for spatial depth
        const float pinkNoiseL = pinkNoiseFilters_[0].process() * dynamicNoiseGain;
        const float pinkNoiseR = pinkNoiseFilters_[1].process() * dynamicNoiseGain;

        // Mix heartbeats with stereo pink noise
        float left = enhancedP1 * selfGain + pinkNoiseL;
        float right = enhancedP2 * selfGain + pinkNoiseR;

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
