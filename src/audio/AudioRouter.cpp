#include "AudioRouter.h"

#include "SceneController.h"
#include "Utility.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>

#include "ofLog.h"
#include "ofJson.h"
#include "ofFileUtils.h"
#include <glm/gtc/constants.hpp>

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

namespace {

constexpr float kDefaultSilentGainDb = -96.0f;
constexpr float kHapticGain = 0.8f;
constexpr float kHapticFrequencyHz = 50.0f;

RoutingRule makeSilentRule() {
    RoutingRule rule;
    rule.source = ParticipantId::None;
    rule.mixMode = MixMode::Silent;
    rule.gainDb = kDefaultSilentGainDb;
    rule.panLR = 0.0f;
    return rule;
}

} // namespace

void AudioRouter::setup(float sampleRateHz) {
    sampleRateHz_ = std::max(sampleRateHz, 1.0f);
    hapticPhase_.fill(0.0);
    clearRules();
}

void AudioRouter::setRoutingRule(OutputChannel channel, const RoutingRule& rule) {
    rules_[static_cast<std::size_t>(channel)] = rule;
}

const RoutingRule& AudioRouter::routingRule(OutputChannel channel) const {
    return rules_[static_cast<std::size_t>(channel)];
}

void AudioRouter::setRule(OutputChannel channel, const RoutingRule& rule) {
    setRoutingRule(channel, rule);
    ofLogNotice("AudioRouter") << "Rule updated: ch=" << static_cast<int>(channel)
                               << " src=" << static_cast<int>(rule.source)
                               << " mode=" << static_cast<int>(rule.mixMode)
                               << " gain=" << rule.gainDb << "dB";
}

const RoutingRule& AudioRouter::rule(OutputChannel channel) const {
    return routingRule(channel);
}

std::vector<RoutingRule> AudioRouter::rules() const {
    return {rules_.begin(), rules_.end()};
}

void AudioRouter::clearAllRules() {
    clearRules();
    ofLogNotice("AudioRouter") << "All routing rules cleared";
}

void AudioRouter::restorePreset(SceneState scene) {
    applyScenePreset(scene);
}

bool AudioRouter::savePreset(const std::string& presetName, const std::filesystem::path& file) const {
    ofJson json;
    if (std::filesystem::exists(file)) {
        json = ofLoadJson(file.string());
    }
    if (!json.contains("presets") || !json["presets"].is_object()) {
        json["presets"] = ofJson::object();
    }
    json["presets"][presetName] = ofJson::array();
    for (std::size_t idx = 0; idx < rules_.size(); ++idx) {
        const auto& rule = rules_[idx];
        ofJson entry;
        entry["channel"] = static_cast<int>(idx);
        entry["source"] = static_cast<int>(rule.source);
        entry["mode"] = static_cast<int>(rule.mixMode);
        entry["gainDb"] = rule.gainDb;
        entry["pan"] = rule.panLR;
        json["presets"][presetName].push_back(entry);
    }

    std::error_code ec;
    if (!file.parent_path().empty()) {
        std::filesystem::create_directories(file.parent_path(), ec);
    }
    if (!ofSavePrettyJson(file.string(), json)) {
        ofLogError("AudioRouter") << "Failed to save routing preset to " << file;
        return false;
    }
    ofLogNotice("AudioRouter") << "Routing preset '" << presetName << "' saved to " << file;
    return true;
}

bool AudioRouter::loadPreset(const std::string& presetName, const std::filesystem::path& file) {
    if (!std::filesystem::exists(file)) {
        ofLogError("AudioRouter") << "Preset file not found: " << file;
        return false;
    }

    const ofJson json = ofLoadJson(file.string());
    if (!json.contains("presets") || !json["presets"].contains(presetName)) {
        ofLogError("AudioRouter") << "Preset '" << presetName << "' not defined in " << file;
        return false;
    }

    clearRules();
    for (const auto& entry : json["presets"][presetName]) {
        if (!entry.contains("channel")) {
            continue;
        }
        const auto channelIdx = static_cast<std::size_t>(entry.value("channel", 0));
        if (channelIdx >= rules_.size()) {
            continue;
        }

        RoutingRule rule;
        rule.source = static_cast<ParticipantId>(entry.value("source", static_cast<int>(ParticipantId::None)));
        rule.mixMode = static_cast<MixMode>(entry.value("mode", static_cast<int>(MixMode::Silent)));
        rule.gainDb = entry.value("gainDb", kDefaultSilentGainDb);
        rule.panLR = entry.value("pan", 0.0f);
        rules_[channelIdx] = rule;
    }

    ofLogNotice("AudioRouter") << "Routing preset '" << presetName << "' loaded from " << file;
    return true;
}

std::size_t AudioRouter::activeRuleCount() const {
    std::size_t count = 0;
    for (const auto& rule : rules_) {
        if (rule.source != ParticipantId::None && rule.mixMode != MixMode::Silent) {
            ++count;
        }
    }
    return count;
}

void AudioRouter::applyScenePreset(SceneState scene) {
    clearRules();

    const auto assignRule = [&](OutputChannel channel, ParticipantId source, MixMode mixMode,
                                float gainDb, float pan) {
        RoutingRule rule;
        rule.source = source;
        rule.mixMode = mixMode;
        rule.gainDb = gainDb;
        rule.panLR = pan;
        rules_[static_cast<std::size_t>(channel)] = rule;
    };

    switch (scene) {
        case SceneState::Idle:
        case SceneState::Start:
            break;
        case SceneState::FirstPhase:
            assignRule(OutputChannel::CH1_HeadphoneLeft, ParticipantId::Participant1, MixMode::Self, 0.0f, -1.0f);
            assignRule(OutputChannel::CH2_HeadphoneRight, ParticipantId::Participant2, MixMode::Self, 0.0f, 1.0f);
            assignRule(OutputChannel::CH3_HapticP1, ParticipantId::Participant1, MixMode::Haptic, 0.0f, 0.0f);
            assignRule(OutputChannel::CH4_HapticP2, ParticipantId::Participant2, MixMode::Haptic, 0.0f, 0.0f);
            break;
        case SceneState::Exchange:
            assignRule(OutputChannel::CH1_HeadphoneLeft, ParticipantId::Participant2, MixMode::Partner, 0.0f, -1.0f);
            assignRule(OutputChannel::CH2_HeadphoneRight, ParticipantId::Participant1, MixMode::Partner, 0.0f, 1.0f);
            assignRule(OutputChannel::CH3_HapticP1, ParticipantId::Participant1, MixMode::Haptic, 0.0f, 0.0f);
            assignRule(OutputChannel::CH4_HapticP2, ParticipantId::Participant2, MixMode::Haptic, 0.0f, 0.0f);
            break;
        case SceneState::Mixed:
        case SceneState::End:
            assignRule(OutputChannel::CH1_HeadphoneLeft, ParticipantId::Participant1, MixMode::Self, -3.0f, -0.5f);
            assignRule(OutputChannel::CH2_HeadphoneRight, ParticipantId::Participant2, MixMode::Self, -3.0f, 0.5f);
            assignRule(OutputChannel::CH3_HapticP1, ParticipantId::Participant1, MixMode::Haptic, 0.0f, 0.0f);
            assignRule(OutputChannel::CH4_HapticP2, ParticipantId::Participant2, MixMode::Haptic, 0.0f, 0.0f);
            break;
    }

    ofLogNotice("AudioRouter") << "Scene preset applied: " << sceneStateToString(scene);
}

void AudioRouter::route(const std::array<float, 2>& headphoneInput,
                        const std::array<float, 2>& inputEnvelopes,
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
                sample = headphoneInput[*participant];
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
    const auto idx = participantIndex(id);
    if (!idx) {
        return 0.0f;
    }

    const double phaseIncrement = static_cast<double>(kHapticFrequencyHz) /
                                  std::max(1.0, static_cast<double>(sampleRateHz_));
    double phase = hapticPhase_[*idx];
    const float sineSample = std::sin(static_cast<float>(phase * glm::two_pi<double>()));
    phase += phaseIncrement;
    if (phase >= 1.0) {
        phase -= 1.0;
    }
    hapticPhase_[*idx] = phase;

    const float clampedEnvelope = std::clamp(envelope, 0.0f, 1.0f);
    return sineSample * clampedEnvelope * kHapticGain;
}

void AudioRouter::clearRules() {
    for (auto& rule : rules_) {
        rule = makeSilentRule();
    }
}

} // namespace knot::audio
