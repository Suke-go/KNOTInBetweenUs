#pragma once

#include <vector>
#include <cmath>

namespace knot::audio {

class CrossCorrelationNoiseReducer {
public:
    void setup(double sampleRate);
    void process(float& ch1, float& ch2);
    void reset();

private:
    std::vector<float> ch1Buffer_;
    std::vector<float> ch2Buffer_;
    static constexpr std::size_t kWindowSize = 512;  // 約10ms @48kHz
    static constexpr float kCorrelationThreshold = 0.75f;  // 相関閾値
    static constexpr float kNoiseReductionFactor = 0.6f;  // ノイズ減算係数

    float calculateCrossCorrelation(const std::vector<float>& x,
                                   const std::vector<float>& y);
};

} // namespace knot::audio

