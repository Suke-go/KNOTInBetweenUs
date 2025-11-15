#pragma once

#include <array>
#include <random>

namespace knot::audio {

/**
 * Pink noise (1/f) generator using the Voss-McCartney algorithm.
 * Produces noise with power spectral density inversely proportional to frequency.
 * Useful for creating relaxing, natural-sounding background noise.
 */
class PinkNoiseFilter {
public:
    PinkNoiseFilter() {
        std::random_device rd;
        rng_.seed(rd());
        reset();
    }

    /**
     * Generate the next pink noise sample.
     * @return Pink noise sample in range approximately [-1.0, 1.0]
     */
    float process() {
        // Voss-McCartney algorithm:
        // Update octaves based on bit changes in counter
        const std::uint32_t lastCounter = counter_;
        ++counter_;
        const std::uint32_t diff = lastCounter ^ counter_;

        float sum = 0.0f;
        for (std::size_t i = 0; i < kNumOctaves; ++i) {
            // Check if bit i has changed
            if (diff & (1u << i)) {
                octaveValues_[i] = dist_(rng_);
            }
            sum += octaveValues_[i];
        }

        // Normalize: average of 16 uniform [-1, 1] values
        // Expected range is still approximately [-1, 1] but with 1/f spectrum
        constexpr float scale = 1.0f / static_cast<float>(kNumOctaves);
        return sum * scale;
    }

    /**
     * Reset the internal state.
     */
    void reset() {
        octaveValues_.fill(0.0f);
        counter_ = 0;
    }

private:
    static constexpr std::size_t kNumOctaves = 16;

    std::array<float, kNumOctaves> octaveValues_{};
    std::mt19937 rng_;
    std::uniform_real_distribution<float> dist_{-1.0f, 1.0f};
    std::uint32_t counter_ = 0;
};

} // namespace knot::audio
