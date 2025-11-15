#include "HrtfProcessor.h"

#include <algorithm>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace knot::audio {

HrtfProcessor::HrtfProcessor() {
    reset();
}

HrtfProcessor::~HrtfProcessor() {
}

bool HrtfProcessor::loadHrtfData(const std::string& sofaFilePath) {
    // 将来の実装: libmysofaを使用してSOFAファイルを読み込む
    // 現在は実装されていない（簡易的な空間音響を使用）
    hrtfLoaded_ = false;
    return false;
}

void HrtfProcessor::setDirection(float azimuthDeg, float elevationDeg) {
    currentAzimuth_ = azimuthDeg;
    currentElevation_ = elevationDeg;
}

void HrtfProcessor::processBlock(const float* input, float* outputLeft, float* outputRight, size_t numSamples) {
    // 将来の実装: HRIR畳み込み処理
    // 現在は簡易的な処理を実装
    for (size_t i = 0; i < numSamples; ++i) {
        float sample = input[i];
        float left = 0.0f;
        float right = 0.0f;
        processSpatial(sample, currentAzimuth_, 1.0f, left, right);
        outputLeft[i] = left;
        outputRight[i] = right;
    }
}

void HrtfProcessor::processSpatial(float sample, float azimuthDeg, float distance,
                                   float& outputLeft, float& outputRight) {
    // 簡易的な空間音響処理（現在の実装）
    // 距離減衰
    const float minDistance = 1.0f;
    const float refDistance = minDistance;
    float distanceAttenuation = (refDistance * refDistance) / (distance * distance);
    distanceAttenuation = std::max(0.1f, std::min(1.0f, distanceAttenuation));
    sample *= distanceAttenuation;
    
    // 距離に応じたスペクトラル変化（ローパスフィルター）
    applyDistanceFiltering(sample, distance);
    
    // 方位角に基づくパンニング
    // Constant Power Panning (glm::piの代わりにM_PIを使用)
    float panAngle = (azimuthDeg * static_cast<float>(M_PI) / 180.0f + static_cast<float>(M_PI)) / 4.0f;
    float panLeft = std::cos(panAngle);
    float panRight = std::sin(panAngle);
    
    outputLeft = sample * panLeft;
    outputRight = sample * panRight;
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
    hrtfLoaded_ = false;
    currentAzimuth_ = 0.0f;
    currentElevation_ = 0.0f;
    lowpassStateLeft_ = 0.0f;
    lowpassStateRight_ = 0.0f;
    sampleRate_ = 48000.0f;
}

} // namespace knot::audio

