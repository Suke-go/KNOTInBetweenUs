#include "AudioRouter.h"

#include "SceneController.h"
#include "Utility.h"
#include "HrtfProcessor.h"

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

} // namespace

// Static member definitions
const float AudioRouter::kMixedPartnerPan = 0.3f;
const double AudioRouter::kExchangeDuration = 20.0;
const float AudioRouter::kMinDistance = 1.0f;
const float AudioRouter::kMaxDistance = 3.0f;

namespace {

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
    currentScene_ = SceneState::Idle;  // Initialize scene state
    clearRules();
    
    // 空間音響の初期化
    for (auto& listenerPos : soundSourcePositions_) {
        for (auto& sourcePos : listenerPos) {
            sourcePos.distance = kMinDistance;
            sourcePos.azimuth = 0.0f;
            sourcePos.elevation = 0.0f;
        }
    }
    for (auto& listenerState : lowpassState_) {
        listenerState.fill(0.0f);
    }
    
    // HRTFプロセッサの初期化（将来の拡張用）
    // 現在は使用しない（useHrtf_ = false）
    for (auto& listenerProcessors : hrtfProcessors_) {
        for (auto& processor : listenerProcessors) {
            processor = std::make_unique<HrtfProcessor>();
            // 将来的にHRTFデータを読み込む場合:
            // processor->loadHrtfData("hrir/mit_kemar_normal_pinna.sofa");
        }
    }
    useHrtf_ = false;  // 現在は簡易的な空間音響を使用
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
    currentScene_ = scene;
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
            // Idle/Startシーンでもピンクノイズと心拍を出力するためのルーティングを設定
            // AudioPipelineが生成するピンクノイズ（入力信号がない場合でも）と心拍信号を出力する
            assignRule(OutputChannel::CH1_HeadphoneP1_Left, ParticipantId::Participant1, MixMode::Self, 0.0f, 0.0f);
            assignRule(OutputChannel::CH2_HeadphoneP1_Right, ParticipantId::Participant1, MixMode::Self, 0.0f, 0.0f);
            // Participant 2: own heartbeat (stereo)
            assignRule(OutputChannel::CH3_HeadphoneP2_Left, ParticipantId::Participant2, MixMode::Self, 0.0f, 0.0f);
            assignRule(OutputChannel::CH4_HeadphoneP2_Right, ParticipantId::Participant2, MixMode::Self, 0.0f, 0.0f);
            // Haptics: increased gain for better haptic response
            assignRule(OutputChannel::CH5_HapticP1, ParticipantId::Participant1, MixMode::Haptic, 6.0f, 0.0f);
            assignRule(OutputChannel::CH6_HapticP2, ParticipantId::Participant2, MixMode::Haptic, 6.0f, 0.0f);
            // Note: Panning will be applied dynamically via updateDynamicPanning()
            ofLogNotice("AudioRouter") << "Idle/Start routing applied: P1->CH1/CH2, P2->CH3/CH4 (pink noise and heartbeat enabled)";
            break;
            
        case SceneState::FirstPhase:
            // Participant 1: own heartbeat (stereo)
            // Note: AudioPipeline already applies -15dB, so we use 0.0dB here (no additional attenuation)
            // If audio is too quiet, we can increase this (e.g., +3dB to +6dB)
            assignRule(OutputChannel::CH1_HeadphoneP1_Left, ParticipantId::Participant1, MixMode::Self, 0.0f, 0.0f);
            assignRule(OutputChannel::CH2_HeadphoneP1_Right, ParticipantId::Participant1, MixMode::Self, 0.0f, 0.0f);
            // Participant 2: own heartbeat (stereo)
            assignRule(OutputChannel::CH3_HeadphoneP2_Left, ParticipantId::Participant2, MixMode::Self, 0.0f, 0.0f);
            assignRule(OutputChannel::CH4_HeadphoneP2_Right, ParticipantId::Participant2, MixMode::Self, 0.0f, 0.0f);
            // Haptics: increased gain for better haptic response
            assignRule(OutputChannel::CH5_HapticP1, ParticipantId::Participant1, MixMode::Haptic, 6.0f, 0.0f);
            assignRule(OutputChannel::CH6_HapticP2, ParticipantId::Participant2, MixMode::Haptic, 6.0f, 0.0f);
            // Note: Panning will be applied dynamically via updateDynamicPanning()
            ofLogNotice("AudioRouter") << "FirstPhase routing applied: P1->CH1/CH2, P2->CH3/CH4, gain=0.0dB";
            break;
            
        case SceneState::Exchange:
            // Participant 1: partner's heartbeat (stereo)
            assignRule(OutputChannel::CH1_HeadphoneP1_Left, ParticipantId::Participant2, MixMode::Partner, 0.0f, 0.0f);
            assignRule(OutputChannel::CH2_HeadphoneP1_Right, ParticipantId::Participant2, MixMode::Partner, 0.0f, 0.0f);
            // Participant 2: partner's heartbeat (stereo)
            assignRule(OutputChannel::CH3_HeadphoneP2_Left, ParticipantId::Participant1, MixMode::Partner, 0.0f, 0.0f);
            assignRule(OutputChannel::CH4_HeadphoneP2_Right, ParticipantId::Participant1, MixMode::Partner, 0.0f, 0.0f);
            // Haptics: still own heartbeat, increased gain for better haptic response
            assignRule(OutputChannel::CH5_HapticP1, ParticipantId::Participant1, MixMode::Haptic, 6.0f, 0.0f);
            assignRule(OutputChannel::CH6_HapticP2, ParticipantId::Participant2, MixMode::Haptic, 6.0f, 0.0f);
            // Note: Panning will be applied dynamically via updateDynamicPanning()
            break;
            
        case SceneState::Mixed:
            // Mixed scene: each participant hears both their own and partner's heartbeat
            // Participant 1: own heartbeat (center) + partner's heartbeat (slight right)
            assignRule(OutputChannel::CH1_HeadphoneP1_Left, ParticipantId::Participant1, MixMode::Self, -3.0f, 0.0f);
            assignRule(OutputChannel::CH2_HeadphoneP1_Right, ParticipantId::Participant1, MixMode::Self, -3.0f, 0.0f);
            // Also add partner's heartbeat (will be mixed in route() function)
            // Note: We need to modify route() to support multiple sources per channel for Mixed scene
            // For now, we'll route partner's heartbeat to the same channels with different pan
            // Participant 2: own heartbeat (center) + partner's heartbeat (slight left)
            assignRule(OutputChannel::CH3_HeadphoneP2_Left, ParticipantId::Participant2, MixMode::Self, -3.0f, 0.0f);
            assignRule(OutputChannel::CH4_HeadphoneP2_Right, ParticipantId::Participant2, MixMode::Self, -3.0f, 0.0f);
            // Haptics: increased gain for better haptic response
            assignRule(OutputChannel::CH5_HapticP1, ParticipantId::Participant1, MixMode::Haptic, 6.0f, 0.0f);
            assignRule(OutputChannel::CH6_HapticP2, ParticipantId::Participant2, MixMode::Haptic, 6.0f, 0.0f);
            // Note: Mixed scene mixing will be handled in route() function
            break;
            
        case SceneState::End:
            // Similar to Mixed but with fade
            assignRule(OutputChannel::CH1_HeadphoneP1_Left, ParticipantId::Participant1, MixMode::Self, -6.0f, 0.0f);
            assignRule(OutputChannel::CH2_HeadphoneP1_Right, ParticipantId::Participant1, MixMode::Self, -6.0f, 0.0f);
            assignRule(OutputChannel::CH3_HeadphoneP2_Left, ParticipantId::Participant2, MixMode::Self, -6.0f, 0.0f);
            assignRule(OutputChannel::CH4_HeadphoneP2_Right, ParticipantId::Participant2, MixMode::Self, -6.0f, 0.0f);
            // Haptics: increased gain for better haptic response
            assignRule(OutputChannel::CH5_HapticP1, ParticipantId::Participant1, MixMode::Haptic, 6.0f, 0.0f);
            assignRule(OutputChannel::CH6_HapticP2, ParticipantId::Participant2, MixMode::Haptic, 6.0f, 0.0f);
            break;
    }

    ofLogNotice("AudioRouter") << "Scene preset applied: " << sceneStateToString(scene);
}

void AudioRouter::applyStereoPan(float sample, float panLR, float& leftOut, float& rightOut) {
    // Constant Power Panning
    // panLR: -1.0 (left) to +1.0 (right), 0.0 = center
    const float panAngle = (panLR + 1.0f) * glm::pi<float>() / 4.0f;  // -1.0→0, 0.0→PI/4, +1.0→PI/2
    const float panLeft = std::cos(panAngle);
    const float panRight = std::sin(panAngle);
    
    leftOut = sample * panLeft;
    rightOut = sample * panRight;
}

void AudioRouter::route(const std::array<float, 2>& headphoneInput,
                        const std::array<float, 2>& inputEnvelopes,
                        std::array<float, 6>& outputBuffer) {
    outputBuffer.fill(0.0f);

    // Special handling for Mixed scene: each participant hears both own and partner's heartbeat
    if (currentScene_ == SceneState::Mixed) {
        // Participant 1 headphone (CH1/CH2): own heartbeat (center) + partner's heartbeat (slight right)
        {
            // Own heartbeat (center, pan = 0.0)
            const float ownSample = headphoneInput[0];
            const float ownGain = dbToLinear(-3.0f);
            float leftOwn = 0.0f, rightOwn = 0.0f;
            applyStereoPan(ownSample * ownGain, 0.0f, leftOwn, rightOwn);
            outputBuffer[0] += leftOwn;   // CH1: P1 Left
            outputBuffer[1] += rightOwn;  // CH2: P1 Right
            
            // Partner's heartbeat (slight right, pan = +0.3)
            const float partnerSample = headphoneInput[1];
            const float partnerGain = dbToLinear(-3.0f);
            float leftPartner = 0.0f, rightPartner = 0.0f;
            applyStereoPan(partnerSample * partnerGain, kMixedPartnerPan, leftPartner, rightPartner);
            outputBuffer[0] += leftPartner;   // CH1: P1 Left
            outputBuffer[1] += rightPartner;  // CH2: P1 Right
        }
        
        // Participant 2 headphone (CH3/CH4): own heartbeat (center) + partner's heartbeat (slight left)
        {
            // Own heartbeat (center, pan = 0.0)
            const float ownSample = headphoneInput[1];
            const float ownGain = dbToLinear(-3.0f);
            float leftOwn = 0.0f, rightOwn = 0.0f;
            applyStereoPan(ownSample * ownGain, 0.0f, leftOwn, rightOwn);
            outputBuffer[2] += leftOwn;   // CH3: P2 Left
            outputBuffer[3] += rightOwn;  // CH4: P2 Right
            
            // Partner's heartbeat (slight left, pan = -0.3)
            const float partnerSample = headphoneInput[0];
            const float partnerGain = dbToLinear(-3.0f);
            float leftPartner = 0.0f, rightPartner = 0.0f;
            applyStereoPan(partnerSample * partnerGain, -kMixedPartnerPan, leftPartner, rightPartner);
            outputBuffer[2] += leftPartner;   // CH3: P2 Left
            outputBuffer[3] += rightPartner;  // CH4: P2 Right
        }
    } else if (currentScene_ == SceneState::Exchange) {
        // Exchangeフェーズ: 空間音響を使用
        // Participant 1 headphone (CH1/CH2)
        {
            // 自分の心音（Participant1）
            const float ownSample = headphoneInput[0];
            const float ownGain = dbToLinear(-3.0f);
            const auto ownPos = getSoundSourcePosition(ParticipantId::Participant1, ParticipantId::Participant1);
            float leftOwn = 0.0f, rightOwn = 0.0f;
            applySpatialAudio(ownSample * ownGain, ownPos, leftOwn, rightOwn, 0, 0);
            outputBuffer[0] += leftOwn;   // CH1: P1 Left
            outputBuffer[1] += rightOwn;  // CH2: P1 Right
            
            // パートナーの心音（Participant2）
            const float partnerSample = headphoneInput[1];
            const float partnerGain = dbToLinear(-3.0f);
            const auto partnerPos = getSoundSourcePosition(ParticipantId::Participant2, ParticipantId::Participant1);
            float leftPartner = 0.0f, rightPartner = 0.0f;
            applySpatialAudio(partnerSample * partnerGain, partnerPos, leftPartner, rightPartner, 0, 1);
            outputBuffer[0] += leftPartner;   // CH1: P1 Left
            outputBuffer[1] += rightPartner;  // CH2: P1 Right
        }
        
        // Participant 2 headphone (CH3/CH4)
        {
            // 自分の心音（Participant2）
            const float ownSample = headphoneInput[1];
            const float ownGain = dbToLinear(-3.0f);
            const auto ownPos = getSoundSourcePosition(ParticipantId::Participant2, ParticipantId::Participant2);
            float leftOwn = 0.0f, rightOwn = 0.0f;
            applySpatialAudio(ownSample * ownGain, ownPos, leftOwn, rightOwn, 1, 1);
            outputBuffer[2] += leftOwn;   // CH3: P2 Left
            outputBuffer[3] += rightOwn;  // CH4: P2 Right
            
            // パートナーの心音（Participant1）
            const float partnerSample = headphoneInput[0];
            const float partnerGain = dbToLinear(-3.0f);
            const auto partnerPos = getSoundSourcePosition(ParticipantId::Participant1, ParticipantId::Participant2);
            float leftPartner = 0.0f, rightPartner = 0.0f;
            applySpatialAudio(partnerSample * partnerGain, partnerPos, leftPartner, rightPartner, 1, 0);
            outputBuffer[2] += leftPartner;   // CH3: P2 Left
            outputBuffer[3] += rightPartner;  // CH4: P2 Right
        }
    } else {
        // Normal routing for other scenes (FirstPhase, Idle, Start, End)
        // Process headphone outputs (CH1-4) with stereo panning
        static std::size_t routeCallCount = 0;
        routeCallCount++;
        
        for (std::size_t ch = 0; ch < 4; ++ch) {
            const auto& rule = rules_[ch];
            const auto participant = participantIndex(rule.source);
            if (!participant || rule.mixMode == MixMode::Silent) {
                continue;
            }

            float sample = 0.0f;
            switch (rule.mixMode) {
                case MixMode::Self:
                case MixMode::Partner:
                    sample = headphoneInput[*participant];
                    // Debug: Log audio levels occasionally for FirstPhase
                    if (currentScene_ == SceneState::FirstPhase && routeCallCount % 4800 == 0) {
                        const float absSample = std::fabs(sample);
                        if (absSample > 0.0001f) {
                            ofLogNotice("AudioRouter::route") << "FirstPhase CH" << (ch+1) 
                                << " | Participant " << (*participant + 1) 
                                << " | Sample: " << sample 
                                << " | Envelope: " << inputEnvelopes[*participant]
                                << " | Pan: " << currentPanValues_[*participant];
                        }
                    }
                    break;
                case MixMode::Haptic:
                    // Should not happen for headphone channels, but handle gracefully
                    sample = generateHapticSample(inputEnvelopes[*participant], rule.source);
                    break;
                case MixMode::Silent:
                default:
                    sample = 0.0f;
                    break;
            }

            const float gainLinear = dbToLinear(rule.gainDb);
            sample *= gainLinear;
            
            // Apply dynamic panning (use currentPanValues_ instead of rule.panLR)
            const float panLR = currentPanValues_[*participant];
            
            // Determine left/right channels based on channel index
            // CH1/CH2 = P1 Left/Right, CH3/CH4 = P2 Left/Right
            float leftSample = 0.0f;
            float rightSample = 0.0f;
            applyStereoPan(sample, panLR, leftSample, rightSample);
            
            if (ch < 2) {
                // Participant 1: CH1 = Left, CH2 = Right
                outputBuffer[ch] += (ch == 0) ? leftSample : rightSample;
            } else {
                // Participant 2: CH3 = Left, CH4 = Right
                outputBuffer[ch] += (ch == 2) ? leftSample : rightSample;
            }
        }
    }
    
    // Process haptic outputs (CH5-6) - no panning
    for (std::size_t ch = 4; ch < 6; ++ch) {
        const auto& rule = rules_[ch];
        const auto participant = participantIndex(rule.source);
        if (!participant || rule.mixMode != MixMode::Haptic) {
            continue;
        }
        
        const float sample = generateHapticSample(inputEnvelopes[*participant], rule.source);
        const float gainLinear = dbToLinear(rule.gainDb);
        outputBuffer[ch] = sample * gainLinear;
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

void AudioRouter::updateDynamicPanning(SceneState scene, double timeInState, float transitionBlend) {
    constexpr double kFirstPhasePanDuration = 15.0;  // 15 seconds for panning in FirstPhase
    
    // Calculate target pan values for current scene
    float targetPanP1 = 0.0f;
    float targetPanP2 = 0.0f;
    
    switch (scene) {
        case SceneState::Idle:
        case SceneState::Start:
            // Center position
            targetPanP1 = 0.0f;
            targetPanP2 = 0.0f;
            break;
            
        case SceneState::FirstPhase: {
            // P1 moves to right (+1.0), P2 moves to left (-1.0) over 15 seconds
            const float panProgress = std::clamp(static_cast<float>(timeInState / kFirstPhasePanDuration), 0.0f, 1.0f);
            // Smooth interpolation (ease in-out)
            const float easedProgress = 0.5f - 0.5f * std::cos(panProgress * glm::pi<float>());
            targetPanP1 = easedProgress;      // P1: 0.0 → +1.0 (right)
            targetPanP2 = -easedProgress;     // P2: 0.0 → -1.0 (left)
            break;
        }
        
        case SceneState::Exchange:
            // Exchangeフェーズでは空間音響を使用するため、パンニングはupdateSpatialAudioで処理
            // ここでは初期値を設定（実際の処理はapplySpatialAudioで行われる）
            targetPanP1 = 0.0f;
            targetPanP2 = 0.0f;
            break;
            
        case SceneState::Mixed:
            // Own heartbeat: center (0.0) - panning is handled in route() for Mixed scene
            // Note: In Mixed scene, each participant's own heartbeat is at center,
            // and partner's heartbeat is mixed separately with slight pan (±0.3)
            targetPanP1 = 0.0f;    // P1's own heartbeat: center
            targetPanP2 = 0.0f;    // P2's own heartbeat: center
            break;
            
        case SceneState::End:
            // Fade to center
            targetPanP1 = 0.0f;
            targetPanP2 = 0.0f;
            break;
    }
    
    // Apply transition blend if transitioning
    if (transitionBlend > 0.0f && transitionBlend < 1.0f) {
        // During transition, interpolate between current and target pan values
        // Smooth interpolation for natural transition
        const float easedBlend = 0.5f - 0.5f * std::cos(transitionBlend * glm::pi<float>());
        currentPanValues_[0] = currentPanValues_[0] + (targetPanP1 - currentPanValues_[0]) * easedBlend;
        currentPanValues_[1] = currentPanValues_[1] + (targetPanP2 - currentPanValues_[1]) * easedBlend;
    } else {
        // No transition: set directly to target
        currentPanValues_[0] = targetPanP1;
        currentPanValues_[1] = targetPanP2;
    }
}

void AudioRouter::updateSpatialAudio(SceneState scene, double timeInState) {
    if (scene != SceneState::Exchange) {
        // Exchangeフェーズ以外は通常のパンニングを使用
        return;
    }
    
    // Exchangeフェーズ: 20秒間かけて音源が移動・交換
    const float exchangeProgress = std::clamp(static_cast<float>(timeInState / kExchangeDuration), 0.0f, 1.0f);
    const float easedProgress = 0.5f - 0.5f * std::cos(exchangeProgress * glm::pi<float>());  // イーズイン・アウト
    
    // 参加者1の視点
    // 自分の心音: 中央(1.0m, 0°) → 右側遠方(3.0m, +90°)
    soundSourcePositions_[0][0].distance = kMinDistance + (kMaxDistance - kMinDistance) * easedProgress;
    soundSourcePositions_[0][0].azimuth = 90.0f * easedProgress;  // 0° → +90° (右側)
    soundSourcePositions_[0][0].elevation = 0.0f;
    
    // パートナーの心音: 遠方(3.0m, -90°) → 中央(1.0m, 0°)
    soundSourcePositions_[0][1].distance = kMaxDistance - (kMaxDistance - kMinDistance) * easedProgress;
    soundSourcePositions_[0][1].azimuth = -90.0f * (1.0f - easedProgress);  // -90° → 0° (左側から中央へ)
    soundSourcePositions_[0][1].elevation = 0.0f;
    
    // 参加者2の視点
    // 自分の心音: 中央(1.0m, 0°) → 左側遠方(3.0m, -90°)
    soundSourcePositions_[1][1].distance = kMinDistance + (kMaxDistance - kMinDistance) * easedProgress;
    soundSourcePositions_[1][1].azimuth = -90.0f * easedProgress;  // 0° → -90° (左側)
    soundSourcePositions_[1][1].elevation = 0.0f;
    
    // パートナーの心音: 遠方(3.0m, +90°) → 中央(1.0m, 0°)
    soundSourcePositions_[1][0].distance = kMaxDistance - (kMaxDistance - kMinDistance) * easedProgress;
    soundSourcePositions_[1][0].azimuth = 90.0f * (1.0f - easedProgress);  // +90° → 0° (右側から中央へ)
    soundSourcePositions_[1][0].elevation = 0.0f;
}

SoundSourcePosition AudioRouter::getSoundSourcePosition(ParticipantId source, ParticipantId listener) const {
    const auto sourceIdx = participantIndex(source);
    const auto listenerIdx = participantIndex(listener);
    if (!sourceIdx || !listenerIdx) {
        SoundSourcePosition defaultPos;
        defaultPos.distance = kMinDistance;
        defaultPos.azimuth = 0.0f;
        defaultPos.elevation = 0.0f;
        return defaultPos;
    }
    return soundSourcePositions_[*listenerIdx][*sourceIdx];
}

float AudioRouter::calculateDistanceAttenuation(float distance) const {
    // 逆二乗則による距離減衰（最小距離での基準を1.0とする）
    const float refDistance = kMinDistance;
    const float attenuation = (refDistance * refDistance) / (distance * distance);
    // 最小値と最大値を制限
    return std::max(0.1f, std::min(1.0f, attenuation));
}

float AudioRouter::calculateSpatialPan(float azimuth) const {
    // 方位角を-1.0 (左) から +1.0 (右) のパン値に変換
    // -180° → -1.0, 0° → 0.0, +180° → +1.0
    return std::sin(azimuth * glm::pi<float>() / 180.0f);
}

void AudioRouter::applyDistanceFiltering(float& sample, float distance, std::size_t listenerIdx, std::size_t sourceIdx) {
    // 簡易的なローパスフィルター（距離が遠くなるほど高周波が減衰）
    // 距離に応じたカットオフ周波数の変化をシミュレート
    const float maxCutoff = 20000.0f;  // 最大カットオフ周波数 (Hz)
    const float minCutoff = 2000.0f;   // 最小カットオフ周波数 (Hz)
    
    // 距離に応じたカットオフ周波数の計算
    const float distanceNormalized = (distance - kMinDistance) / (kMaxDistance - kMinDistance);
    const float cutoffFreq = maxCutoff - (maxCutoff - minCutoff) * distanceNormalized;
    
    // 簡易的な一次ローパスフィルター（1次IIR）
    // サンプルレートが48000Hzの場合の係数計算
    const float dt = 1.0f / sampleRateHz_;
    const float rc = 1.0f / (2.0f * glm::pi<float>() * cutoffFreq);
    const float alpha = dt / (rc + dt);
    
    float& state = lowpassState_[listenerIdx][sourceIdx];
    state = state + alpha * (sample - state);
    sample = state;
}

void AudioRouter::applySpatialAudio(float sample, const SoundSourcePosition& position,
                                    float& leftOut, float& rightOut,
                                    std::size_t listenerIdx, std::size_t sourceIdx) {
    if (useHrtf_ && hrtfProcessors_[listenerIdx][sourceIdx] && 
        hrtfProcessors_[listenerIdx][sourceIdx]->isLoaded()) {
        // HRTFを使用する場合（将来の拡張）
        hrtfProcessors_[listenerIdx][sourceIdx]->setDirection(position.azimuth, position.elevation);
        hrtfProcessors_[listenerIdx][sourceIdx]->processSpatial(
            sample, position.azimuth, position.distance, leftOut, rightOut);
    } else {
        // 簡易的な空間音響処理（現在の実装）
        // 距離減衰
        const float distanceAttenuation = calculateDistanceAttenuation(position.distance);
        sample *= distanceAttenuation;
        
        // 距離に応じたスペクトラル変化（ローパスフィルター）
        applyDistanceFiltering(sample, position.distance, listenerIdx, sourceIdx);
        
        // 方位角に基づくパンニング
        const float panLR = calculateSpatialPan(position.azimuth);
        applyStereoPan(sample, panLR, leftOut, rightOut);
    }
}

void AudioRouter::clearRules() {
    for (auto& rule : rules_) {
        rule = makeSilentRule();
    }
    currentPanValues_.fill(0.0f);
    // Note: currentScene_ is not reset here - it should be set by applyScenePreset()
    
    // 空間音響のリセット
    for (auto& listenerPos : soundSourcePositions_) {
        for (auto& sourcePos : listenerPos) {
            sourcePos.distance = kMinDistance;
            sourcePos.azimuth = 0.0f;
            sourcePos.elevation = 0.0f;
        }
    }
    
    // HRTFプロセッサのリセット
    for (auto& listenerProcessors : hrtfProcessors_) {
        for (auto& processor : listenerProcessors) {
            if (processor) {
                processor->reset();
            }
        }
    }
}

} // namespace knot::audio
