#pragma once

#include "ParticipantId.h"

#include <array>
#include <cstdint>

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
    void setRoutingRule(OutputChannel channel, const RoutingRule& rule);
    const RoutingRule& routingRule(OutputChannel channel) const;

    void applyScenePreset(SceneState scene);

    void route(const std::array<float, 2>& inputEnvelopes, std::array<float, 4>& outputBuffer);

private:
    std::array<RoutingRule, 4> rules_{};

    float generateHapticSample(float envelope, ParticipantId id);
};

} // namespace knot::audio

