#include "CrossCorrelationNoiseReducer.h"

#include <algorithm>
#include <cmath>

namespace knot::audio {

void CrossCorrelationNoiseReducer::setup(double sampleRate) {
    (void)sampleRate;  // 現在は未使用だが、将来の拡張に備えて保持
    reset();
}

void CrossCorrelationNoiseReducer::reset() {
    ch1Buffer_.clear();
    ch2Buffer_.clear();
    ch1Buffer_.reserve(kWindowSize);
    ch2Buffer_.reserve(kWindowSize);
}

void CrossCorrelationNoiseReducer::process(float& ch1, float& ch2) {
    // バッファに追加
    ch1Buffer_.push_back(ch1);
    ch2Buffer_.push_back(ch2);

    // ウィンドウサイズを超えたら古いサンプルを削除
    if (ch1Buffer_.size() > kWindowSize) {
        ch1Buffer_.erase(ch1Buffer_.begin());
        ch2Buffer_.erase(ch2Buffer_.begin());
    }

    // バッファが十分でない場合は処理をスキップ
    if (ch1Buffer_.size() < kWindowSize) {
        return;
    }

    // 相関係数を計算
    float correlation = calculateCrossCorrelation(ch1Buffer_, ch2Buffer_);

    // 相関が高い = 環境ノイズの可能性が高い
    // ただし、心拍音を誤って除去しないよう、閾値を高めに設定
    if (correlation > kCorrelationThreshold) {
        // 共通成分（環境ノイズ）を推定
        // 平均を取ることで、独立した心拍音成分は相殺される
        float commonNoise = (ch1 + ch2) * 0.5f;

        // ノイズを控えめに減算（心拍音を保護）
        ch1 -= commonNoise * kNoiseReductionFactor;
        ch2 -= commonNoise * kNoiseReductionFactor;
    }
}

float CrossCorrelationNoiseReducer::calculateCrossCorrelation(
    const std::vector<float>& x, const std::vector<float>& y) {
    if (x.size() != y.size() || x.size() < 2) {
        return 0.0f;
    }

    // 平均を計算
    float meanX = 0.0f;
    float meanY = 0.0f;
    for (std::size_t i = 0; i < x.size(); ++i) {
        meanX += x[i];
        meanY += y[i];
    }
    meanX /= static_cast<float>(x.size());
    meanY /= static_cast<float>(y.size());

    // 相関係数を計算（ピアソン相関係数）
    float numerator = 0.0f;
    float denomX = 0.0f;
    float denomY = 0.0f;
    for (std::size_t i = 0; i < x.size(); ++i) {
        const float dx = x[i] - meanX;
        const float dy = y[i] - meanY;
        numerator += dx * dy;
        denomX += dx * dx;
        denomY += dy * dy;
    }

    const float denominator = std::sqrt(denomX * denomY);
    return (denominator > 1e-6f) ? numerator / denominator : 0.0f;
}

} // namespace knot::audio

