#include "AudioRouter.h"

#include "Utility.h"

#include <algorithm>
#include <optional>

namespace {

std::optional<std::size_t> participantIndex(knot::audio::ParticipantId id) {
    using knot::audio::ParticipantId;
    switch (id) {
        case ParticipantId::Participant1:
            return 0;
        case ParticipantId::Participant2:
            return 1;
        default:
            return std::nullopt;
    }
}

} // namespace

namespace knot::audio {

void AudioRouter::setRoutingRule(OutputChannel channel, const RoutingRule& rule) {
    rules_[static_cast<std::size_t>(channel)] = rule;
}

const RoutingRule& AudioRouter::routingRule(OutputChannel channel) const {
    return rules_[static_cast<std::size_t>(channel)];
}

void AudioRouter::applyScenePreset(SceneState scene) {
    (void)scene;
    // Phase 4: Scene-dependent routing will be implemented later.
}

void AudioRouter::route(const std::array<float, 2>& inputEnvelopes,
                        std::array<float, 4>& outputBuffer) {
    outputBuffer.fill(0.0f);

    for (std::size_t outputIdx = 0; outputIdx < rules_.size(); ++outputIdx) {
        const auto& rule = rules_[outputIdx];
        const auto participant = participantIndex(rule.source);
        if (!participant || rule.mixMode == MixMode::Silent) {
            outputBuffer[outputIdx] = 0.0f;
            continue;
        }

        float sample = 0.0f;
        switch (rule.mixMode) {
            case MixMode::Self:
            case MixMode::Partner:
                sample = inputEnvelopes[*participant];
                break;
            case MixMode::Haptic:
                sample = generateHapticSample(inputEnvelopes[*participant], rule.source);
                break;
            case MixMode::Silent:
            default:
                sample = 0.0f;
                break;
        }

        const float gainLinear = dbToLinear(rule.gainDb);
        outputBuffer[outputIdx] = sample * gainLinear;
    }
}

float AudioRouter::generateHapticSample(float envelope, ParticipantId id) {
    (void)id;
    (void)envelope;
    return 0.0f;
}

} // namespace knot::audio

