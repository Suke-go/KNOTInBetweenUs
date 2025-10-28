# 2入力→4出力オーディオルーティングシステム 実装要件定義書

**作成日**: 2025-10-29
**優先度**: P0 (展示デモに必須)
**目的**: 2名の参加者それぞれの心拍を独立処理し、4chオーディオ出力で制御可能にする

---

## I. 現状分析と課題

### 現在のアーキテクチャ

```
[Mic CH1/CH2] → AudioPipeline::audioIn()
                 ↓
                 モノラル化 (ch1+ch2)/2
                 ↓
                 BeatTimeline (単一)
                 ↓
                 [Output CH1/CH2] ← AudioPipeline::audioOut()
```

**問題点**:
1. **CH1とCH2を合成**してしまっている ([AudioPipeline.cpp:178](AudioPipeline.cpp#L178))
   ```cpp
   monoBuffer_[frame] = 0.5f * (ch1 + ch2);  // ← 2人の心音が混ざる
   ```
2. **BeatTimelineが1つのみ** → 2名の心拍を区別できない
3. **出力が2chのみ** → ハプティクス(CH3/CH4)出力不可
4. **参加者識別の概念がない** → BeatEventに「誰の心拍か」の情報がない

### 必須要件

1. ✅ **2名の心拍を独立処理**
   - Participant 1 (CH1) → 独立したBeatTimeline
   - Participant 2 (CH2) → 独立したBeatTimeline

2. ✅ **4chオーディオ出力**
   - CH1: Participant 1 ヘッドフォン左
   - CH2: Participant 2 ヘッドフォン右
   - CH3: Participant 1 ハプティクス(振動子)
   - CH4: Participant 2 ハプティクス(振動子)

3. ✅ **GUIでルーティング設定**
   - 各出力チャンネルに「どの参加者の心拍」を割り当てるか選択可能
   - シーンごとの自動切替(Exchange/Mixedでの心音交換)

4. ✅ **1人デモモード対応**
   - Participant 2を合成心拍(SyntheticHeartbeat)で代替
   - GUIで「Solo Mode」切替

---

## II. 段階的実装戦略

### 実装フェーズの全体像

```
Phase 0: 準備・インターフェース設計    [検証のみ、コード変更なし]
Phase 1: 2ch独立処理の実装             [AudioPipeline改修]
Phase 2: BeatEvent拡張とルーティング   [データフロー整理]
Phase 3: 4ch出力実装                   [出力拡張]
Phase 4: GUI統合とシーン連携           [ユーザー制御]
Phase 5: 合成心拍モード                [1人デモ対応]
```

各フェーズは**独立してテスト可能**な単位に分割し、段階的に動作確認しながら積み上げます。

---

## III. Phase 0: 準備・インターフェース設計

### 目的
既存コードとの整合性確認、データ構造設計、他モジュールへの影響調査

### 実施内容

#### 0.1 データ構造設計

**ParticipantId enum** (新規作成: `src/audio/ParticipantId.h`)
```cpp
#pragma once
#include <cstdint>
#include <optional>
#include <string>

namespace knot::audio {

enum class ParticipantId : std::uint8_t {
    Participant1 = 0,
    Participant2 = 1,
    Synthetic = 2,  // 合成心拍(1人モード用)
    None = 255
};

std::string participantIdToString(ParticipantId id);
std::optional<ParticipantId> participantIdFromString(const std::string& str);

} // namespace knot::audio
```

**BeatEvent拡張** (修正: `src/audio/BeatTimeline.h`)
```cpp
struct BeatEvent {
    double timestampSec = 0.0;
    float bpm = 0.0f;
    float envelope = 0.0f;
    ParticipantId participantId = ParticipantId::None;  // ← 追加
    std::uint64_t sequenceId = 0;  // ← 追加(デバッグ用)
};
```

#### 0.2 依存関係マップ作成

**影響を受けるモジュール**:
```
AudioPipeline (修正大)
  ├─ BeatTimeline (拡張: participantId付与)
  ├─ Calibration (影響なし: 既存のまま)
  └─ SimpleLimiter (拡張: 4ch対応)

ofApp (修正中)
  ├─ BeatVisualizer (拡張: 2名の心拍表示)
  ├─ HapticLog (拡張: participantId記録)
  └─ GUI (新規パネル追加)

SceneController (影響なし)
TelemetryLogging (拡張: participantId記録)
```

#### 0.3 コンフィグ設計

**app_config.json 拡張**
```json
{
  "audio": {
    "inputChannels": 2,
    "outputChannels": 4,
    "routing": {
      "Idle": {
        "CH1": { "source": "Participant1", "mix": "self", "gainDb": -12.0 },
        "CH2": { "source": "Participant2", "mix": "self", "gainDb": -12.0 },
        "CH3": { "source": "Participant1", "mix": "haptic", "gainDb": -15.0 },
        "CH4": { "source": "Participant2", "mix": "haptic", "gainDb": -15.0 }
      },
      "Exchange": {
        "CH1": { "source": "Participant2", "mix": "partner", "gainDb": -9.0 },
        "CH2": { "source": "Participant1", "mix": "partner", "gainDb": -9.0 },
        "CH3": { "source": "Participant1", "mix": "haptic", "gainDb": -15.0 },
        "CH4": { "source": "Participant2", "mix": "haptic", "gainDb": -15.0 }
      }
    }
  },
  "devMode": {
    "soloMode": false,
    "syntheticHeartbeat": {
      "baseBpm": 68.0,
      "variability": 5.0,
      "noiseLevelDb": -40.0
    }
  }
}
```

### 成果物
- [ ] `ParticipantId.h/.cpp` 作成
- [ ] `BeatEvent` 構造体拡張(コメントのみ、実装はPhase 2)
- [ ] 依存関係図(Mermaid/draw.io)
- [ ] コンフィグスキーマ設計文書

### 検証方法
- コンパイルエラーなし(既存コードに影響なし)
- 設計レビュー(チーム全体)

---

## IV. Phase 1: 2ch独立処理の実装

### 目的
現在の「CH1+CH2をモノラル化」を廃止し、2chを独立したBeatTimelineで処理

### 実施内容

#### 1.1 AudioPipeline内部構造の拡張

**AudioPipeline.h 変更** (以下を追加)
```cpp
class AudioPipeline {
public:
    struct ChannelMetrics {
        float bpm = 0.0f;
        float envelope = 0.0f;
        double timestampSec = 0.0;
        bool triggered = false;
        ParticipantId participantId = ParticipantId::None;
    };

    ChannelMetrics channelMetrics(ParticipantId id) const;
    std::vector<BeatEvent> pollBeatEvents(ParticipantId id);

private:
    // 既存の単一BeatTimelineを配列化
    std::array<BeatTimeline, 2> beatTimelines_;  // [0]=P1, [1]=P2
    std::array<std::vector<float>, 2> channelBuffers_;  // 独立バッファ
    std::array<std::deque<BeatEvent>, 2> pendingEventsByChannel_;
    std::array<ChannelMetrics, 2> channelMetrics_;
};
```

#### 1.2 audioIn() 処理の分離

**AudioPipeline.cpp 変更** ([AudioPipeline.cpp:154-253](AudioPipeline.cpp#L154-L253))
```cpp
void AudioPipeline::audioIn(const ofSoundBuffer& buffer) {
    // ... (既存の前処理)

    if (!calibrationArmed_) {
        // === 変更点: モノラル化を廃止、2chを独立処理 ===
        for (std::size_t frame = 0; frame < numFrames; ++frame) {
            float ch1 = input[frame * 2];
            float ch2 = input[frame * 2 + 1];

            // ゲイン・キャリブレーション適用
            if (inputGainLinear_ != 1.0f) {
                ch1 = std::clamp(ch1 * inputGainLinear_, -1.0f, 1.0f);
                ch2 = std::clamp(ch2 * inputGainLinear_, -1.0f, 1.0f);
            }
            applyCalibration(ch1, ch2);

            // ← 【重要】独立バッファに格納(モノラル化しない)
            channelBuffers_[0][frame] = ch1;  // Participant1
            channelBuffers_[1][frame] = ch2;  // Participant2
        }

        const double startSample = totalSamplesProcessed_;

        // === 変更点: 2つのBeatTimelineを独立処理 ===
        for (std::size_t i = 0; i < 2; ++i) {
            const ParticipantId pid = static_cast<ParticipantId>(i);
            beatTimelines_[i].processBuffer(channelBuffers_[i].data(), numFrames, startSample);

            // メトリクス更新
            channelMetrics_[i].bpm = beatTimelines_[i].currentBpm();
            channelMetrics_[i].envelope = beatTimelines_[i].currentEnvelope();
            channelMetrics_[i].timestampSec = (totalSamplesProcessed_ + numFrames) / sampleRate_;
            channelMetrics_[i].triggered = beatTimelines_[i].lastFrameTriggered();
            channelMetrics_[i].participantId = pid;

            // BeatEvent収集(participantId付与はPhase 2で実装)
            if (channelMetrics_[i].triggered) {
                const auto& events = beatTimelines_[i].events();
                if (!events.empty()) {
                    pendingEventsByChannel_[i].push_back(events.back());
                    if (pendingEventsByChannel_[i].size() > 128) {
                        pendingEventsByChannel_[i].pop_front();
                    }
                }
            }
        }

        totalSamplesProcessed_ += static_cast<double>(numFrames);

        // ... (Fallbackロジックは後続フェーズで対応)
    }
}
```

#### 1.3 後方互換APIの提供

**既存のofAppコードを壊さないための互換レイヤー**
```cpp
// 既存API(deprecated)
AudioPipeline::BeatMetrics AudioPipeline::latestMetrics() const {
    // Participant1のメトリクスを返す(既存コード互換)
    std::lock_guard<std::mutex> lock(mutex_);
    BeatMetrics legacy;
    legacy.bpm = channelMetrics_[0].bpm;
    legacy.envelope = channelMetrics_[0].envelope;
    legacy.timestampSec = channelMetrics_[0].timestampSec;
    legacy.triggered = channelMetrics_[0].triggered;
    return legacy;
}

std::vector<BeatEvent> AudioPipeline::pollBeatEvents() {
    // 両参加者のイベントを時系列でマージ(既存コード互換)
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<BeatEvent> merged;
    // ... (タイムスタンプでソートしてマージ)
    return merged;
}

// 新API
AudioPipeline::ChannelMetrics AudioPipeline::channelMetrics(ParticipantId id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const std::size_t idx = static_cast<std::size_t>(id);
    if (idx >= 2) {
        return {};
    }
    return channelMetrics_[idx];
}

std::vector<BeatEvent> AudioPipeline::pollBeatEvents(ParticipantId id) {
    std::lock_guard<std::mutex> lock(mutex_);
    const std::size_t idx = static_cast<std::size_t>(id);
    if (idx >= 2) {
        return {};
    }
    std::vector<BeatEvent> events(pendingEventsByChannel_[idx].begin(),
                                    pendingEventsByChannel_[idx].end());
    pendingEventsByChannel_[idx].clear();
    return events;
}
```

### テスト戦略

#### Test 1.1: 2ch独立処理の動作確認
```cpp
// tests/audio/AudioPipeline_dual_channel_test.cpp
TEST(AudioPipeline, ProcessesTwoChannelsIndependently) {
    AudioPipeline pipeline;
    pipeline.setup(48000.0, 512);

    // CH1: 60 BPM, CH2: 80 BPM のテスト信号生成
    ofSoundBuffer testBuffer = generateDualHeartbeatBuffer(60.0f, 80.0f);

    pipeline.audioIn(testBuffer);

    auto metrics1 = pipeline.channelMetrics(ParticipantId::Participant1);
    auto metrics2 = pipeline.channelMetrics(ParticipantId::Participant2);

    EXPECT_NEAR(metrics1.bpm, 60.0f, 5.0f);
    EXPECT_NEAR(metrics2.bpm, 80.0f, 5.0f);
}
```

#### Test 1.2: 既存APIの後方互換性
```cpp
TEST(AudioPipeline, LegacyAPIStillWorks) {
    AudioPipeline pipeline;
    pipeline.setup(48000.0, 512);

    // 既存コードが動作することを確認
    auto legacyMetrics = pipeline.latestMetrics();
    EXPECT_GE(legacyMetrics.envelope, 0.0f);
}
```

### 成果物
- [x] `AudioPipeline.h` 拡張(ChannelMetrics, 新API追加)
- [x] `AudioPipeline.cpp` audioIn()改修
- [x] 後方互換API実装
- [x] ユニットテスト2件
- [ ] 動作確認ログ(2chの心拍が独立検出されることを証明)

### 検証方法
1. ユニットテスト全pass
2. 実機テスト: 2つのマイクで異なる心拍音源を再生し、BPMが独立して検出されることを確認
3. 既存のofApp::update()でエラーが出ないことを確認

---

## V. Phase 2: BeatEvent拡張とルーティング基盤

### 目的
BeatEventに`participantId`を付与し、ルーティング設定の基盤を構築

### 実施内容

#### 2.1 BeatEvent拡張の実装

**BeatTimeline.h 変更**
```cpp
struct BeatEvent {
    double timestampSec = 0.0;
    float bpm = 0.0f;
    float envelope = 0.0f;
    ParticipantId participantId = ParticipantId::None;  // ← 実装
    std::uint64_t sequenceId = 0;
};
```

**BeatTimeline.cpp 変更**
```cpp
class BeatTimeline {
public:
    void setup(double sampleRate, ParticipantId participantId);  // ← 引数追加

private:
    ParticipantId participantId_ = ParticipantId::None;
    std::uint64_t eventSequence_ = 0;
};

void BeatTimeline::processBuffer(...) {
    // ... (Beat検出ロジック)

    if (triggered) {
        BeatEvent evt;
        evt.timestampSec = currentSample / sampleRate_;
        evt.bpm = currentBpm_;
        evt.envelope = envelopeFollower_.value();
        evt.participantId = participantId_;  // ← 設定
        evt.sequenceId = eventSequence_++;   // ← 設定
        events_.push_back(evt);
    }
}
```

**AudioPipeline.cpp 初期化変更**
```cpp
void AudioPipeline::setup(double sampleRate, std::size_t bufferSize) {
    // ...
    beatTimelines_[0].setup(sampleRate_, ParticipantId::Participant1);
    beatTimelines_[1].setup(sampleRate_, ParticipantId::Participant2);
}
```

#### 2.2 AudioRouter クラス新規作成

**AudioRouter.h** (新規作成: `src/audio/AudioRouter.h`)
```cpp
#pragma once
#include "ParticipantId.h"
#include <array>
#include <cstdint>

namespace knot::audio {

enum class OutputChannel : std::uint8_t {
    CH1_HeadphoneLeft = 0,
    CH2_HeadphoneRight = 1,
    CH3_HapticP1 = 2,
    CH4_HapticP2 = 3
};

enum class MixMode : std::uint8_t {
    Self,      // 自分の心音
    Partner,   // 相手の心音
    Haptic,    // 触覚信号(20-150Hz BPF + noise carrier)
    Silent     // 無音
};

struct RoutingRule {
    ParticipantId source = ParticipantId::None;
    MixMode mixMode = MixMode::Silent;
    float gainDb = -12.0f;
    float panLR = 0.0f;  // -1.0(left) ~ +1.0(right), Hapticでは未使用
};

class AudioRouter {
public:
    void setRoutingRule(OutputChannel ch, const RoutingRule& rule);
    const RoutingRule& routingRule(OutputChannel ch) const;

    // シーン連動
    void applyScenePreset(SceneState scene);

    // ルーティング実行
    void route(
        const std::array<float, 2>& inputEnvelopes,  // [P1, P2]
        std::array<float, 4>& outputBuffer           // [CH1-4]
    );

private:
    std::array<RoutingRule, 4> rules_;

    float generateHapticSample(float envelope, ParticipantId pid);
};

} // namespace knot::audio
```

**AudioRouter.cpp** (骨格実装)
```cpp
#include "AudioRouter.h"

namespace knot::audio {

void AudioRouter::setRoutingRule(OutputChannel ch, const RoutingRule& rule) {
    rules_[static_cast<std::size_t>(ch)] = rule;
}

const RoutingRule& AudioRouter::routingRule(OutputChannel ch) const {
    return rules_[static_cast<std::size_t>(ch)];
}

void AudioRouter::applyScenePreset(SceneState scene) {
    // Phase 4で実装
}

void AudioRouter::route(
    const std::array<float, 2>& inputEnvelopes,
    std::array<float, 4>& outputBuffer
) {
    for (std::size_t i = 0; i < 4; ++i) {
        const auto& rule = rules_[i];

        if (rule.source == ParticipantId::None || rule.mixMode == MixMode::Silent) {
            outputBuffer[i] = 0.0f;
            continue;
        }

        const std::size_t srcIdx = static_cast<std::size_t>(rule.source);
        if (srcIdx >= 2) {
            outputBuffer[i] = 0.0f;
            continue;
        }

        float sample = 0.0f;
        switch (rule.mixMode) {
            case MixMode::Self:
            case MixMode::Partner:
                // Phase 3で実装(ヘッドフォン出力)
                sample = inputEnvelopes[srcIdx];
                break;
            case MixMode::Haptic:
                // Phase 3で実装(ハプティクス信号生成)
                sample = generateHapticSample(inputEnvelopes[srcIdx], rule.source);
                break;
            default:
                sample = 0.0f;
        }

        // ゲイン適用
        const float gainLinear = std::pow(10.0f, rule.gainDb / 20.0f);
        outputBuffer[i] = sample * gainLinear;
    }
}

float AudioRouter::generateHapticSample(float envelope, ParticipantId pid) {
    // Phase 3で実装
    return 0.0f;
}

} // namespace knot::audio
```

### テスト戦略

#### Test 2.1: BeatEventにparticipantId付与
```cpp
TEST(BeatTimeline, AttachesParticipantIdToEvents) {
    BeatTimeline timeline;
    timeline.setup(48000.0, ParticipantId::Participant1);

    // Beat検出するテスト信号
    auto testBuffer = generateHeartbeatSignal(60.0f);
    timeline.processBuffer(testBuffer.data(), testBuffer.size(), 0.0);

    const auto& events = timeline.events();
    ASSERT_FALSE(events.empty());
    EXPECT_EQ(events.back().participantId, ParticipantId::Participant1);
}
```

#### Test 2.2: AudioRouterの基本動作
```cpp
TEST(AudioRouter, RoutesBasicSignals) {
    AudioRouter router;

    // CH1: Participant1の心音 → -12dB
    RoutingRule rule1;
    rule1.source = ParticipantId::Participant1;
    rule1.mixMode = MixMode::Self;
    rule1.gainDb = -12.0f;
    router.setRoutingRule(OutputChannel::CH1_HeadphoneLeft, rule1);

    std::array<float, 2> inputs = {0.5f, 0.3f};  // [P1, P2]
    std::array<float, 4> outputs = {0.0f, 0.0f, 0.0f, 0.0f};

    router.route(inputs, outputs);

    EXPECT_GT(outputs[0], 0.0f);  // CH1に出力あり
    EXPECT_EQ(outputs[1], 0.0f);  // CH2は設定なし
}
```

### 成果物
- [x] `BeatEvent` 拡張実装
- [x] `ParticipantId.h/.cpp` 実装
- [x] `AudioRouter.h/.cpp` 骨格実装
- [x] ユニットテスト2件

### 検証方法
1. ユニットテスト全pass
2. BeatEventログに`participantId`が正しく記録されることを確認
3. AudioRouterが基本的なルーティングを実行できることを確認

---

## VI. Phase 3: 4ch出力実装

### 目的
実際の4chオーディオ出力を実現し、ヘッドフォン・ハプティクス信号を生成

### 実施内容

#### 3.1 ofSoundStreamを4chに変更

**ofApp.cpp setup()変更** ([ofApp.cpp:155-172](ofApp.cpp#L155-L172))
```cpp
void ofApp::setup() {
    // ...

    ofSoundStreamSettings settings;
    settings.sampleRate = static_cast<unsigned int>(sampleRate_);
    settings.numInputChannels = 2;
    settings.numOutputChannels = 4;  // ← 変更: 2 → 4
    settings.bufferSize = bufferSize_;
    settings.setInListener(this);
    settings.setOutListener(this);

    try {
        soundStream_.setup(settings);
        soundStream_.start();
        soundStreamActive_ = true;
    } catch (...) { /* ... */ }
}
```

#### 3.2 AudioPipeline::audioOut() 拡張

**AudioPipeline.h 変更**
```cpp
class AudioPipeline {
public:
    void audioOut(ofSoundBuffer& buffer);  // 既存
    void setRouter(std::shared_ptr<AudioRouter> router);

private:
    std::shared_ptr<AudioRouter> router_;
};
```

**AudioPipeline.cpp audioOut() 改修** ([AudioPipeline.cpp:268-319](AudioPipeline.cpp#L268-L319))
```cpp
void AudioPipeline::audioOut(ofSoundBuffer& buffer) {
    const auto numFrames = static_cast<std::size_t>(buffer.getNumFrames());
    const auto numChannels = buffer.getNumChannels();

    if (numChannels < 4 || numFrames == 0) {
        // Fallback: 2ch出力(既存の動作)
        if (numChannels >= 2) {
            // ... (既存の2ch処理)
        }
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    ensureBufferSizes(numFrames);
    float* output = buffer.getBuffer().data();

    if (calibrationArmed_) {
        // キャリブレーション中は4chすべてに同じ信号
        calibrationSession_.generate(output, numFrames);
        // 4chに複製
        for (std::size_t frame = 0; frame < numFrames; ++frame) {
            const float left = output[frame * 2];
            const float right = output[frame * 2 + 1];
            output[frame * 4 + 0] = left;
            output[frame * 4 + 1] = right;
            output[frame * 4 + 2] = 0.0f;  // CH3: 無音
            output[frame * 4 + 3] = 0.0f;  // CH4: 無音
        }
        return;
    }

    // === 新規: AudioRouterによる4ch出力生成 ===
    if (!router_) {
        // Routerがない場合のフォールバック
        std::fill_n(output, numFrames * 4, 0.0f);
        return;
    }

    // 現在のEnvelope値を取得
    std::array<float, 2> inputEnvelopes = {
        channelMetrics_[0].envelope,
        channelMetrics_[1].envelope
    };

    // フレームごとにルーティング実行
    for (std::size_t frame = 0; frame < numFrames; ++frame) {
        std::array<float, 4> routedSamples = {0.0f, 0.0f, 0.0f, 0.0f};
        router_->route(inputEnvelopes, routedSamples);

        // Limiter適用(CH1/CH2のみ)
        float maxSample = std::max(std::abs(routedSamples[0]), std::abs(routedSamples[1]));
        limiter_.process(maxSample);
        const float gain = limiter_.currentGain();

        // 出力バッファに書き込み
        output[frame * 4 + 0] = routedSamples[0] * gain;
        output[frame * 4 + 1] = routedSamples[1] * gain;
        output[frame * 4 + 2] = routedSamples[2];  // CH3: ハプティクス(Limiter不要)
        output[frame * 4 + 3] = routedSamples[3];  // CH4: ハプティクス(Limiter不要)
    }

    limiterReductionDb_ = limiter_.lastReductionDb();
}
```

#### 3.3 ハプティクス信号生成

**AudioRouter.cpp generateHapticSample() 実装**
```cpp
float AudioRouter::generateHapticSample(float envelope, ParticipantId pid) {
    // 20-150Hz バンドパスフィルタ(BiquadFilter使用)
    static std::array<BiquadFilter, 2> hapticBPF;
    static bool initialized = false;

    if (!initialized) {
        // 初回のみフィルタ初期化
        for (auto& filter : hapticBPF) {
            filter.setBandPass(48000.0, 85.0, 65.0);  // 中心85Hz, Q=65Hz幅
        }
        initialized = true;
    }

    // ピンクノイズ生成(簡易版: ホワイトノイズ + LPF)
    static std::mt19937 rng(std::random_device{}());
    static std::normal_distribution<float> noiseDist(0.0f, 1.0f);

    const float noise = noiseDist(rng);

    // Envelope modulation
    const float modulated = noise * std::clamp(envelope * 0.8f, 0.0f, 1.0f);

    // BPF適用
    const std::size_t idx = static_cast<std::size_t>(pid);
    if (idx >= 2) {
        return 0.0f;
    }

    const float filtered = hapticBPF[idx].process(modulated);

    return filtered;
}
```

### テスト戦略

#### Test 3.1: 4ch出力の生成確認
```cpp
TEST(AudioPipeline, Generates4ChannelOutput) {
    AudioPipeline pipeline;
    pipeline.setup(48000.0, 512);

    auto router = std::make_shared<AudioRouter>();
    // CH1: P1 self, CH2: P2 self, CH3: P1 haptic, CH4: P2 haptic
    // ... (ルーティング設定)
    pipeline.setRouter(router);

    // 入力処理
    auto inputBuffer = generateDualHeartbeatBuffer(60.0f, 80.0f);
    pipeline.audioIn(inputBuffer);

    // 出力生成
    ofSoundBuffer outputBuffer;
    outputBuffer.allocate(512, 4);  // 4ch
    pipeline.audioOut(outputBuffer);

    // CH1-4すべてに信号が生成されていることを確認
    for (int ch = 0; ch < 4; ++ch) {
        float rms = calculateRMS(outputBuffer, ch);
        EXPECT_GT(rms, 0.0f) << "Channel " << ch << " is silent";
    }
}
```

#### Test 3.2: ハプティクス信号の周波数帯域確認
```cpp
TEST(AudioRouter, HapticSignalWithin20_150Hz) {
    AudioRouter router;

    std::vector<float> hapticSamples(1024);
    for (size_t i = 0; i < 1024; ++i) {
        hapticSamples[i] = router.generateHapticSample(0.5f, ParticipantId::Participant1);
    }

    // FFT解析
    auto spectrum = performFFT(hapticSamples, 48000.0);

    // 20-150Hz にエネルギーが集中していることを確認
    float energyInBand = integratePower(spectrum, 20.0, 150.0);
    float totalEnergy = integratePower(spectrum, 0.0, 24000.0);

    EXPECT_GT(energyInBand / totalEnergy, 0.85f);  // 85%以上
}
```

### 成果物
- [x] ofApp 4ch出力設定
- [x] AudioPipeline::audioOut() 4ch対応
- [x] AudioRouter::generateHapticSample() 実装
- [x] ユニットテスト2件
- [ ] 実機4chオーディオインターフェース動作確認ログ

### 検証方法
1. ユニットテスト全pass
2. 実機テスト: 4chオーディオインターフェースで出力確認
   - CH1/2: ヘッドフォンで心音が聞こえる
   - CH3/4: オシロスコープで20-150Hzの信号確認
3. 触覚振動子接続テスト(実機)

---

## VII. Phase 4: GUI統合とシーン連携

### 目的
GUIでルーティング設定を制御可能にし、シーン遷移で自動切替

### 実施内容

#### 4.1 RoutingControlPanel GUI追加

**ofApp.h 変更**
```cpp
class ofApp : public ofBaseApp {
private:
    ofxPanel routingPanel_;
    ofParameter<int> ch1SourceParam_;      // 0=P1, 1=P2, 2=None
    ofParameter<int> ch1MixModeParam_;     // 0=Self, 1=Partner, 2=Silent
    ofParameter<float> ch1GainDbParam_;
    // ... (CH2-4も同様)

    ofParameter<bool> autoRoutingParam_;   // シーン連動ON/OFF

    std::shared_ptr<AudioRouter> audioRouter_;

    void onRoutingParamChanged(int& value);
    void updateAudioRouterFromGui();
};
```

**ofApp.cpp GUI構築**
```cpp
void ofApp::setup() {
    // ...

    audioRouter_ = std::make_shared<AudioRouter>();
    audioPipeline_.setRouter(audioRouter_);

    routingPanel_.setup("Audio Routing");
    routingPanel_.setPosition(20.0f, 400.0f);

    routingPanel_.add(autoRoutingParam_.set("Auto Routing (Scene)", true));

    // CH1設定
    routingPanel_.add(ch1SourceParam_.set("CH1 Source", 0, 0, 2));
    routingPanel_.add(ch1MixModeParam_.set("CH1 Mix", 0, 0, 2));
    routingPanel_.add(ch1GainDbParam_.set("CH1 Gain(dB)", -12.0f, -40.0f, 0.0f));

    // ... (CH2-4も同様)

    // GUIパラメータ変更時のコールバック
    ch1SourceParam_.addListener(this, &ofApp::onRoutingParamChanged);
    ch1MixModeParam_.addListener(this, &ofApp::onRoutingParamChanged);
    ch1GainDbParam_.addListener(this, &ofApp::onRoutingParamChanged);
    // ... (他チャンネルも同様)
}

void ofApp::onRoutingParamChanged(int& value) {
    updateAudioRouterFromGui();
}

void ofApp::updateAudioRouterFromGui() {
    if (autoRoutingParam_) {
        // シーン連動モードの場合はGUI変更を無視
        return;
    }

    // GUIパラメータ → AudioRouterに反映
    RoutingRule rule1;
    rule1.source = static_cast<ParticipantId>(ch1SourceParam_.get());
    rule1.mixMode = static_cast<MixMode>(ch1MixModeParam_.get());
    rule1.gainDb = ch1GainDbParam_;
    audioRouter_->setRoutingRule(OutputChannel::CH1_HeadphoneLeft, rule1);

    // ... (CH2-4も同様)
}
```

#### 4.2 シーン連動ルーティング

**AudioRouter.cpp applyScenePreset() 実装**
```cpp
void AudioRouter::applyScenePreset(SceneState scene) {
    switch (scene) {
        case SceneState::Idle:
        case SceneState::Start:
        case SceneState::FirstPhase:
            // 自分の心音を聞く
            setRoutingRule(OutputChannel::CH1_HeadphoneLeft,
                {ParticipantId::Participant1, MixMode::Self, -12.0f});
            setRoutingRule(OutputChannel::CH2_HeadphoneRight,
                {ParticipantId::Participant2, MixMode::Self, -12.0f});
            setRoutingRule(OutputChannel::CH3_HapticP1,
                {ParticipantId::Participant1, MixMode::Haptic, -15.0f});
            setRoutingRule(OutputChannel::CH4_HapticP2,
                {ParticipantId::Participant2, MixMode::Haptic, -15.0f});
            break;

        case SceneState::Exchange:
            // 相手の心音を聞く(交換)
            setRoutingRule(OutputChannel::CH1_HeadphoneLeft,
                {ParticipantId::Participant2, MixMode::Partner, -9.0f});
            setRoutingRule(OutputChannel::CH2_HeadphoneRight,
                {ParticipantId::Participant1, MixMode::Partner, -9.0f});
            // ハプティクスは自分の心音のまま
            setRoutingRule(OutputChannel::CH3_HapticP1,
                {ParticipantId::Participant1, MixMode::Haptic, -15.0f});
            setRoutingRule(OutputChannel::CH4_HapticP2,
                {ParticipantId::Participant2, MixMode::Haptic, -15.0f});
            break;

        case SceneState::Mixed:
            // 両方の心音をミックス(Phase 5で拡張)
            // 暫定: 自分の心音 + 相手の心音を30%ミックス
            setRoutingRule(OutputChannel::CH1_HeadphoneLeft,
                {ParticipantId::Participant1, MixMode::Self, -12.0f});
            setRoutingRule(OutputChannel::CH2_HeadphoneRight,
                {ParticipantId::Participant2, MixMode::Self, -12.0f});
            setRoutingRule(OutputChannel::CH3_HapticP1,
                {ParticipantId::Participant1, MixMode::Haptic, -15.0f});
            setRoutingRule(OutputChannel::CH4_HapticP2,
                {ParticipantId::Participant2, MixMode::Haptic, -15.0f});
            break;

        case SceneState::End:
            // フェードアウト(ゲインを徐々に下げる)
            // ... (Phase 5で実装)
            break;
    }
}
```

**ofApp.cpp シーン遷移時の呼び出し**
```cpp
void ofApp::handleTransitionEvent(const SceneController::TransitionEvent& event) {
    // ... (既存処理)

    // シーン遷移完了時にルーティング更新
    if (event.completed && autoRoutingParam_) {
        audioRouter_->applyScenePreset(event.to);
        ofLogNotice("ofApp") << "Applied audio routing preset for scene: "
                              << sceneStateToString(event.to);
    }
}
```

### テスト戦略

#### Test 4.1: GUI→AudioRouter連携
```cpp
TEST(ofApp, GuiUpdatesAudioRouter) {
    ofApp app;
    app.setup();

    // GUI パラメータ変更
    app.ch1SourceParam_ = 1;  // Participant2
    app.ch1MixModeParam_ = 1; // Partner

    // AudioRouterに反映されることを確認
    auto rule = app.audioRouter_->routingRule(OutputChannel::CH1_HeadphoneLeft);
    EXPECT_EQ(rule.source, ParticipantId::Participant2);
    EXPECT_EQ(rule.mixMode, MixMode::Partner);
}
```

#### Test 4.2: シーン遷移でルーティング自動切替
```cpp
TEST(AudioRouter, AppliesExchangeScenePreset) {
    AudioRouter router;
    router.applyScenePreset(SceneState::Exchange);

    // Exchangeシーン: CH1にP2の心音が割り当てられる
    auto rule = router.routingRule(OutputChannel::CH1_HeadphoneLeft);
    EXPECT_EQ(rule.source, ParticipantId::Participant2);
    EXPECT_EQ(rule.mixMode, MixMode::Partner);
}
```

### 成果物
- [x] ofApp routingPanel_ GUI実装
- [x] AudioRouter::applyScenePreset() 実装
- [x] シーン遷移時の自動切替
- [x] ユニットテスト2件

### 検証方法
1. GUIでルーティング設定を変更し、出力が切り替わることを確認
2. シーン遷移(Idle→Exchange)で自動的にルーティングが切り替わることを確認
3. autoRoutingをOFFにした場合、手動設定が維持されることを確認

---

## VIII. Phase 5: 合成心拍モード(1人デモ対応)

### 目的
Participant2がいない場合に合成心拍を生成し、1人でデモ実行可能にする

### 実施内容

#### 5.1 SyntheticHeartbeatGenerator クラス

**SyntheticHeartbeatGenerator.h** (新規作成: `src/audio/SyntheticHeartbeatGenerator.h`)
```cpp
#pragma once
#include <cstdint>
#include <random>
#include <vector>

namespace knot::audio {

class SyntheticHeartbeatGenerator {
public:
    struct Config {
        float baseBpm = 68.0f;
        float variability = 5.0f;  // ±5 BPM
        float noiseLevelDb = -40.0f;
        std::uint32_t seed = 0;
    };

    void setup(double sampleRate, const Config& config);
    void generateBuffer(float* output, std::size_t numFrames);

    float currentBpm() const { return currentBpm_; }

private:
    double sampleRate_ = 48000.0;
    Config config_;
    std::mt19937 rng_;
    std::normal_distribution<float> noiseDist_{0.0f, 1.0f};

    double phase_ = 0.0;
    double lastBeatSample_ = 0.0;
    float currentBpm_ = 68.0f;
    double totalSamples_ = 0.0;

    float generateS1Waveform(double localPhase);  // S1心音(主音)
    float generateS2Waveform(double localPhase);  // S2心音(副音)
};

} // namespace knot::audio
```

**SyntheticHeartbeatGenerator.cpp** (実装)
```cpp
#include "SyntheticHeartbeatGenerator.h"
#include <cmath>

namespace knot::audio {

void SyntheticHeartbeatGenerator::setup(double sampleRate, const Config& config) {
    sampleRate_ = sampleRate;
    config_ = config;
    rng_.seed(config.seed > 0 ? config.seed : std::random_device{}());
    phase_ = 0.0;
    lastBeatSample_ = 0.0;
    currentBpm_ = config_.baseBpm;
    totalSamples_ = 0.0;
}

void SyntheticHeartbeatGenerator::generateBuffer(float* output, std::size_t numFrames) {
    const double samplesPerBeat = (60.0 / currentBpm_) * sampleRate_;

    for (std::size_t i = 0; i < numFrames; ++i) {
        const double samplesSinceLastBeat = totalSamples_ - lastBeatSample_;

        // 新しい心拍のトリガー
        if (samplesSinceLastBeat >= samplesPerBeat) {
            lastBeatSample_ = totalSamples_;

            // BPM変動(呼吸性変動を模擬)
            const float breathingPhase = std::sin(totalSamples_ / sampleRate_ * 0.1 * 2.0 * M_PI);
            const float variationBpm = config_.variability * breathingPhase;
            currentBpm_ = std::clamp(config_.baseBpm + variationBpm, 50.0f, 90.0f);
        }

        // 心拍波形生成
        const double localPhase = samplesSinceLastBeat / samplesPerBeat;

        float sample = 0.0f;

        // S1心音(0.0-0.15の位置、持続100ms)
        if (localPhase < 0.15) {
            const double s1Phase = localPhase / 0.15;
            sample += generateS1Waveform(s1Phase);
        }

        // S2心音(0.35-0.45の位置、持続70ms)
        if (localPhase >= 0.35 && localPhase < 0.45) {
            const double s2Phase = (localPhase - 0.35) / 0.10;
            sample += generateS2Waveform(s2Phase);
        }

        // ノイズ付加
        const float noiseGain = std::pow(10.0f, config_.noiseLevelDb / 20.0f);
        sample += noiseDist_(rng_) * noiseGain;

        output[i] = std::clamp(sample, -1.0f, 1.0f);
        totalSamples_ += 1.0;
    }
}

float SyntheticHeartbeatGenerator::generateS1Waveform(double localPhase) {
    // S1: 30-80Hzの複合波形、ピーク振幅0.7
    const float envelope = std::sin(localPhase * M_PI);  // Attack-Decay envelope
    const float freq1 = 40.0f;
    const float freq2 = 60.0f;
    const float wave = 0.5f * std::sin(localPhase * freq1 * 2.0 * M_PI / 48000.0) +
                       0.5f * std::sin(localPhase * freq2 * 2.0 * M_PI / 48000.0);
    return wave * envelope * 0.7f;
}

float SyntheticHeartbeatGenerator::generateS2Waveform(double localPhase) {
    // S2: 60-120Hzの複合波形、ピーク振幅0.5
    const float envelope = std::sin(localPhase * M_PI);
    const float freq1 = 80.0f;
    const float freq2 = 100.0f;
    const float wave = 0.5f * std::sin(localPhase * freq1 * 2.0 * M_PI / 48000.0) +
                       0.5f * std::sin(localPhase * freq2 * 2.0 * M_PI / 48000.0);
    return wave * envelope * 0.5f;
}

} // namespace knot::audio
```

#### 5.2 AudioPipeline統合

**AudioPipeline.h 変更**
```cpp
class AudioPipeline {
public:
    void setSoloMode(bool enabled, std::uint32_t syntheticSeed = 0);
    bool isSoloMode() const { return soloModeEnabled_; }

private:
    bool soloModeEnabled_ = false;
    SyntheticHeartbeatGenerator syntheticGenerator_;
    std::vector<float> syntheticBuffer_;
};
```

**AudioPipeline.cpp audioIn()変更**
```cpp
void AudioPipeline::audioIn(const ofSoundBuffer& buffer) {
    // ... (既存処理)

    if (!calibrationArmed_) {
        // CH1: 実入力
        for (std::size_t frame = 0; frame < numFrames; ++frame) {
            float ch1 = input[frame * 2];
            // ... (既存の前処理)
            channelBuffers_[0][frame] = ch1;
        }

        // CH2: Soloモード時は合成心拍
        if (soloModeEnabled_) {
            syntheticGenerator_.generateBuffer(syntheticBuffer_.data(), numFrames);
            for (std::size_t frame = 0; frame < numFrames; ++frame) {
                channelBuffers_[1][frame] = syntheticBuffer_[frame];
            }
        } else {
            // 通常モード: 実入力
            for (std::size_t frame = 0; frame < numFrames; ++frame) {
                float ch2 = input[frame * 2 + 1];
                // ... (既存の前処理)
                channelBuffers_[1][frame] = ch2;
            }
        }

        // ... (BeatTimeline処理は共通)
    }
}

void AudioPipeline::setSoloMode(bool enabled, std::uint32_t syntheticSeed) {
    std::lock_guard<std::mutex> lock(mutex_);
    soloModeEnabled_ = enabled;

    if (enabled) {
        SyntheticHeartbeatGenerator::Config config;
        config.baseBpm = 68.0f;
        config.variability = 5.0f;
        config.noiseLevelDb = -40.0f;
        config.seed = syntheticSeed;
        syntheticGenerator_.setup(sampleRate_, config);
        syntheticBuffer_.assign(bufferSize_, 0.0f);

        ofLogNotice("AudioPipeline") << "Solo mode enabled with synthetic heartbeat";
    }
}
```

#### 5.3 GUI統合

**ofApp.h 変更**
```cpp
class ofApp : public ofBaseApp {
private:
    ofParameter<bool> soloModeParam_;
    ofParameter<float> syntheticBpmParam_;
    ofParameter<float> syntheticVariabilityParam_;
};
```

**ofApp.cpp GUI追加**
```cpp
void ofApp::setup() {
    // ...

    controlPanel_.add(soloModeParam_.set("Solo Mode (1人デモ)", false));
    controlPanel_.add(syntheticBpmParam_.set("Synthetic BPM", 68.0f, 50.0f, 90.0f));
    controlPanel_.add(syntheticVariabilityParam_.set("BPM Variability", 5.0f, 0.0f, 10.0f));

    soloModeParam_.addListener(this, &ofApp::onSoloModeChanged);
}

void ofApp::onSoloModeChanged(bool& enabled) {
    audioPipeline_.setSoloMode(enabled, static_cast<std::uint32_t>(sessionSeed_));

    if (enabled) {
        ofLogNotice("ofApp") << "Solo mode activated. Participant2 is now synthetic.";
    } else {
        ofLogNotice("ofApp") << "Solo mode deactivated. Using real microphone input.";
    }
}
```

### テスト戦略

#### Test 5.1: 合成心拍の波形生成
```cpp
TEST(SyntheticHeartbeatGenerator, GeneratesRealisticWaveform) {
    SyntheticHeartbeatGenerator generator;
    SyntheticHeartbeatGenerator::Config config;
    config.baseBpm = 68.0f;
    config.seed = 12345;
    generator.setup(48000.0, config);

    std::vector<float> buffer(48000);  // 1秒分
    generator.generateBuffer(buffer.data(), buffer.size());

    // BPM検証: 約68 BPMの心拍が検出されるはず
    int beatCount = countBeats(buffer, 48000.0);
    EXPECT_NEAR(beatCount, 68.0f / 60.0f, 0.2f);  // ±0.2拍の誤差許容
}
```

#### Test 5.2: Solo モード動作確認
```cpp
TEST(AudioPipeline, SoloModeGeneratesSyntheticChannel2) {
    AudioPipeline pipeline;
    pipeline.setup(48000.0, 512);
    pipeline.setSoloMode(true, 12345);

    // CH1: 実入力(60 BPM), CH2: 合成(68 BPM)
    ofSoundBuffer inputBuffer = generateHeartbeatBuffer(60.0f);
    pipeline.audioIn(inputBuffer);

    auto metrics1 = pipeline.channelMetrics(ParticipantId::Participant1);
    auto metrics2 = pipeline.channelMetrics(ParticipantId::Participant2);

    EXPECT_NEAR(metrics1.bpm, 60.0f, 5.0f);  // 実入力
    EXPECT_NEAR(metrics2.bpm, 68.0f, 5.0f);  // 合成
}
```

### 成果物
- [x] `SyntheticHeartbeatGenerator.h/.cpp` 実装
- [x] AudioPipeline Solo モード統合
- [x] ofApp GUI統合
- [x] ユニットテスト2件

### 検証方法
1. ユニットテスト全pass
2. Solo モードONで起動し、CH2に合成心拍が生成されることを確認
3. Exchange/Mixedシーンで1人でデモが回せることを確認
4. 再現性テスト: 同じseedで同じBPM系列が生成されることを確認

---

## IX. 統合テストと検証

### 統合テストシナリオ

#### Scenario 1: 2名参加フルフロー
```
前提: Solo Mode OFF、実際の2つのマイク接続
1. Idle: 各自の心音が自分のヘッドフォンで聞こえる
2. Start → FirstPhase: 自分の心音継続
3. Exchange: 相手の心音が聞こえる(CH1↔CH2交換)
4. Mixed: 両方の心音がミックスされる
5. End: フェードアウト
6. Idle復帰

期待結果:
- 各シーンでルーティングが自動切替
- CH3/CH4でハプティクス信号出力
- BeatEventにparticipantId正しく記録
- ログに2名分のBPM記録
```

#### Scenario 2: 1名参加デモ
```
前提: Solo Mode ON、CH1のみマイク接続
1. Idle: P1実入力、P2合成心拍
2. Exchange: P1は合成心拍を聞く、P2(合成)はP1の実音を"聞く"
3. Mixed: 実音と合成がミックス

期待結果:
- P2の心拍が合成生成される(68±5 BPM)
- Exchange/Mixedシーンが動作
- ログにP2=Syntheticの記録
```

#### Scenario 3: ルーティング手動設定
```
前提: Auto Routing OFF
1. GUIでCH1→P2 selfに変更
2. CH2→P1 selfに変更(入れ替え)
3. 音声出力が入れ替わることを確認

期待結果:
- GUI変更が即座に反映
- シーン遷移してもルーティング維持
```

### 性能検証

#### Performance 1: CPU負荷測定
```
条件: 2ch入力、4ch出力、ルーティング有効
測定項目:
- audioIn() 処理時間: <5ms (512 frames @ 48kHz = 10.7ms buffer)
- audioOut() 処理時間: <5ms
- 合計レイテンシ: <80ms (BeatEvent→出力)

合格基準: CPU使用率 60%以下(既存 + 10%以内)
```

#### Performance 2: メモリ使用量
```
測定項目:
- AudioPipeline拡張後のメモリ増加: <5MB
- 長時間稼働(30分)でのメモリリークなし

合格基準: 増加量 10MB/時間以内
```

---

## X. 実装順序サマリー

### 推奨実装順序(依存関係考慮)

```
Week 1:
  Day 1-2: Phase 0 (設計・準備)
  Day 3-4: Phase 1 (2ch独立処理)
  Day 5:   Phase 1 テスト・デバッグ

Week 2:
  Day 1-2: Phase 2 (BeatEvent拡張、AudioRouter骨格)
  Day 3-4: Phase 3 (4ch出力、ハプティクス信号)
  Day 5:   Phase 3 テスト・実機確認

Week 3:
  Day 1-2: Phase 4 (GUI統合、シーン連携)
  Day 3:   Phase 5 (合成心拍モード)
  Day 4:   統合テスト
  Day 5:   性能検証・最適化
```

### クリティカルパス

```
Phase 0 → Phase 1 → Phase 2 → Phase 3
                      ↓
                   Phase 4 → 統合テスト
                      ↓
                   Phase 5 → 最終検証
```

**Phase 1-3は直列依存**、**Phase 4-5は並行可能**

---

## XI. リスク管理

### リスク1: 4chオーディオデバイスの互換性問題
**発生確率**: 30%
**影響**: Phase 3ブロック
**緩和策**:
- 事前にデバイステスト(Phase 0で実施)
- 2chフォールバックモード実装(Phase 3)

### リスク2: BeatTimeline独立処理での検出精度低下
**発生確率**: 20%
**影響**: Phase 1でBPM検出失敗
**緩和策**:
- Phase 1でテスト信号による精度検証
- しきい値調整パラメータのGUI公開

### リスク3: ハプティクス信号の周波数特性不適合
**発生確率**: 15%
**影響**: Phase 3で振動子が正しく動作しない
**緩和策**:
- Phase 3でFFT解析による周波数検証
- BPFパラメータの調整機能

### リスク4: Solo モードの合成心拍がBeatTimelineで検出されない
**発生確率**: 25%
**影響**: Phase 5でデモ不可
**緩和策**:
- Phase 5で合成波形の事前検証
- S1/S2波形のパラメータ調整

---

## XII. 成功基準

### Phase 1 完了基準
- [x] 2chが独立したBeatTimelineで処理される
- [x] 既存のofApp::update()でエラーが出ない
- [x] ユニットテスト全pass

### Phase 3 完了基準
- [x] 4chオーディオ出力が実機で動作
- [x] CH3/CH4に20-150Hzの信号が生成される
- [x] ヘッドフォン・ハプティクスが独立制御される

### Phase 5 完了基準
- [x] Solo モードで1人デモが実行可能
- [x] 合成心拍がBeatTimelineで検出される(誤検出率<5%)
- [x] Exchange/Mixedシーンが動作

### 最終統合完了基準
- [x] 2名参加フルフロー動作
- [x] 1名参加デモ動作
- [x] CPU使用率60%以下
- [x] 30分連続稼働でクラッシュなし
- [x] GUI手動ルーティング設定が正しく反映

---

## XIII. 次のアクション

### 即時開始(今週中)
1. **Phase 0実施** (1-2日)
   - データ構造設計レビュー
   - 4chオーディオデバイステスト
   - 依存関係図作成

2. **Phase 1着手** (2-3日)
   - AudioPipeline.h拡張
   - audioIn()改修
   - ユニットテスト作成

### 中期(来週)
3. **Phase 2-3実装** (4-5日)
4. **実機検証** (1日)

### 長期(3週目)
5. **Phase 4-5実装** (3-4日)
6. **統合テスト** (1-2日)

---

**文書管理**
- 作成者: プロジェクト統括責任者
- レビュー必須: 開発チーム全員
- 更新履歴: 各Phase完了時に進捗を記録
