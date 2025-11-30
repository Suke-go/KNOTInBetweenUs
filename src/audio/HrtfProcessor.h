#pragma once

#include <cstddef>
#include <vector>
#include <memory>
#include <optional>
#include <string>

#if !defined(KNOT_HAS_MYSOFA)
#if __has_include(<mysofa.h>)
#define KNOT_HAS_MYSOFA 1
#else
#define KNOT_HAS_MYSOFA 0
#endif
#endif

#if KNOT_HAS_MYSOFA
struct MYSOFA_EASY;
#endif

namespace knot::audio {

// HRTFプロセッサクラス
// 通常はlibmysofaを使ったHRTF処理を行い、失敗した場合のみ簡易空間音響にフォールバックする
class HrtfProcessor {
public:
    HrtfProcessor();
    ~HrtfProcessor();

    // HRTFデータの読み込み（将来の実装）
    bool loadHrtfData(const std::string& sofaFilePath);

    // サンプルレートの設定（HRTFのリサンプリングに使用）
    void setSampleRate(float sampleRate);

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
    std::string sofaPath_;

    float currentAzimuth_ = 0.0f;
    float currentElevation_ = 0.0f;

    // HRTF畳み込み用の内部バッファ
    std::vector<float> hrirLeft_;
    std::vector<float> hrirRight_;
    std::vector<float> historyBuffer_;
    std::size_t historyIndex_ = 0;
    float delayLeft_ = 0.0f;
    float delayRight_ = 0.0f;
    std::optional<float> lastDistance_;
    std::optional<float> loadedAzimuth_;
    std::optional<float> loadedElevation_;
#if KNOT_HAS_MYSOFA
    MYSOFA_EASY* sofaHandle_ = nullptr;
#endif

    // 簡易的なフィルター状態（距離フィルタリング用）
    float lowpassStateLeft_ = 0.0f;
    float lowpassStateRight_ = 0.0f;
    float sampleRate_ = 48000.0f;

    // 距離フィルタリング（簡易実装）
    void applyDistanceFiltering(float& sample, float distance);

    // HRTFフィルターの更新（方位角・距離が変わったときに再計算）
    void updateFilter(float azimuthDeg, float elevationDeg, float distance);
    void resetHistoryBuffer(std::size_t length);
    float calculateDistanceAttenuation(float distance) const;
#if KNOT_HAS_MYSOFA
    void closeHandle();
#endif
};

} // namespace knot::audio

