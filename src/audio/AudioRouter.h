#pragma once

#include "ParticipantId.h"
#include "HrtfProcessor.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>
#include <memory>

// Forward declaration
enum class SceneState : std::uint8_t;

namespace knot::audio {

enum class OutputChannel : std::uint8_t {
    // Participant 1 Headphone (Stereo)
    CH1_HeadphoneP1_Left = 0,
    CH2_HeadphoneP1_Right = 1,
    // Participant 2 Headphone (Stereo)
    CH3_HeadphoneP2_Left = 2,
    CH4_HeadphoneP2_Right = 3,
    // Haptics
    CH5_HapticP1 = 4,
    CH6_HapticP2 = 5
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

// 3D空間音源の位置情報
struct SoundSourcePosition {
    float distance = 1.0f;      // 距離 (m)
    float azimuth = 0.0f;       // 方位角 (度): -180 to +180, 0 = 正面
    float elevation = 0.0f;     // 仰角 (度): -90 to +90, 0 = 水平
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
    
    // Dynamic panning update based on scene state and time
    void updateDynamicPanning(SceneState scene, double timeInState, float transitionBlend);
    
    // 空間音響: 音源位置の更新（Exchangeフェーズ用）
    void updateSpatialAudio(SceneState scene, double timeInState);
    
    // 音源位置の取得
    SoundSourcePosition getSoundSourcePosition(ParticipantId participant, ParticipantId listener) const;

    void route(const std::array<float, 2>& headphoneInput,
               const std::array<float, 2>& inputEnvelopes,
               std::array<float, 6>& outputBuffer);

private:
    std::array<RoutingRule, 6> rules_{};
    float sampleRateHz_ = 48000.0f;
    std::array<double, 2> hapticPhase_{{0.0, 0.0}};
    
    // Current dynamic pan values (updated by updateDynamicPanning)
    std::array<float, 2> currentPanValues_{0.0f, 0.0f};  // [P1, P2]
    
    // 空間音響: 音源位置（[listener][source]）
    // listener: 0=P1, 1=P2, source: 0=P1, 1=P2
    std::array<std::array<SoundSourcePosition, 2>, 2> soundSourcePositions_;
    
    // 距離減衰とスペクトラル変化用のフィルター状態（簡易実装）
    std::array<std::array<float, 2>, 2> lowpassState_;  // [listener][source]
    
    // HRTFプロセッサ（将来の拡張用）
    // 現在は簡易的な空間音響を使用し、将来的にHRTFを統合可能
    std::array<std::array<std::unique_ptr<HrtfProcessor>, 2>, 2> hrtfProcessors_;  // [listener][source]
    bool useHrtf_ = false;  // HRTFを使用するか（現在はfalse）
    
    // Current scene state (for Mixed scene mixing logic)
    // Note: Initialized in setup() or clearRules()
    SceneState currentScene_;
    static const float kMixedPartnerPan;  // Slight pan for partner's heartbeat in Mixed scene
    static const double kExchangeDuration;  // Exchangeフェーズの交換時間（秒）
    static const float kMinDistance;       // 最小距離 (m)
    static const float kMaxDistance;       // 最大距離 (m)

    void clearRules();
    float generateHapticSample(float envelope, ParticipantId id);
    void applyStereoPan(float sample, float panLR, float& leftOut, float& rightOut);
    
    // 空間音響処理
    void applySpatialAudio(float sample, const SoundSourcePosition& position, 
                          float& leftOut, float& rightOut, 
                          std::size_t listenerIdx, std::size_t sourceIdx);
    float calculateDistanceAttenuation(float distance) const;
    float calculateSpatialPan(float azimuth) const;
    void applyDistanceFiltering(float& sample, float distance, std::size_t listenerIdx, std::size_t sourceIdx);
};

} // namespace knot::audio
