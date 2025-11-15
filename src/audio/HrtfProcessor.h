#pragma once

#include <cstddef>
#include <vector>
#include <memory>

namespace knot::audio {

// HRTF処理用の構造体
struct HrtfData {
    std::vector<float> leftHrir;   // 左チャンネルHRIR
    std::vector<float> rightHrir;  // 右チャンネルHRIR
    size_t sampleRate = 48000;     // サンプルレート
    size_t length = 0;             // HRIR長さ（サンプル数）
};

// HRTFプロセッサクラス（将来の拡張用）
// 現在は簡易的な空間音響を使用し、将来HRTFを統合する
class HrtfProcessor {
public:
    HrtfProcessor();
    ~HrtfProcessor();
    
    // HRTFデータの読み込み（将来の実装）
    bool loadHrtfData(const std::string& sofaFilePath);
    
    // 方向の設定（方位角、仰角）
    void setDirection(float azimuthDeg, float elevationDeg);
    
    // ブロック処理（将来の実装: HRIR畳み込み）
    void processBlock(const float* input, float* outputLeft, float* outputRight, size_t numSamples);
    
    // 現在の実装: 簡易的な空間音響処理
    void processSpatial(float sample, float azimuthDeg, float distance,
                        float& outputLeft, float& outputRight);
    
    // リセット
    void reset();
    
    // HRTFデータが読み込まれているか
    bool isLoaded() const { return hrtfLoaded_; }

private:
    bool hrtfLoaded_ = false;
    std::unique_ptr<HrtfData> hrtfData_;
    
    float currentAzimuth_ = 0.0f;
    float currentElevation_ = 0.0f;
    
    // 簡易的なフィルター状態（距離フィルタリング用）
    float lowpassStateLeft_ = 0.0f;
    float lowpassStateRight_ = 0.0f;
    float sampleRate_ = 48000.0f;
    
    // 距離フィルタリング（簡易実装）
    void applyDistanceFiltering(float& sample, float distance);
};

} // namespace knot::audio

