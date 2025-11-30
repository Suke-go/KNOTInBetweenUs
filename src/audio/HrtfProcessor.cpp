#include "HrtfProcessor.h"

#include <algorithm>
#include <cmath>
#include <limits>

#if KNOT_HAS_MYSOFA
#include <mysofa.h>
#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace knot::audio {

HrtfProcessor::HrtfProcessor() {
    reset();
}

HrtfProcessor::~HrtfProcessor() {
#if KNOT_HAS_MYSOFA
    closeHandle();
#endif
}

bool HrtfProcessor::loadHrtfData(const std::string& sofaFilePath) {
#if KNOT_HAS_MYSOFA
    closeHandle();
    int err = 0;
    sofaHandle_ = mysofa_open(sofaFilePath.c_str(), static_cast<float>(sampleRate_), &err);
    if (!sofaHandle_ || err != 0) {
        hrtfLoaded_ = false;
        closeHandle();
        return false;
    }

    sofaPath_ = sofaFilePath;
    hrtfLoaded_ = true;
    resetHistoryBuffer(0);
    return true;
#else
    (void)sofaFilePath;
    hrtfLoaded_ = false;
    return false;
#endif
}

void HrtfProcessor::setDirection(float azimuthDeg, float elevationDeg) {
    currentAzimuth_ = azimuthDeg;
    currentElevation_ = elevationDeg;
}

void HrtfProcessor::setSampleRate(float sampleRate) {
    sampleRate_ = sampleRate;
}

void HrtfProcessor::processBlock(const float* input, float* outputLeft, float* outputRight, size_t numSamples) {
    // HRTFを基本とし、利用不可の場合のみ簡易処理へフォールバック
    for (size_t i = 0; i < numSamples; ++i) {
        float left = 0.0f;
        float right = 0.0f;
        processSpatial(input[i], currentAzimuth_, 1.0f, left, right);
        outputLeft[i] = left;
        outputRight[i] = right;
    }
}

void HrtfProcessor::processSpatial(float sample, float azimuthDeg, float distance,
                                   float& outputLeft, float& outputRight) {
    if (hrtfLoaded_) {
        updateFilter(azimuthDeg, currentElevation_, distance);

        const float attenuated = sample * calculateDistanceAttenuation(distance);
        if (!hrirLeft_.empty() && !hrirRight_.empty() &&
            historyBuffer_.size() >= std::max(hrirLeft_.size(), hrirRight_.size())) {
            historyBuffer_[historyIndex_] = attenuated;

            auto convolveEar = [&](const std::vector<float>& hrir, float delay) {
                float acc = 0.0f;
                const std::size_t bufferSize = historyBuffer_.size();
                const std::size_t baseDelay = delay > 0.0f ? static_cast<std::size_t>(std::floor(delay)) : 0;
                const float fracDelay = delay > 0.0f ? (delay - std::floor(delay)) : 0.0f;

                for (std::size_t i = 0; i < hrir.size(); ++i) {
                    const std::size_t offset = i + baseDelay;
                    const std::size_t idx0 = (historyIndex_ + bufferSize - offset) % bufferSize;
                    float sample0 = historyBuffer_[idx0];
                    if (fracDelay > 0.0f) {
                        const std::size_t idx1 = (idx0 + bufferSize - 1) % bufferSize;
                        const float sample1 = historyBuffer_[idx1];
                        sample0 = sample0 * (1.0f - fracDelay) + sample1 * fracDelay;
                    }
                    acc += sample0 * hrir[i];
                }
                return acc;
            };

            const float left = convolveEar(hrirLeft_, delayLeft_);
            const float right = convolveEar(hrirRight_, delayRight_);

            historyIndex_ = (historyIndex_ + 1) % historyBuffer_.size();
            outputLeft = left;
            outputRight = right;
            return;
        }
    }

    // 簡易的な空間音響処理（フォールバック）
    const float minDistance = 1.0f;
    const float refDistance = minDistance;
    float distanceAttenuation = (refDistance * refDistance) / (distance * distance);
    distanceAttenuation = std::max(0.1f, std::min(1.0f, distanceAttenuation));
    float attenuated = sample * distanceAttenuation;

    // 距離に応じたスペクトラル変化（ローパスフィルター）
    applyDistanceFiltering(attenuated, distance);

    // 方位角に基づくパンニング
    float panAngle = (azimuthDeg * static_cast<float>(M_PI) / 180.0f + static_cast<float>(M_PI)) / 4.0f;
    float panLeft = std::cos(panAngle);
    float panRight = std::sin(panAngle);

    outputLeft = attenuated * panLeft;
    outputRight = attenuated * panRight;
}

void HrtfProcessor::applyDistanceFiltering(float& sample, float distance) {
    // 簡易的な1次ローパスフィルター
    const float maxCutoff = 20000.0f;
    const float minCutoff = 2000.0f;
    const float minDistance = 1.0f;
    const float maxDistance = 3.0f;
    
    float distanceNormalized = (distance - minDistance) / (maxDistance - minDistance);
    float cutoffFreq = maxCutoff - (maxCutoff - minCutoff) * distanceNormalized;
    
    const float dt = 1.0f / sampleRate_;
    const float rc = 1.0f / (2.0f * static_cast<float>(M_PI) * cutoffFreq);
    const float alpha = dt / (rc + dt);
    
    lowpassStateLeft_ = lowpassStateLeft_ + alpha * (sample - lowpassStateLeft_);
    lowpassStateRight_ = lowpassStateRight_ + alpha * (sample - lowpassStateRight_);
    sample = (lowpassStateLeft_ + lowpassStateRight_) * 0.5f;
}

void HrtfProcessor::reset() {
    currentAzimuth_ = 0.0f;
    currentElevation_ = 0.0f;
    hrirLeft_.clear();
    hrirRight_.clear();
    historyBuffer_.clear();
    historyIndex_ = 0;
    delayLeft_ = 0.0f;
    delayRight_ = 0.0f;
    lastDistance_.reset();
    loadedAzimuth_.reset();
    loadedElevation_.reset();
#if KNOT_HAS_MYSOFA
    closeHandle();
#endif
    lowpassStateLeft_ = 0.0f;
    lowpassStateRight_ = 0.0f;
}

void HrtfProcessor::updateFilter(float azimuthDeg, float elevationDeg, float distance) {
#if KNOT_HAS_MYSOFA
    if (!hrtfLoaded_) {
        return;
    }

    if (!sofaHandle_ && !sofaPath_.empty()) {
        int err = 0;
        sofaHandle_ = mysofa_open(sofaPath_.c_str(), static_cast<float>(sampleRate_), &err);
        if (!sofaHandle_ || err != 0) {
            hrtfLoaded_ = false;
            closeHandle();
            return;
        }
    }

    const bool needsDirectionUpdate = !loadedAzimuth_.has_value() || !loadedElevation_.has_value() ||
        std::fabs(azimuthDeg - *loadedAzimuth_) > 1e-3f ||
        std::fabs(elevationDeg - *loadedElevation_) > 1e-3f;
    if (hrirLeft_.empty() || hrirRight_.empty() || needsDirectionUpdate ||
        !lastDistance_.has_value() || std::fabs(distance - *lastDistance_) > 1e-3f) {

        if (!sofaHandle_) {
            hrtfLoaded_ = false;
            return;
        }

        const float azRad = azimuthDeg * static_cast<float>(M_PI) / 180.0f;
        const float elRad = elevationDeg * static_cast<float>(M_PI) / 180.0f;
        const float x = distance * std::cos(elRad) * std::cos(azRad);
        const float y = distance * std::cos(elRad) * std::sin(azRad);
        const float z = distance * std::sin(elRad);

        // バッファを十分に大きく確保（MIT KEMAR HRIRは~512サンプル程度）
        constexpr std::size_t kMaxHrirLength = 1024;
        std::vector<float> tempLeft(kMaxHrirLength, 0.0f);
        std::vector<float> tempRight(kMaxHrirLength, 0.0f);
        int length = 0;
        float delayLeft = 0.0f;
        float delayRight = 0.0f;

        mysofa_getfilter_float(sofaHandle_, x, y, z, tempLeft.data(), tempRight.data(), &length, &delayLeft, &delayRight);

        if (length > 0 && static_cast<std::size_t>(length) <= kMaxHrirLength) {
            hrirLeft_.assign(tempLeft.begin(), tempLeft.begin() + length);
            hrirRight_.assign(tempRight.begin(), tempRight.begin() + length);
            const float maxEarDelay = std::max(delayLeft, delayRight);
            const std::size_t historyLength = static_cast<std::size_t>(length) +
                static_cast<std::size_t>(std::ceil(std::max(0.0f, maxEarDelay))) + 1;
            resetHistoryBuffer(historyLength);
            delayLeft_ = delayLeft;
            delayRight_ = delayRight;
            lastDistance_ = distance;
            loadedAzimuth_ = azimuthDeg;
            loadedElevation_ = elevationDeg;
        } else {
            hrtfLoaded_ = false;
        }
    }
#else
    (void)azimuthDeg;
    (void)elevationDeg;
    (void)distance;
#endif
}

void HrtfProcessor::resetHistoryBuffer(std::size_t length) {
    historyBuffer_.assign(std::max<std::size_t>(1, length), 0.0f);
    historyIndex_ = 0;
}

float HrtfProcessor::calculateDistanceAttenuation(float distance) const {
    const float minDistance = 1.0f;
    const float refDistance = minDistance;
    float attenuation = (refDistance * refDistance) / (distance * distance);
    return std::max(0.1f, std::min(1.0f, attenuation));
}

#if KNOT_HAS_MYSOFA
void HrtfProcessor::closeHandle() {
    if (sofaHandle_) {
        mysofa_close(sofaHandle_);
        sofaHandle_ = nullptr;
    }
}
#endif

} // namespace knot::audio

