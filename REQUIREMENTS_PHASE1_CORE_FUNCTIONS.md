# Phase 1 要件定義書: コア機能修復

**作成日**: 2025-10-29
**対象**: 開発チーム
**優先度**: 🔴 致命的 (即座に実装必要)
**目的**: Exchangeシーン、2チャンネル独立ビジュアル、ハプティクス出力を機能させる

---

## 📋 Phase 1 の目標

### 解決する問題:
- ✅ **問題1**: Exchangeシーンでオーディオルーティングが切り替わらない
- ✅ **問題2**: 片方のマイクだけがビジュアルと連動している
- ✅ **問題4**: ハプティクス信号生成が未実装

### 成功基準:
1. Exchangeシーンで心拍音が交換される
2. 2人の参加者の心拍が独立してビジュアルに表示される
3. CH3/4にハプティクス信号が出力される
4. 既存の機能が引き続き動作する

### 実装範囲外 (Phase 2で対応):
- ルーティングGUI
- 1人モード (Synthetic心拍)
- 動的ルーティング変更

---

## 🎯 実装単位一覧

Phase 1は **12個の最小実装単位** で構成されます。順序通りに実施してください。

| ID | 実装単位 | ファイル | 優先度 | 依存関係 |
|----|---------|---------|-------|---------|
| **1.1** | 4チャンネル出力有効化 | ofApp.cpp | 🔴 | なし |
| **1.2** | AudioRouterメンバー追加 | ofApp.h | 🔴 | なし |
| **1.3** | AudioRouterセットアップ | ofApp.cpp | 🔴 | 1.2 |
| **1.4** | generateHapticSample実装 | AudioRouter.cpp | 🔴 | なし |
| **1.5** | applyScenePreset実装 | AudioRouter.cpp | 🔴 | なし |
| **1.6** | audioOut統合: エンベロープ取得 | ofApp.cpp | 🔴 | 1.2, 1.3 |
| **1.7** | audioOut統合: ルーティング適用 | ofApp.cpp | 🔴 | 1.6 |
| **1.8** | handleTransitionEvent: プリセット適用 | ofApp.cpp | 🔴 | 1.3, 1.5 |
| **1.9** | 2ch独立メトリクス取得 | ofApp.cpp | 🔴 | なし |
| **1.10** | 2ch独立ビジュアル更新 | ofApp.cpp | 🔴 | 1.9 |
| **1.11** | シェーダー2ch対応 (starfield) | starfield.frag | 🟡 | 1.10 |
| **1.12** | シェーダー2ch対応 (ripple) | ripple.frag | 🟡 | 1.10 |

---

## 📝 詳細実装指示

### **Unit 1.1: 4チャンネル出力有効化**

**ファイル**: `src/ofApp.cpp`
**変更箇所**: Line 172
**所要時間**: 5分

#### 背景:
現在は2チャンネル出力 (ヘッドフォンのみ) だが、ハプティクス用にCH3/4が必要。

#### 実装指示:

```cpp
// BEFORE (L172):
settings.numOutputChannels = 2;

// AFTER:
settings.numOutputChannels = 4;  // CH1/2: Headphones, CH3/4: Haptics
```

#### テスト方法:

```bash
# 1. ビルド
make Release

# 2. 起動してオーディオデバイス確認
# コンソールログに出力チャンネル数が表示される
# macOS: Audio MIDI Setup.app で4chであることを確認

# 3. アプリケーション起動確認
# クラッシュせずに起動すること
# 既存の音声出力 (CH1/2) が引き続き動作すること
```

#### 成功基準:
- ✅ ビルドエラーなし
- ✅ アプリケーションがクラッシュせず起動
- ✅ 既存のヘッドフォン音声が引き続き出力される
- ✅ オーディオインターフェースが4ch認識

#### リスク:
- **中**: オーディオインターフェースが4ch未対応の場合、起動失敗の可能性
- **対策**: 4ch対応のオーディオインターフェースを使用すること

---

### **Unit 1.2: AudioRouterメンバー追加**

**ファイル**: `src/ofApp.h`
**変更箇所**: Line 178付近 (private members)
**所要時間**: 10分

#### 背景:
AudioRouterクラスは存在するが、ofAppに統合されていない。

#### 実装指示:

**追加位置**: Line 178 (audioFading_ の直後)

```cpp
// 既存コード (L172-178):
    ofSoundPlayer bellSound_;
    bool bellSoundLoaded_ = false;
    float audioFadeGain_ = 1.0f;
    float targetAudioFadeGain_ = 1.0f;
    double audioFadeStartTime_ = 0.0;
    double audioFadeDuration_ = 10.0;
    bool audioFading_ = false;

// 追加: (L179以降に追加)
    // Audio routing for 4-channel output
    knot::audio::AudioRouter audioRouter_;
    std::array<float, 2> currentEnvelopes_{0.0f, 0.0f};
    std::array<float, 4> outputBuffer_{0.0f, 0.0f, 0.0f, 0.0f};
};
```

**ファイル先頭にインクルード追加**:

```cpp
// 既存のインクルードに追加 (AudioPipeline.hの近く):
#include "audio/AudioRouter.h"
```

#### テスト方法:

```bash
# 1. ビルド
make Release

# ビルドエラーがなければ成功
# この段階では動作確認不要 (変数宣言のみ)
```

#### 成功基準:
- ✅ ビルドエラーなし
- ✅ AudioRouter.hが正しくインクルードされる
- ✅ メンバー変数が正しく宣言される

#### リスク:
- **低**: インクルードパスの問題
- **対策**: `#include "audio/AudioRouter.h"` の相対パスを確認

---

### **Unit 1.3: AudioRouterセットアップ**

**ファイル**: `src/ofApp.cpp`
**変更箇所**: setup() (Line 165付近)
**所要時間**: 15分

#### 背景:
AudioRouterインスタンスを初期化し、初期シーンのプリセットを適用する。

#### 実装指示:

**挿入位置**: Line 165付近 (audioPipeline_.setup()の直後)

```cpp
// 既存コード (L163-165):
    const float inputGainDb = appConfig_.audio.inputGainDb;
    audioPipeline_.setup(sampleRate_, inputGainDb);

// 追加: (L166以降に追加)
    // Initialize audio router for 4-channel output
    audioRouter_.setup();
    audioRouter_.applyScenePreset(sceneController_.currentScene());
    ofLogNotice("ofApp") << "AudioRouter initialized for scene: "
                          << sceneStateToString(sceneController_.currentScene());
```

**AudioRouter.hにsetup()メソッド追加が必要な場合**:

```cpp
// src/audio/AudioRouter.h に追加 (既に存在する場合はスキップ):
public:
    void setup() {
        // Initialize routing rules to default (all silent)
        for (auto& rule : rules_) {
            rule.source = ParticipantId::None;
            rule.mixMode = MixMode::Silent;
            rule.gainDb = -96.0f;  // Mute
            rule.panLR = 0.0f;
        }
    }
```

#### テスト方法:

```bash
# 1. ビルド
make Release

# 2. 起動してログ確認
# コンソールに "AudioRouter initialized for scene: Idle" が表示される

# 3. クラッシュしないことを確認
# AudioRouterが正常に初期化されている
```

#### 成功基準:
- ✅ ビルドエラーなし
- ✅ 起動時にAudioRouterが初期化される
- ✅ 初期シーン (Idle) のプリセットが適用される
- ✅ クラッシュしない

#### リスク:
- **低**: setup()メソッドが存在しない場合
- **対策**: AudioRouter.hにsetup()を追加

---

### **Unit 1.4: generateHapticSample実装**

**ファイル**: `src/audio/AudioRouter.cpp`
**変更箇所**: generateHapticSample() (L71-75)
**所要時間**: 30分

#### 背景:
ハプティクストランスデューサー (20-150Hz) 用の低周波信号を生成する。

#### 実装指示:

**AudioRouter.h にprivateメンバー追加**:

```cpp
// src/audio/AudioRouter.h private members に追加:
private:
    float sampleRate_ = 48000.0f;
    std::array<double, 2> hapticPhase_{0.0, 0.0};  // Phase accumulator per participant
```

**AudioRouter.cpp setup()でsampleRate初期化**:

```cpp
void AudioRouter::setup() {
    sampleRate_ = 48000.0f;  // デフォルト値、必要に応じて引数で受け取る
    hapticPhase_.fill(0.0);
    // ... 既存のsetup処理 ...
}
```

**generateHapticSample()の実装**:

```cpp
// BEFORE (L71-75):
float AudioRouter::generateHapticSample(float envelope, ParticipantId id) {
    (void)id;
    (void)envelope;
    return 0.0f;
}

// AFTER:
float AudioRouter::generateHapticSample(float envelope, ParticipantId id) {
    // Generate low-frequency sine wave for haptic transducer (20-150Hz range)
    constexpr float kHapticFrequency = 50.0f;  // Hz, within Dayton Audio DAEX25 range
    constexpr float kHapticGain = 0.8f;  // Adjust for transducer sensitivity

    // Get participant index
    const std::size_t idx = static_cast<std::size_t>(id);
    if (idx >= 2) {
        return 0.0f;  // Invalid participant or Synthetic
    }

    // Generate sine wave
    const float sample = std::sin(static_cast<float>(hapticPhase_[idx] * 2.0 * M_PI));

    // Advance phase
    hapticPhase_[idx] += static_cast<double>(kHapticFrequency) / static_cast<double>(sampleRate_);
    if (hapticPhase_[idx] >= 1.0) {
        hapticPhase_[idx] -= 1.0;
    }

    // Modulate by envelope and apply gain
    return sample * envelope * kHapticGain;
}
```

#### テスト方法:

```bash
# 1. ビルド
make Release

# 2. オシロスコープまたはDAWで波形確認
# CH3/4 を録音
# Logic Pro, Ableton Live, またはAudacity等で確認

# 3. 周波数解析
# FFTスペクトラムで50Hz付近にピークが存在することを確認
# 20-150Hzの範囲内であることを確認

# 4. エンベロープ連動確認
# 心拍が強い → ハプティクス振幅大
# 心拍が弱い → ハプティクス振幅小
# 心拍なし → ハプティクス振幅0

# 5. 実機テスト (可能であれば)
# Dayton Audio DAEX25 等のハプティクストランスデューサーに接続
# 振動を感じることを確認
```

#### 成功基準:
- ✅ ビルドエラーなし
- ✅ CH3/4 に50Hz正弦波が出力される
- ✅ エンベロープに応じて振幅が変化する
- ✅ Participant1とParticipant2で独立した位相

#### リスク:
- **中**: kHapticFrequency, kHapticGain の調整が必要
- **対策**: 実機テストで最適値を調整

#### 改善案 (後のフェーズ):
- BPMに応じた周波数変調
- バンドパスフィルタ (20-150Hz)
- エンベロープのスムージング

---

### **Unit 1.5: applyScenePreset実装**

**ファイル**: `src/audio/AudioRouter.cpp`
**変更箇所**: applyScenePreset() (L34-37)
**所要時間**: 45分

#### 背景:
各シーン (FirstPhase, Exchange, Mixed) で異なるルーティングを適用する。

#### 実装指示:

```cpp
// BEFORE (L34-37):
void AudioRouter::applyScenePreset(SceneState scene) {
    (void)scene;
    // Phase 4: Scene-dependent routing will be implemented later.
}

// AFTER:
void AudioRouter::applyScenePreset(SceneState scene) {
    using namespace knot::audio;

    switch (scene) {
    case SceneState::Idle:
    case SceneState::Start:
        // No heartbeat routing during Idle/Start (guidance audio only)
        // All channels silent
        for (std::size_t i = 0; i < rules_.size(); ++i) {
            rules_[i].source = ParticipantId::None;
            rules_[i].mixMode = MixMode::Silent;
            rules_[i].gainDb = -96.0f;
            rules_[i].panLR = 0.0f;
        }
        break;

    case SceneState::FirstPhase:
        // Each participant hears their OWN heartbeat
        // CH1 (Headphone L): Participant1's own beat (full left)
        rules_[static_cast<std::size_t>(OutputChannel::CH1_HeadphoneLeft)] = {
            ParticipantId::Participant1,
            MixMode::Self,
            0.0f,   // 0dB gain
            -1.0f   // Full left pan
        };

        // CH2 (Headphone R): Participant2's own beat (full right)
        rules_[static_cast<std::size_t>(OutputChannel::CH2_HeadphoneRight)] = {
            ParticipantId::Participant2,
            MixMode::Self,
            0.0f,   // 0dB gain
            1.0f    // Full right pan
        };

        // CH3 (Haptic P1): Participant1's haptic feedback
        rules_[static_cast<std::size_t>(OutputChannel::CH3_HapticP1)] = {
            ParticipantId::Participant1,
            MixMode::Haptic,
            0.0f,   // 0dB gain
            0.0f    // Center (not used for haptics)
        };

        // CH4 (Haptic P2): Participant2's haptic feedback
        rules_[static_cast<std::size_t>(OutputChannel::CH4_HapticP2)] = {
            ParticipantId::Participant2,
            MixMode::Haptic,
            0.0f,   // 0dB gain
            0.0f    // Center (not used for haptics)
        };
        break;

    case SceneState::Exchange:
        // Participants hear PARTNER's heartbeat (exchange)
        // CH1 (Headphone L): Participant2's beat → Participant1 hears partner
        rules_[static_cast<std::size_t>(OutputChannel::CH1_HeadphoneLeft)] = {
            ParticipantId::Participant2,  // Partner's signal
            MixMode::Partner,
            0.0f,   // 0dB gain
            -1.0f   // Full left pan
        };

        // CH2 (Headphone R): Participant1's beat → Participant2 hears partner
        rules_[static_cast<std::size_t>(OutputChannel::CH2_HeadphoneRight)] = {
            ParticipantId::Participant1,  // Partner's signal
            MixMode::Partner,
            0.0f,   // 0dB gain
            1.0f    // Full right pan
        };

        // Haptics remain with self (not exchanged)
        rules_[static_cast<std::size_t>(OutputChannel::CH3_HapticP1)] = {
            ParticipantId::Participant1,
            MixMode::Haptic,
            0.0f,
            0.0f
        };
        rules_[static_cast<std::size_t>(OutputChannel::CH4_HapticP2)] = {
            ParticipantId::Participant2,
            MixMode::Haptic,
            0.0f,
            0.0f
        };
        break;

    case SceneState::Mixed:
        // Mix both participants' heartbeats in stereo
        // CH1 (L): P1 (loud, -3dB) + P2 (soft, -9dB, background)
        // Note: AudioRouter currently supports 1 rule per output channel
        // For mixing, we use P1 as primary on CH1, P2 as primary on CH2
        // Full mixing requires multiple rules per channel (Phase 2 enhancement)

        // CH1 (Headphone L): Participant1's beat (primary left)
        rules_[static_cast<std::size_t>(OutputChannel::CH1_HeadphoneLeft)] = {
            ParticipantId::Participant1,
            MixMode::Self,
            -3.0f,   // -3dB gain (slightly reduced)
            -0.7f    // Slightly left
        };

        // CH2 (Headphone R): Participant2's beat (primary right)
        rules_[static_cast<std::size_t>(OutputChannel::CH2_HeadphoneRight)] = {
            ParticipantId::Participant2,
            MixMode::Self,
            -3.0f,   // -3dB gain (slightly reduced)
            0.7f     // Slightly right
        };

        // Haptics remain independent
        rules_[static_cast<std::size_t>(OutputChannel::CH3_HapticP1)] = {
            ParticipantId::Participant1,
            MixMode::Haptic,
            0.0f,
            0.0f
        };
        rules_[static_cast<std::size_t>(OutputChannel::CH4_HapticP2)] = {
            ParticipantId::Participant2,
            MixMode::Haptic,
            0.0f,
            0.0f
        };
        break;

    case SceneState::End:
        // Keep Mixed routing, fade is handled by global fade mechanism
        applyScenePreset(SceneState::Mixed);
        break;
    }

    ofLogNotice("AudioRouter") << "Applied scene preset: " << sceneStateToString(scene)
                                << " (" << rules_.size() << " channels configured)";
}
```

#### テスト方法:

```bash
# 1. ビルド
make Release

# 2. シーン遷移テスト: FirstPhase
# GUI または自動遷移で FirstPhase に移動
# ヘッドフォン確認:
#   - 左 (CH1): Participant1の心拍音
#   - 右 (CH2): Participant2の心拍音
# ハプティクス確認:
#   - CH3: Participant1の振動
#   - CH4: Participant2の振動

# 3. シーン遷移テスト: Exchange
# FirstPhase → Exchange に遷移
# ヘッドフォン確認:
#   - 左 (CH1): Participant2の心拍音 (相手の音)
#   - 右 (CH2): Participant1の心拍音 (相手の音)
# ハプティクス: 変わらず自分の振動

# 4. シーン遷移テスト: Mixed
# Exchange → Mixed に遷移
# ヘッドフォン確認:
#   - 両チャンネルで両方の心拍が聞こえる (ステレオミックス)
#   - 左右でバランスが異なる

# 5. ログ確認
# 各シーン遷移で "Applied scene preset: ..." が表示される
```

#### 成功基準:
- ✅ ビルドエラーなし
- ✅ FirstPhase: 自分の心拍のみ聞こえる
- ✅ Exchange: 相手の心拍が聞こえる (交換)
- ✅ Mixed: 両方の心拍が混合される
- ✅ ハプティクスが全シーンで動作

#### リスク:
- **中**: Mixed シーンで真の混合が実現できない (現在のAudioRouterは1チャンネル1ルール)
- **対策**: Phase 2で複数ルール対応を実装

#### 注意:
- **MixMode::Self と MixMode::Partner の違い**:
  - Self: 自分の心拍 (通常)
  - Partner: 相手の心拍 (Exchange用)
  - 実際の処理は同じだが、意図を明確にするため区別

---

### **Unit 1.6: audioOut統合: エンベロープ取得**

**ファイル**: `src/ofApp.cpp`
**変更箇所**: audioOut() (L448-461)
**所要時間**: 30分

#### 背景:
現在はAudioPipeline::audioOut()を呼ぶだけ。AudioRouterを統合する。

#### 実装指示:

**audioOut()を完全に書き換え**:

```cpp
// BEFORE (L448-461):
void ofApp::audioOut(ofSoundBuffer& output) {
    std::lock_guard<std::mutex> lock(audioMutex_);
    audioPipeline_.audioOut(output);

    // オーディオフェードゲインを適用
    if (audioFadeGain_ < 0.99f) {
        float* buffer = output.getBuffer().data();
        const std::size_t numSamples = output.getNumFrames() * output.getNumChannels();

        for (std::size_t i = 0; i < numSamples; ++i) {
            buffer[i] *= audioFadeGain_;
        }
    }
}

// AFTER:
void ofApp::audioOut(ofSoundBuffer& output) {
    std::lock_guard<std::mutex> lock(audioMutex_);

    const std::size_t numFrames = output.getNumFrames();
    const std::size_t numChannels = output.getNumChannels();
    float* buffer = output.getBuffer().data();

    // Safety check
    if (numFrames == 0 || numChannels == 0) {
        return;
    }

    // Get current envelopes from AudioPipeline for both participants
    const auto metricsP1 = audioPipeline_.channelMetrics(ParticipantId::Participant1);
    const auto metricsP2 = audioPipeline_.channelMetrics(ParticipantId::Participant2);
    currentEnvelopes_[0] = metricsP1.envelope;
    currentEnvelopes_[1] = metricsP2.envelope;

    // Route to 4 channels for each frame
    for (std::size_t frame = 0; frame < numFrames; ++frame) {
        // Apply audio routing
        audioRouter_.route(currentEnvelopes_, outputBuffer_);

        // Write to output buffer
        if (numChannels >= 4) {
            // 4-channel mode: CH1/2 = Headphones, CH3/4 = Haptics
            buffer[frame * numChannels + 0] = outputBuffer_[0];  // CH1: Headphone L
            buffer[frame * numChannels + 1] = outputBuffer_[1];  // CH2: Headphone R
            buffer[frame * numChannels + 2] = outputBuffer_[2];  // CH3: Haptic P1
            buffer[frame * numChannels + 3] = outputBuffer_[3];  // CH4: Haptic P2
        } else if (numChannels >= 2) {
            // Fallback: 2-channel mode (headphones only)
            buffer[frame * numChannels + 0] = outputBuffer_[0];  // CH1: L
            buffer[frame * numChannels + 1] = outputBuffer_[1];  // CH2: R
        }
    }

    // Apply audio fade gain if active
    if (audioFadeGain_ < 0.99f) {
        for (std::size_t i = 0; i < numFrames * numChannels; ++i) {
            buffer[i] *= audioFadeGain_;
        }
    }
}
```

#### テスト方法:

```bash
# 1. ビルド
make Release

# 2. オーディオ出力確認
# ヘッドフォンで音が聞こえることを確認
# CH1/2 に音声が出力されている

# 3. ハプティクス出力確認 (オシロスコープ)
# CH3/4 に50Hz正弦波が出力されている

# 4. シーン遷移テスト
# FirstPhase → Exchange で音が変わることを確認
# (Unit 1.8完了後に完全動作)

# 5. パフォーマンス確認
# CPU使用率が許容範囲内 (<30%)
# オーディオドロップアウトがない
```

#### 成功基準:
- ✅ ビルドエラーなし
- ✅ ヘッドフォン音声が出力される
- ✅ ハプティクス信号 (CH3/4) が出力される
- ✅ エンベロープがルーティングに反映される

#### リスク:
- **高**: 毎フレームroute()呼び出しでパフォーマンス低下の可能性
- **対策**: 後でバッファ単位の最適化を実施 (Unit 1.7で改善)

---

### **Unit 1.7: audioOut統合: パフォーマンス最適化**

**ファイル**: `src/ofApp.cpp`
**変更箇所**: audioOut() (Unit 1.6の改善)
**所要時間**: 20分

#### 背景:
Unit 1.6で毎フレームroute()を呼んでいるが、エンベロープは高頻度更新不要。

#### 実装指示:

**Unit 1.6のコードを最適化**:

```cpp
void ofApp::audioOut(ofSoundBuffer& output) {
    std::lock_guard<std::mutex> lock(audioMutex_);

    const std::size_t numFrames = output.getNumFrames();
    const std::size_t numChannels = output.getNumChannels();
    float* buffer = output.getBuffer().data();

    if (numFrames == 0 || numChannels == 0) {
        return;
    }

    // Get envelopes ONCE per buffer (not per frame)
    const auto metricsP1 = audioPipeline_.channelMetrics(ParticipantId::Participant1);
    const auto metricsP2 = audioPipeline_.channelMetrics(ParticipantId::Participant2);
    currentEnvelopes_[0] = metricsP1.envelope;
    currentEnvelopes_[1] = metricsP2.envelope;

    // Apply routing ONCE per buffer (envelopes don't change within 512 samples)
    audioRouter_.route(currentEnvelopes_, outputBuffer_);

    // Fill buffer with routed audio
    for (std::size_t frame = 0; frame < numFrames; ++frame) {
        if (numChannels >= 4) {
            buffer[frame * numChannels + 0] = outputBuffer_[0];
            buffer[frame * numChannels + 1] = outputBuffer_[1];
            buffer[frame * numChannels + 2] = outputBuffer_[2];
            buffer[frame * numChannels + 3] = outputBuffer_[3];
        } else if (numChannels >= 2) {
            buffer[frame * numChannels + 0] = outputBuffer_[0];
            buffer[frame * numChannels + 1] = outputBuffer_[1];
        }
    }

    // Apply audio fade gain
    if (audioFadeGain_ < 0.99f) {
        for (std::size_t i = 0; i < numFrames * numChannels; ++i) {
            buffer[i] *= audioFadeGain_;
        }
    }
}
```

**問題**: outputBuffer_は1サンプル分 (std::array<float, 4>) だが、バッファ全体を埋める必要がある。

**修正案**: generateHapticSample()をバッファ全体に対して呼び出す必要がある。

**より良い実装**:

```cpp
void ofApp::audioOut(ofSoundBuffer& output) {
    std::lock_guard<std::mutex> lock(audioMutex_);

    const std::size_t numFrames = output.getNumFrames();
    const std::size_t numChannels = output.getNumChannels();
    float* buffer = output.getBuffer().data();

    if (numFrames == 0 || numChannels == 0) {
        return;
    }

    // Get envelopes for both participants
    const auto metricsP1 = audioPipeline_.channelMetrics(ParticipantId::Participant1);
    const auto metricsP2 = audioPipeline_.channelMetrics(ParticipantId::Participant2);
    currentEnvelopes_[0] = metricsP1.envelope;
    currentEnvelopes_[1] = metricsP2.envelope;

    // Generate audio for each frame
    for (std::size_t frame = 0; frame < numFrames; ++frame) {
        // Route envelopes to output channels
        audioRouter_.route(currentEnvelopes_, outputBuffer_);

        // Write to interleaved buffer
        if (numChannels >= 4) {
            buffer[frame * numChannels + 0] = outputBuffer_[0];
            buffer[frame * numChannels + 1] = outputBuffer_[1];
            buffer[frame * numChannels + 2] = outputBuffer_[2];
            buffer[frame * numChannels + 3] = outputBuffer_[3];
        } else if (numChannels >= 2) {
            buffer[frame * numChannels + 0] = outputBuffer_[0];
            buffer[frame * numChannels + 1] = outputBuffer_[1];
        }
    }

    // Apply audio fade gain
    if (audioFadeGain_ < 0.99f) {
        for (std::size_t i = 0; i < numFrames * numChannels; ++i) {
            buffer[i] *= audioFadeGain_;
        }
    }
}
```

**注**: ハプティクス信号 (CH3/4) は毎サンプル生成が必要 (位相更新のため)。
エンベロープは変化しないが、route()内のgenerateHapticSample()が毎回呼ばれる必要がある。

#### テスト方法:

```bash
# 1. ビルド
make Release

# 2. CPU使用率測定
# Activity Monitor (macOS) でCPU使用率確認
# 目標: <30%

# 3. 長時間動作テスト
# 30分以上連続動作
# オーディオドロップアウトがないことを確認

# 4. ハプティクス波形確認
# CH3/4 が連続した50Hz正弦波であることを確認
# 位相が正しく更新されている
```

#### 成功基準:
- ✅ CPU使用率が低減
- ✅ ハプティクス信号が正しく生成される (毎サンプル)
- ✅ オーディオドロップアウトなし

#### リスク:
- **低**: ハプティクス生成の計算コスト
- **対策**: 後でルックアップテーブル最適化を検討

---

### **Unit 1.8: handleTransitionEvent: プリセット適用**

**ファイル**: `src/ofApp.cpp`
**変更箇所**: handleTransitionEvent() (L918付近)
**所要時間**: 15分

#### 背景:
シーン遷移完了時にAudioRouterのプリセットを自動適用する。

#### 実装指示:

**挿入位置**: Line 919 (event.completed ブロック内の最初)

```cpp
// 既存コード (L918-919):
    // シーン遷移完了時の処理
    if (event.completed) {

// 追加: (L920に追加)
        // Apply audio routing preset for the new scene
        {
            std::lock_guard<std::mutex> lock(audioMutex_);
            audioRouter_.applyScenePreset(event.to);
        }
        ofLogNotice("ofApp") << "Audio routing updated for scene: "
                              << sceneStateToString(event.to);

// 既存のフェードイン処理が続く...
```

#### テスト方法:

```bash
# 1. ビルド
make Release

# 2. 自動シーン遷移テスト
# Start → FirstPhase → Exchange → Mixed → End
# 各遷移でログに "Audio routing updated for scene: ..." が表示される

# 3. FirstPhase 確認
# 自分の心拍が聞こえる

# 4. Exchange 確認 (重要!)
# 相手の心拍が聞こえる (交換)
# これが成功すれば問題1が解決

# 5. Mixed 確認
# 両方の心拍が混合される

# 6. ベルとフェードの協調
# ベル音 → フェードアウト → ルーティング切替 → フェードイン
# スムーズな遷移
```

#### 成功基準:
- ✅ ビルドエラーなし
- ✅ シーン遷移時に自動でプリセット適用
- ✅ Exchange で心拍音が交換される ← **問題1解決**
- ✅ ベルとフェードが正しく動作

#### リスク:
- **低**: audioMutex_のデッドロック
- **対策**: lock_guardで適切にロック

---

### **Unit 1.9: 2ch独立メトリクス取得**

**ファイル**: `src/ofApp.cpp`
**変更箇所**: update() (L276-284)
**所要時間**: 30分

#### 背景:
現在は統合メトリクスのみ使用。2人分の独立したメトリクスを取得する。

#### 実装指示A: ofApp.hに変数追加

```cpp
// src/ofApp.h private members に追加 (latestMetrics_の近く):
private:
    // ... existing ...
    AudioPipeline::Metrics latestMetrics_{};

    // 追加:
    std::array<AudioPipeline::ChannelMetrics, 2> channelMetrics_{};
    float displayEnvelopeP1_ = 0.0f;
    float displayEnvelopeP2_ = 0.0f;
```

#### 実装指示B: update()で2ch独立取得

```cpp
// BEFORE (L276-291):
    const auto metrics = audioPipeline_.latestMetrics();
    const bool metricsAvailable =
        (metrics.timestampSec > 0.0) || (metrics.envelope > 0.0f) || (metrics.bpm > 0.0f);
    if (metricsAvailable) {
        applyBeatMetrics(metrics, nowSeconds);
        const auto events = audioPipeline_.pollBeatEvents();
        if (!events.empty()) {
            handleBeatEvents(events, nowSeconds);
        }
        limiterReductionDbSmooth_ =
            ofLerp(limiterReductionDbSmooth_, audioPipeline_.lastLimiterReductionDb(), 0.18f);
    } else {
        updateFakeSignal(nowSeconds);
        limiterReductionDbSmooth_ = ofLerp(limiterReductionDbSmooth_, 0.0f, 0.15f);
    }
    signalHealth_ = audioPipeline_.signalHealth();

// AFTER:
    // Get independent metrics for both participants
    {
        std::lock_guard<std::mutex> lock(audioMutex_);
        channelMetrics_[0] = audioPipeline_.channelMetrics(ParticipantId::Participant1);
        channelMetrics_[1] = audioPipeline_.channelMetrics(ParticipantId::Participant2);
    }

    // Check if either participant has valid metrics
    const bool metricsP1Available = (channelMetrics_[0].envelope > 0.0f) || (channelMetrics_[0].bpm > 0.0f);
    const bool metricsP2Available = (channelMetrics_[1].envelope > 0.0f) || (channelMetrics_[1].bpm > 0.0f);
    const bool anyMetricsAvailable = metricsP1Available || metricsP2Available;

    if (anyMetricsAvailable) {
        // Use Participant1 as primary for legacy metrics (for backward compatibility)
        if (metricsP1Available) {
            AudioPipeline::Metrics legacyMetrics;
            legacyMetrics.timestampSec = nowSeconds;
            legacyMetrics.envelope = channelMetrics_[0].envelope;
            legacyMetrics.bpm = channelMetrics_[0].bpm;
            applyBeatMetrics(legacyMetrics, nowSeconds);
        }

        // Poll beat events for both participants
        const auto eventsP1 = audioPipeline_.pollBeatEvents(ParticipantId::Participant1);
        const auto eventsP2 = audioPipeline_.pollBeatEvents(ParticipantId::Participant2);

        if (!eventsP1.empty()) {
            handleBeatEvents(eventsP1, nowSeconds);
        }
        if (!eventsP2.empty()) {
            // For now, handle P2 events the same way as P1
            // In future, could differentiate visual response per participant
            handleBeatEvents(eventsP2, nowSeconds);
        }

        limiterReductionDbSmooth_ =
            ofLerp(limiterReductionDbSmooth_, audioPipeline_.lastLimiterReductionDb(), 0.18f);
    } else {
        updateFakeSignal(nowSeconds);
        limiterReductionDbSmooth_ = ofLerp(limiterReductionDbSmooth_, 0.0f, 0.15f);
    }

    signalHealth_ = audioPipeline_.signalHealth();

    // Update display envelopes for both participants
    displayEnvelopeP1_ = channelMetrics_[0].envelope;
    displayEnvelopeP2_ = channelMetrics_[1].envelope;

    // Keep legacy displayEnvelope_ for backward compatibility (use P1)
    displayEnvelope_ = displayEnvelopeP1_;
```

#### テスト方法:

```bash
# 1. ビルド
make Release

# 2. 2つのマイクで心拍入力
# Participant1とParticipant2の両方に心拍信号を入力

# 3. ログ確認
# 両方のparticipantのメトリクスが取得されている

# 4. デバッグ出力追加 (オプション)
# update()に以下を追加してログ確認:
ofLogNotice("ofApp") << "P1 envelope: " << channelMetrics_[0].envelope
                      << ", P2 envelope: " << channelMetrics_[1].envelope;
```

#### 成功基準:
- ✅ ビルドエラーなし
- ✅ 2人分のメトリクスが独立して取得される
- ✅ displayEnvelopeP1_とdisplayEnvelopeP2_が更新される
- ✅ 既存の機能が引き続き動作

#### リスク:
- **低**: 既存のapplyBeatMetrics()との互換性
- **対策**: Participant1を主として使用

---

### **Unit 1.10: 2ch独立ビジュアル更新**

**ファイル**: `src/ofApp.cpp`
**変更箇所**: drawStarfield(), drawRipple() (L730-765)
**所要時間**: 45分

#### 背景:
現在は1つのエンベロープだけをシェーダーに渡している。2人分を独立して渡す。

#### 実装指示A: シェーダーにエンベロープ2つを渡す

```cpp
// BEFORE drawStarfield() (L730-748):
void ofApp::drawStarfield(SceneState scene, float alpha, double nowSeconds) {
    if (!starfieldShaderLoaded_) {
        return;
    }
    const float clampedAlpha = std::clamp(alpha, 0.0f, 1.0f);
    const float baseEnv = displayEnvelope_;
    const float pulseMix = 0.25f;
    const float env = baseEnv * pulseMix;

    starfieldShader_.begin();
    starfieldShader_.setUniform1f("uTime", static_cast<float>(nowSeconds));
    starfieldShader_.setUniform2f("uResolution", ofGetWidth(), ofGetHeight());
    starfieldShader_.setUniform1f("uEnvelope", env);
    starfieldShader_.setUniform1f("uAlpha", clampedAlpha);
    fullscreenQuadMesh().draw();
    starfieldShader_.end();
}

// AFTER:
void ofApp::drawStarfield(SceneState scene, float alpha, double nowSeconds) {
    if (!starfieldShaderLoaded_) {
        return;
    }
    const float clampedAlpha = std::clamp(alpha, 0.0f, 1.0f);

    // Get envelopes for both participants
    const float envP1 = displayEnvelopeP1_;
    const float envP2 = displayEnvelopeP2_;
    const float pulseMix = 0.25f;

    starfieldShader_.begin();
    starfieldShader_.setUniform1f("uTime", static_cast<float>(nowSeconds));
    starfieldShader_.setUniform2f("uResolution", ofGetWidth(), ofGetHeight());
    starfieldShader_.setUniform1f("uEnvelopeP1", envP1 * pulseMix);  // Participant 1
    starfieldShader_.setUniform1f("uEnvelopeP2", envP2 * pulseMix);  // Participant 2
    starfieldShader_.setUniform1f("uAlpha", clampedAlpha);
    fullscreenQuadMesh().draw();
    starfieldShader_.end();
}
```

**同様にdrawRipple()も変更**:

```cpp
// BEFORE drawRipple() (L750-765):
void ofApp::drawRipple(SceneState scene, float alpha, double nowSeconds) {
    if (!rippleShaderLoaded_) {
        return;
    }
    const float clampedAlpha = std::clamp(alpha, 0.0f, 1.0f);
    const float env = displayEnvelope_;

    rippleShader_.begin();
    rippleShader_.setUniform1f("uTime", static_cast<float>(nowSeconds));
    rippleShader_.setUniform2f("uResolution", ofGetWidth(), ofGetHeight());
    rippleShader_.setUniform1f("uEnvelope", env);
    rippleShader_.setUniform1f("uAlpha", clampedAlpha);
    fullscreenQuadMesh().draw();
    rippleShader_.end();
}

// AFTER:
void ofApp::drawRipple(SceneState scene, float alpha, double nowSeconds) {
    if (!rippleShaderLoaded_) {
        return;
    }
    const float clampedAlpha = std::clamp(alpha, 0.0f, 1.0f);

    // Get envelopes for both participants
    const float envP1 = displayEnvelopeP1_;
    const float envP2 = displayEnvelopeP2_;

    rippleShader_.begin();
    rippleShader_.setUniform1f("uTime", static_cast<float>(nowSeconds));
    rippleShader_.setUniform2f("uResolution", ofGetWidth(), ofGetHeight());
    rippleShader_.setUniform1f("uEnvelopeP1", envP1);  // Participant 1
    rippleShader_.setUniform1f("uEnvelopeP2", envP2);  // Participant 2
    rippleShader_.setUniform1f("uAlpha", clampedAlpha);
    fullscreenQuadMesh().draw();
    rippleShader_.end();
}
```

#### テスト方法:

```bash
# 1. ビルド
make Release

# 2. 2つのマイクで心拍入力

# 3. ビジュアル確認 (重要!)
# FirstPhaseシーンで:
#   - 2つの独立したビジュアル要素が見える (Unit 1.11/1.12完了後)
#   - Participant1の心拍 → 左側のビジュアル
#   - Participant2の心拍 → 右側のビジュアル

# 4. 片方のマイクを外すテスト
# Participant1のみ入力 → P1のビジュアルだけ動く
# Participant2のみ入力 → P2のビジュアルだけ動く
# これが成功すれば問題2が解決 ← **問題2解決**
```

#### 成功基準:
- ✅ ビルドエラーなし
- ✅ シェーダーに2つのエンベロープが渡される
- ✅ 2人分の独立したビジュアルが表示される (シェーダー修正後)
- ✅ 片方のマイクだけでも正しく動作 ← **問題2解決**

#### リスク:
- **低**: シェーダーがuEnvelopeP1/P2に未対応
- **対策**: Unit 1.11/1.12でシェーダー修正

---

### **Unit 1.11: シェーダー2ch対応 (starfield)**

**ファイル**: `bin/data/shaders/starfield.frag`
**変更箇所**: uniform追加、2人分の星表示
**所要時間**: 30分

#### 背景:
現在は1つのuEnvelopeのみ。2つのenvelopeで左右に分けて星を表示。

#### 実装指示:

```glsl
// BEFORE (既存のuEnvelope):
uniform float uEnvelope;

// AFTER (2つのenvelopeに対応):
uniform float uEnvelopeP1;  // Participant 1
uniform float uEnvelopeP2;  // Participant 2

// ... existing hash functions ...

void main() {
    vec2 uv = gl_FragCoord.xy / uResolution;

    // Split screen: left half = P1, right half = P2
    float envelope = (uv.x < 0.5) ? uEnvelopeP1 : uEnvelopeP2;

    // Adjust UV for each half to maintain aspect ratio
    vec2 localUV = uv;
    if (uv.x < 0.5) {
        localUV.x = uv.x * 2.0;  // Scale left half to [0, 1]
    } else {
        localUV.x = (uv.x - 0.5) * 2.0;  // Scale right half to [0, 1]
    }

    // Generate starfield (existing logic)
    vec2 cell = floor(localUV * 800.0);
    float h = hash21(cell + floor(uTime * 0.2));
    float sparkle = pow(h, 80.0) * smoothstep(0.995, 1.0, hash21(cell + uTime));
    float star = sparkle * envelope;

    // Background and star color
    float background = 0.02;
    vec3 starColor = vec3(0.65, 0.75, 0.95) * star;
    vec3 baseColor = vec3(background);

    // Add subtle blue tint for P1 (left), purple tint for P2 (right)
    if (uv.x < 0.5) {
        baseColor += vec3(0.0, 0.0, 0.02);  // Slight blue for P1
    } else {
        baseColor += vec3(0.02, 0.0, 0.02);  // Slight purple for P2
    }

    vec3 finalColor = baseColor + starColor;
    gl_FragColor = vec4(finalColor, uAlpha);
}
```

#### テスト方法:

```bash
# 1. ビルド
make Release

# 2. 2つのマイクで心拍入力

# 3. 画面分割確認
# 左半分: Participant1の星空
# 右半分: Participant2の星空

# 4. 独立動作確認
# P1だけ心拍 → 左側だけ星が輝く
# P2だけ心拍 → 右側だけ星が輝く
# 両方 → 両側が独立して輝く

# 5. 色調確認
# 左側: やや青み
# 右側: やや紫
# 視覚的に区別できる
```

#### 成功基準:
- ✅ シェーダーがビルドエラーなし
- ✅ 画面が左右に分割される
- ✅ 各参加者の心拍が独立して表示される
- ✅ 色調で視覚的に区別できる

#### リスク:
- **低**: 画面分割で違和感
- **対策**: 後でよりエレガントな2人表示方法を検討

---

### **Unit 1.12: シェーダー2ch対応 (ripple)**

**ファイル**: `bin/data/shaders/ripple.frag`
**変更箇所**: uniform追加、2つの中心からリプル
**所要時間**: 30分

#### 背景:
現在は1つのuEnvelopeのみ。2つのenvelopeで2つの中心からリプルを表示。

#### 実装指示:

```glsl
// BEFORE (既存のuEnvelope):
uniform float uEnvelope;

// AFTER (2つのenvelopeに対応):
uniform float uEnvelopeP1;  // Participant 1
uniform float uEnvelopeP2;  // Participant 2

void main() {
    vec2 uv = gl_FragCoord.xy / uResolution;
    vec2 center = vec2(0.5, 0.5);

    // Two ripple centers: left (P1) and right (P2)
    vec2 centerP1 = vec2(0.25, 0.5);  // Left quarter
    vec2 centerP2 = vec2(0.75, 0.5);  // Right quarter

    // Distance from each center
    float distP1 = length(uv - centerP1);
    float distP2 = length(uv - centerP2);

    // Ripple parameters
    float speed = 0.5;
    float frequency = 15.0;
    float thickness = 0.02;

    // Ripple 1 (Participant 1)
    float ripple1 = abs(sin((distP1 - uTime * speed) * frequency));
    ripple1 = smoothstep(thickness, 0.0, ripple1);
    float intensity1 = ripple1 * uEnvelopeP1;
    float fadeEdge1 = smoothstep(0.6, 0.0, distP1);
    intensity1 *= fadeEdge1;

    // Ripple 2 (Participant 2)
    float ripple2 = abs(sin((distP2 - uTime * speed) * frequency));
    ripple2 = smoothstep(thickness, 0.0, ripple2);
    float intensity2 = ripple2 * uEnvelopeP2;
    float fadeEdge2 = smoothstep(0.6, 0.0, distP2);
    intensity2 *= fadeEdge2;

    // Color gradients
    vec3 innerColorP1 = vec3(0.4, 0.55, 0.85);   // Blue for P1
    vec3 outerColorP1 = vec3(0.15, 0.22, 0.45);

    vec3 innerColorP2 = vec3(0.85, 0.4, 0.75);   // Purple for P2
    vec3 outerColorP2 = vec3(0.45, 0.15, 0.35);

    vec3 colorP1 = mix(outerColorP1, innerColorP1, intensity1) * intensity1;
    vec3 colorP2 = mix(outerColorP2, innerColorP2, intensity2) * intensity2;

    // Combine ripples
    vec3 finalColor = colorP1 + colorP2;

    gl_FragColor = vec4(finalColor, uAlpha);
}
```

#### テスト方法:

```bash
# 1. ビルド
make Release

# 2. 2つのマイクで心拍入力

# 3. リプル中心確認
# 左側 (25%位置): Participant1のリプル (青)
# 右側 (75%位置): Participant2のリプル (紫)

# 4. 独立動作確認
# P1だけ心拍 → 左側だけリプル
# P2だけ心拍 → 右側だけリプル
# 両方 → 両側が独立してリプル

# 5. 色確認
# P1: 青系グラディエント
# P2: 紫系グラディエント
# 視覚的に明確に区別できる
```

#### 成功基準:
- ✅ シェーダーがビルドエラーなし
- ✅ 2つの独立したリプルが表示される
- ✅ 各参加者の心拍が独立して表示される
- ✅ 色で視覚的に区別できる

#### リスク:
- **低**: リプルが重なって見にくい
- **対策**: 色の差をより明確にする

---

## ✅ Phase 1 完了チェックリスト

全Unit完了後、以下を確認:

- [ ] **Unit 1.1**: 4チャンネル出力有効化
- [ ] **Unit 1.2**: AudioRouterメンバー追加
- [ ] **Unit 1.3**: AudioRouterセットアップ
- [ ] **Unit 1.4**: generateHapticSample実装
- [ ] **Unit 1.5**: applyScenePreset実装
- [ ] **Unit 1.6**: audioOut統合: エンベロープ取得
- [ ] **Unit 1.7**: audioOut統合: パフォーマンス最適化
- [ ] **Unit 1.8**: handleTransitionEvent: プリセット適用
- [ ] **Unit 1.9**: 2ch独立メトリクス取得
- [ ] **Unit 1.10**: 2ch独立ビジュアル更新
- [ ] **Unit 1.11**: シェーダー2ch対応 (starfield)
- [ ] **Unit 1.12**: シェーダー2ch対応 (ripple)

---

## 🧪 Phase 1 統合テスト

全Unit完了後、以下のシナリオで統合テスト:

### テストシナリオ1: 2人デモ完全フロー

```
1. 2名の参加者がマイクを装着
2. キャリブレーション実行
3. Start → FirstPhase 自動遷移
   - ヘッドフォン: 各自が自分の心拍を聞く
   - ビジュアル: 左側にP1、右側にP2の独立した表示
   - ハプティクス: 各自の振動 (CH3=P1, CH4=P2)
4. FirstPhase → Exchange 自動遷移
   - ベル音再生
   - 10秒フェードアウト → ルーティング切替 → 10秒フェードイン
   - ヘッドフォン: 相手の心拍が聞こえる (交換) ← **問題1解決確認**
   - ビジュアル: 引き続き独立表示 ← **問題2解決確認**
   - ハプティクス: 自分の振動 (交換しない)
5. Exchange → Mixed 自動遷移
   - 両方の心拍がステレオミックスで聞こえる
   - ビジュアル: 両側表示
6. Mixed → End → Idle
   - フェードアウト、自動復帰
```

### テストシナリオ2: 片方のマイクのみ

```
1. Participant1のマイクだけ装着
2. Start → FirstPhase
   - 左側のビジュアルだけ動く ← **問題2解決確認**
   - 右側は静止
3. Participant2のマイクだけ装着
4. FirstPhase 再開
   - 右側のビジュアルだけ動く ← **問題2解決確認**
   - 左側は静止
```

### テストシナリオ3: ハプティクス動作

```
1. CH3/4 をオシロスコープまたは実機 (Dayton Audio DAEX25) に接続
2. FirstPhase で心拍入力
3. CH3 に50Hz正弦波が出力される (P1の振動)
4. CH4 に50Hz正弦波が出力される (P2の振動)
5. エンベロープに応じて振幅が変化する ← **問題4解決確認**
```

---

## 🎯 Phase 1 成功基準

Phase 1が成功したと判断できる基準:

### 機能完全性:
- ✅ **問題1解決**: Exchangeシーンで心拍音が交換される
- ✅ **問題2解決**: 2人の心拍が独立してビジュアルに表示される
- ✅ **問題4解決**: CH3/4にハプティクス信号が出力される

### 品質基準:
- ✅ 30分以上の連続動作でクラッシュなし
- ✅ オーディオドロップアウトなし
- ✅ CPU使用率 <30%
- ✅ 全Unitのテストが成功

### ユーザー体験:
- ✅ シーン遷移がスムーズ
- ✅ 2人の相互作用が明確に感じられる
- ✅ ビジュアルが2人を区別して表示
- ✅ ハプティクス振動が心地よい

---

## 📞 トラブルシューティング

### 問題: CH3/4 に音が出ない

**確認事項**:
1. Unit 1.1: numOutputChannels = 4 になっているか
2. Unit 1.4: generateHapticSample() が実装されているか
3. Unit 1.6: audioOut() でルーティングが呼ばれているか
4. オーディオインターフェースが4ch対応か

**デバッグ**:
```cpp
// audioOut()に追加:
static int debugCounter = 0;
if (debugCounter++ % 4800 == 0) {  // 0.1秒ごと
    ofLogNotice("audioOut") << "CH3: " << outputBuffer_[2] << ", CH4: " << outputBuffer_[3];
}
```

### 問題: Exchangeで音が切り替わらない

**確認事項**:
1. Unit 1.5: applyScenePreset() が実装されているか
2. Unit 1.8: handleTransitionEvent() でプリセット適用されているか
3. ログに "Audio routing updated for scene: Exchange" が表示されるか

**デバッグ**:
```cpp
// applyScenePreset()の最初に追加:
ofLogNotice("AudioRouter") << "Applying preset for: " << sceneStateToString(scene);
for (std::size_t i = 0; i < rules_.size(); ++i) {
    ofLogNotice() << "  CH" << i << ": source=" << static_cast<int>(rules_[i].source)
                   << ", mode=" << static_cast<int>(rules_[i].mixMode);
}
```

### 問題: 片方のビジュアルしか動かない

**確認事項**:
1. Unit 1.9: channelMetrics_[0]とchannelMetrics_[1]が両方更新されているか
2. Unit 1.10: displayEnvelopeP1_とdisplayEnvelopeP2_が正しく設定されているか
3. Unit 1.11/1.12: シェーダーがuEnvelopeP1/P2を受け取っているか

**デバッグ**:
```cpp
// update()に追加:
ofLogNotice("update") << "P1: " << channelMetrics_[0].envelope
                       << ", P2: " << channelMetrics_[1].envelope;
```

---

## 📚 Phase 2 への準備

Phase 1完了後、Phase 2 (GUI/柔軟性) に移行します:

- ルーティングGUI実装
- 1人モード (Synthetic心拍)
- 動的ルーティング変更
- テスト機能追加

Phase 2の詳細は **REQUIREMENTS_PHASE2_GUI_FLEXIBILITY.md** を参照してください。

---

**最終更新**: 2025-10-29
**ドキュメントバージョン**: 1.0
**対象リリース**: MVP Phase 1
