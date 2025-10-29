#pragma once

#include "ParticipantId.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

enum class SceneState : std::uint8_t;

namespace knot::audio {

enum class OutputChannel : std::uint8_t {
    CH1_HeadphoneLeft = 0,
    CH2_HeadphoneRight = 1,
    CH3_HapticP1 = 2,
    CH4_HapticP2 = 3
};

enum class MixMode : std::uint8_t {
    Self,
    Partner,
    Haptic,
    Silent
};

struct RoutingRule {
    ParticipantId source = ParticipantId::None;
    MixMode mixMode = MixMode::Silent;
    float gainDb = -12.0f;
    float panLR = 0.0f;
};

class AudioRouter {
public:
    void setup(float sampleRateHz);
    void setRoutingRule(OutputChannel channel, const RoutingRule& rule);
    const RoutingRule& routingRule(OutputChannel channel) const;
    void setRule(OutputChannel channel, const RoutingRule& rule);
    const RoutingRule& rule(OutputChannel channel) const;
    std::vector<RoutingRule> rules() const;
    void clearAllRules();
    void restorePreset(SceneState scene);
    bool savePreset(const std::string& presetName, const std::filesystem::path& file) const;
    bool loadPreset(const std::string& presetName, const std::filesystem::path& file);
    std::size_t activeRuleCount() const;

    void applyScenePreset(SceneState scene);

    void route(const std::array<float, 2>& headphoneInput,
               const std::array<float, 2>& inputEnvelopes,
               std::array<float, 4>& outputBuffer);

private:
    std::array<RoutingRule, 4> rules_{};
    float sampleRateHz_ = 48000.0f;
    std::array<double, 2> hapticPhase_{{0.0, 0.0}};

    void clearRules();
    float generateHapticSample(float envelope, ParticipantId id);
};

} // namespace knot::audio
