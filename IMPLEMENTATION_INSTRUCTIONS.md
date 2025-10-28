# 実装指示書: 2チャンネル独立処理と4チャンネル出力ルーティング

**作成日**: 2025-10-29
**対象**: 開発チーム
**目的**: 2名参加または1名参加デモを機能させるための最小単位実装指示

---

## 📋 実装状況サマリー

### ✅ 既存実装済み (Phase 0-1相当)

| コンポーネント | ファイル | 状態 | 証拠コード箇所 |
|--------------|---------|------|--------------|
| 2チャンネル入力分離 | AudioPipeline.cpp | **完了** | L203-204: `channelBuffers_[0][frame] = ch1;` |
| 独立BeatTimeline処理 | AudioPipeline.cpp | **完了** | L211: `beatTimelines_[channel].processBuffer(...)` |
| チャンネル別メトリクス | AudioPipeline.cpp | **完了** | L215-219: `channelMetric.bpm/envelope` |
| ParticipantId定義 | ParticipantId.h | **完了** | L7-12: enum class定義 |
| AudioRouterフレームワーク | AudioRouter.h | **完了** | L24-50: RoutingRule, OutputChannel定義 |
| ビジュアル改善 | starfield.frag, ripple.frag | **完了** | 星空・リプル修正済み |
| ベル音・フェード | ofApp.cpp | **完了** | L452-460, L883-917 |

### ❌ 未実装・スタブ状態 (Phase 2-4相当)

| コンポーネント | ファイル | 問題 | 影響 |
|--------------|---------|------|------|
| 4チャンネル出力設定 | ofApp.cpp:172 | `numOutputChannels = 2` | **致命的**: ハプティクス出力不可 |
| AudioRouter統合 | ofApp.h, ofApp.cpp | AudioRouterインスタンス未作成 | **致命的**: ルーティング機能動作せず |
| ハプティクス信号生成 | AudioRouter.cpp:71-75 | `return 0.0f;` スタブ | **致命的**: 振動出力ゼロ |
| シーンプリセット適用 | AudioRouter.cpp:34-37 | 空実装 | **重大**: シーン別ルーティング不可 |
| ルーティングGUI | 存在しない | GUI未実装 | **重大**: 動的設定不可 |
| route()関数呼び出し | ofApp.cpp audioOut() | 未呼び出し | **致命的**: ルーティング未適用 |

---

## 🎯 実装単位と指示

各単位は独立してテスト可能な最小実装です。順序通りに実施してください。

---

### **Unit 1: 4チャンネル出力有効化**

**ファイル**: `src/ofApp.cpp`
**変更箇所**: Line 172
**優先度**: 🔴 致命的 (これがないと全ての4ch機能が動作しない)

#### 指示:

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

# 2. オーディオデバイスログ確認
# 起動時コンソールに "Output channels: 4" と表示されることを確認

# 3. オーディオインターフェース設定確認
# macOS: Audio MIDI Setup.app で出力チャンネル数を確認
# CH1-CH4 が全てアクティブであることを確認
```

#### 成功基準:
- ビルドエラーなし
- アプリケーション起動時にクラッシュしない
- オーディオ出力チャンネル数が4と認識される
- 既存の2ch音声出力(CH1/2)が引き続き動作する

#### リスク:
- **失敗時の影響**: 全ての4chルーティング機能が動作不可
- **注意点**: オーディオインターフェースが4ch出力に対応していることを事前確認

---

### **Unit 2: AudioRouterインスタンス作成と初期化**

**ファイル**: `src/ofApp.h`, `src/ofApp.cpp`
**優先度**: 🔴 致命的 (ルーティング機能の基盤)

#### 指示A: ofApp.hにメンバー追加

**挿入位置**: Line 180付近 (bellSound_などの後)

```cpp
// Add to private members:
#include "audio/AudioRouter.h"

private:
    // ... existing members ...

    // Audio routing
    knot::audio::AudioRouter audioRouter_;
    std::array<float, 2> currentEnvelopes_{0.0f, 0.0f};
    std::array<float, 4> outputBuffer_{0.0f, 0.0f, 0.0f, 0.0f};
```

#### 指示B: ofApp.cpp setup()で初期化

**挿入位置**: Line 165付近 (audioPipeline_.setup()の直後)

```cpp
void ofApp::setup() {
    // ... existing code ...

    audioPipeline_.setup(sampleRate, inputGainDb);

    // Initialize audio router
    audioRouter_.setup();
    audioRouter_.applyScenePreset(sceneController_.currentScene());

    // ... rest of setup ...
}
```

#### テスト方法:

```bash
# 1. ビルド
make Release

# 2. 起動確認
# クラッシュせずに起動することを確認

# 3. デバッグログ確認 (必要に応じてログ追加)
# AudioRouter::setup() が呼ばれていることをログで確認
```

#### 成功基準:
- ビルドエラーなし
- アプリケーション起動時にクラッシュしない
- AudioRouterのsetup()が正常に呼ばれる

#### リスク:
- **失敗時の影響**: 全てのルーティング機能が動作不可
- **依存関係**: Unit 1が完了していること

---

### **Unit 3: audioOut()でAudioRouter::route()呼び出し**

**ファイル**: `src/ofApp.cpp`
**変更箇所**: audioOut()関数 (L452-505付近)
**優先度**: 🔴 致命的 (実際のルーティング適用)

#### 指示: audioOut()の最後にルーティング処理追加

**挿入位置**: Line 500付近 (既存のバッファ書き込み処理の直前)

```cpp
void ofApp::audioOut(ofSoundBuffer& buffer) {
    std::lock_guard<std::mutex> lock(audioMutex_);

    float* output = buffer.getBuffer().data();
    const std::size_t numFrames = buffer.getNumFrames();
    const std::size_t numChannels = buffer.getNumChannels();

    // ... existing audio generation code ...

    // NEW: Apply audio routing for each frame
    for (std::size_t frame = 0; frame < numFrames; ++frame) {
        // Update current envelopes from AudioPipeline
        const auto metricsP1 = audioPipeline_.channelMetrics(ParticipantId::Participant1);
        const auto metricsP2 = audioPipeline_.channelMetrics(ParticipantId::Participant2);
        currentEnvelopes_[0] = metricsP1.envelope;
        currentEnvelopes_[1] = metricsP2.envelope;

        // Route to 4 channels
        audioRouter_.route(currentEnvelopes_, outputBuffer_);

        // Write to output buffer
        if (numChannels == 4) {
            output[frame * 4 + 0] = outputBuffer_[0];  // CH1: Headphone L
            output[frame * 4 + 1] = outputBuffer_[1];  // CH2: Headphone R
            output[frame * 4 + 2] = outputBuffer_[2];  // CH3: Haptic P1
            output[frame * 4 + 3] = outputBuffer_[3];  // CH4: Haptic P2
        } else if (numChannels == 2) {
            // Fallback for 2ch mode
            output[frame * 2 + 0] = outputBuffer_[0];
            output[frame * 2 + 1] = outputBuffer_[1];
        }
    }

    // Apply fade gain if active
    if (audioFadeGain_ < 0.99f) {
        for (std::size_t i = 0; i < buffer.size(); ++i) {
            output[i] *= audioFadeGain_;
        }
    }
}
```

#### テスト方法:

```bash
# 1. ビルド
make Release

# 2. 心拍入力テスト
# 2つのマイクに心拍信号を入力
# または既存のテストモード使用

# 3. CH1/2 ヘッドフォン出力確認
# ヘッドフォンで音声が聞こえることを確認

# 4. CH3/4 ハプティクス出力確認 (現時点では0.0fが出力される)
# オシロスコープまたはDAWで波形確認
# CH3/4が全てゼロでも、ルーティングが呼ばれていることを確認
```

#### 成功基準:
- ビルドエラーなし
- 既存の音声出力(CH1/2)が引き続き動作
- audioRouter_.route()が毎フレーム呼ばれる(ログで確認)
- CH3/4 に出力がある(現時点では0.0fでも可)

#### リスク:
- **失敗時の影響**: ルーティング機能が全く動作しない
- **依存関係**: Unit 1, 2が完了していること
- **パフォーマンス**: 毎フレーム処理なので最適化必要(後のUnit 7で対応)

---

### **Unit 4: ハプティクス信号生成実装**

**ファイル**: `src/audio/AudioRouter.cpp`
**変更箇所**: generateHapticSample() (L71-75)
**優先度**: 🔴 致命的 (ハプティクス出力の実体)

#### 背景知識:
- ハプティクストランスデューサーの動作周波数: 20-150Hz
- 心拍信号のエンベロープから低周波パルスを生成
- BPMに応じた周波数でパルスを生成

#### 指示: generateHapticSample()のスタブを実装

```cpp
// BEFORE (L71-75):
float AudioRouter::generateHapticSample(float envelope, ParticipantId id) {
    (void)id;
    (void)envelope;
    return 0.0f;
}

// AFTER:
float AudioRouter::generateHapticSample(float envelope, ParticipantId id) {
    // Use envelope as amplitude modulation
    // Generate low-frequency pulse for haptic transducer (20-150Hz range)

    // Simple implementation: 50Hz sine wave modulated by envelope
    constexpr float kHapticFrequency = 50.0f;  // Hz, within 20-150Hz range
    constexpr float kHapticGain = 0.8f;  // Adjust for transducer sensitivity

    // Phase accumulator per participant
    static std::array<double, 2> phase = {0.0, 0.0};
    const std::size_t idx = static_cast<std::size_t>(id);

    if (idx >= 2) {
        return 0.0f;  // Invalid participant or Synthetic
    }

    // Generate sine wave
    const float sample = std::sin(static_cast<float>(phase[idx] * 2.0 * M_PI));

    // Advance phase
    phase[idx] += kHapticFrequency / sampleRate_;
    if (phase[idx] >= 1.0) {
        phase[idx] -= 1.0;
    }

    // Modulate by envelope and apply gain
    return sample * envelope * kHapticGain;
}
```

#### テスト方法:

```bash
# 1. ビルド
make Release

# 2. ハプティクス出力確認
# CH3/4 をオシロスコープまたはDAWで記録

# 3. 周波数解析
# CH3/4 の信号が50Hz付近の正弦波であることを確認
# FFTスペクトラムで20-150Hzにピークがあることを確認

# 4. エンベロープ連動確認
# 心拍が強いときにハプティクス振幅が増加することを確認
# 心拍がないときにハプティクス振幅がゼロになることを確認

# 5. 実機テスト (Dayton Audio DAEX25 等)
# ハプティクストランスデューサーに接続
# 振動が感じられることを確認
```

#### 成功基準:
- ビルドエラーなし
- CH3/4 に50Hz正弦波が出力される
- エンベロープに応じて振幅が変化する
- ハプティクストランスデューサーで振動を感じる

#### リスク:
- **失敗時の影響**: ハプティクス出力がゼロのまま、触覚フィードバックなし
- **調整必要**: kHapticFrequency, kHapticGain はハードウェアに応じて調整
- **依存関係**: Unit 1-3が完了していること

#### 改善案 (後のフェーズで実装):
- BPMに応じた周波数変調 (60BPM → 1Hz パルス包絡)
- バンドパスフィルタ (20-150Hz)
- エンベロープのスムージング

---

### **Unit 5: シーン別ルーティングプリセット実装**

**ファイル**: `src/audio/AudioRouter.cpp`
**変更箇所**: applyScenePreset() (L34-37)
**優先度**: 🟡 重大 (シーン遷移時の音響切り替え)

#### 背景知識:
- Idle: 出力なし
- Start: ガイダンスのみ (ルーティング不要)
- FirstPhase: 自分の心拍のみ聞こえる
- Exchange: 相手の心拍が聞こえる
- Mixed: 両方混合
- End: フェードアウト

#### 指示: applyScenePreset()の実装

```cpp
// BEFORE (L34-37):
void AudioRouter::applyScenePreset(SceneState scene) {
    (void)scene;
    // Phase 4: Scene-dependent routing will be implemented later.
}

// AFTER:
void AudioRouter::applyScenePreset(SceneState scene) {
    using namespace knot::audio;

    rules_.clear();

    switch (scene) {
    case SceneState::Idle:
    case SceneState::Start:
        // No routing during Idle/Start (guidance audio only)
        break;

    case SceneState::FirstPhase:
        // Each participant hears their own heartbeat
        // CH1 (Headphone L): Participant1's own beat
        // CH2 (Headphone R): Participant2's own beat
        // CH3 (Haptic P1): Participant1's haptic
        // CH4 (Haptic P2): Participant2's haptic
        rules_.push_back({ParticipantId::Participant1, OutputChannel::CH1_HeadphoneLeft,
                         MixMode::Self, 0.0f, -1.0f});  // Full left
        rules_.push_back({ParticipantId::Participant2, OutputChannel::CH2_HeadphoneRight,
                         MixMode::Self, 0.0f, 1.0f});   // Full right
        rules_.push_back({ParticipantId::Participant1, OutputChannel::CH3_HapticP1,
                         MixMode::Haptic, 0.0f, 0.0f});
        rules_.push_back({ParticipantId::Participant2, OutputChannel::CH4_HapticP2,
                         MixMode::Haptic, 0.0f, 0.0f});
        break;

    case SceneState::Exchange:
        // Participants hear PARTNER's heartbeat
        // CH1: Participant2's beat → Participant1 hears
        // CH2: Participant1's beat → Participant2 hears
        rules_.push_back({ParticipantId::Participant2, OutputChannel::CH1_HeadphoneLeft,
                         MixMode::Partner, 0.0f, -1.0f});
        rules_.push_back({ParticipantId::Participant1, OutputChannel::CH2_HeadphoneRight,
                         MixMode::Partner, 0.0f, 1.0f});
        rules_.push_back({ParticipantId::Participant1, OutputChannel::CH3_HapticP1,
                         MixMode::Haptic, 0.0f, 0.0f});
        rules_.push_back({ParticipantId::Participant2, OutputChannel::CH4_HapticP2,
                         MixMode::Haptic, 0.0f, 0.0f});
        break;

    case SceneState::Mixed:
        // Mix both participants' heartbeats in stereo
        // CH1 (L): P1 (loud) + P2 (soft)
        // CH2 (R): P2 (loud) + P1 (soft)
        rules_.push_back({ParticipantId::Participant1, OutputChannel::CH1_HeadphoneLeft,
                         MixMode::Self, -3.0f, -0.5f});    // -3dB, slight left
        rules_.push_back({ParticipantId::Participant2, OutputChannel::CH1_HeadphoneLeft,
                         MixMode::Partner, -9.0f, -0.5f}); // -9dB, background
        rules_.push_back({ParticipantId::Participant2, OutputChannel::CH2_HeadphoneRight,
                         MixMode::Self, -3.0f, 0.5f});     // -3dB, slight right
        rules_.push_back({ParticipantId::Participant1, OutputChannel::CH2_HeadphoneRight,
                         MixMode::Partner, -9.0f, 0.5f});  // -9dB, background
        rules_.push_back({ParticipantId::Participant1, OutputChannel::CH3_HapticP1,
                         MixMode::Haptic, 0.0f, 0.0f});
        rules_.push_back({ParticipantId::Participant2, OutputChannel::CH4_HapticP2,
                         MixMode::Haptic, 0.0f, 0.0f});
        break;

    case SceneState::End:
        // Fade out handled by global fade mechanism
        // Keep Mixed routing
        applyScenePreset(SceneState::Mixed);
        break;
    }
}
```

#### テスト方法:

```bash
# 1. ビルド
make Release

# 2. シーン遷移テスト
# Idle → Start → FirstPhase
# FirstPhase: 自分の心拍が自分のチャンネルで聞こえることを確認

# 3. Exchange遷移テスト
# FirstPhase → Exchange
# Exchange: 相手の心拍が聞こえることを確認
# P1のヘッドフォンでP2の心拍音を確認
# P2のヘッドフォンでP1の心拍音を確認

# 4. Mixed遷移テスト
# Exchange → Mixed
# Mixed: 両方の心拍が混合されて聞こえることを確認
# ステレオバランスを確認 (各参加者が自分寄りに聞こえる)

# 5. ログ確認
# applyScenePreset()が各シーン遷移で呼ばれていることを確認
# rules_の内容が想定通りであることを確認
```

#### 成功基準:
- ビルドエラーなし
- FirstPhase: 自分の心拍のみ聞こえる
- Exchange: 相手の心拍が聞こえる
- Mixed: 両方の心拍が混合される
- ハプティクスが全シーンで動作する

#### リスク:
- **失敗時の影響**: シーン遷移時に音響が切り替わらない、体験が単調
- **依存関係**: Unit 1-4が完了していること
- **調整必要**: gainDb, panLR の値は試聴して調整

---

### **Unit 6: ofApp側でシーン遷移時にプリセット適用**

**ファイル**: `src/ofApp.cpp`
**変更箇所**: handleTransitionEvent() (L883付近)
**優先度**: 🟡 重大 (プリセット自動適用)

#### 指示: handleTransitionEvent()でプリセット適用

**挿入位置**: Line 890付近 (event.completedチェックの後)

```cpp
void ofApp::handleTransitionEvent(const SceneTransitionEvent& event) {
    // ... existing bell sound and fade logic ...

    if (event.completed) {
        // NEW: Apply audio routing preset for new scene
        audioRouter_.applyScenePreset(event.to);

        // ... existing fade in logic ...
    }
}
```

#### テスト方法:

```bash
# 1. ビルド
make Release

# 2. 自動シーン遷移テスト
# scene_timing.json の設定に従って自動遷移を確認
# Start(30s) → FirstPhase(60s) → Exchange(60s) → Mixed(90s) → End(20s)

# 3. 各シーンでルーティング確認
# 自動遷移時にapplyScenePreset()が呼ばれることを確認
# 音響が各シーンで正しく切り替わることを確認

# 4. ベルとフェードの協調確認
# ベルが鳴る → フェードアウト → ルーティング切替 → フェードイン
# この流れがスムーズであることを確認
```

#### 成功基準:
- シーン遷移時に自動でプリセットが適用される
- ベル音とフェードが正しく動作
- ルーティング切替がユーザーに違和感なく伝わる

#### リスク:
- **失敗時の影響**: 手動でプリセット適用が必要、自動デモが動作しない
- **依存関係**: Unit 5が完了していること

---

### **Unit 7: パフォーマンス最適化 (エンベロープ更新頻度削減)**

**ファイル**: `src/ofApp.cpp`
**優先度**: 🟢 通常 (パフォーマンス改善)

#### 背景:
- Unit 3でエンベロープを毎フレーム更新しているが、心拍エンベロープは高頻度更新不要
- 512サンプルバッファなら1回の更新で十分

#### 指示: update()でエンベロープを更新し、audioOut()で再利用

```cpp
// ofApp.h に追加:
private:
    std::array<float, 2> cachedEnvelopes_{0.0f, 0.0f};

// ofApp.cpp update() に追加:
void ofApp::update() {
    // ... existing code ...

    // Update envelopes once per frame (not per audio sample)
    std::lock_guard<std::mutex> lock(audioMutex_);
    const auto metricsP1 = audioPipeline_.channelMetrics(ParticipantId::Participant1);
    const auto metricsP2 = audioPipeline_.channelMetrics(ParticipantId::Participant2);
    cachedEnvelopes_[0] = metricsP1.envelope;
    cachedEnvelopes_[1] = metricsP2.envelope;
}

// ofApp.cpp audioOut() を変更:
void ofApp::audioOut(ofSoundBuffer& buffer) {
    std::lock_guard<std::mutex> lock(audioMutex_);

    // Use cached envelopes instead of querying every frame
    audioRouter_.route(cachedEnvelopes_, outputBuffer_);

    // ... rest of audioOut ...
}
```

#### テスト方法:

```bash
# 1. ビルド
make Release

# 2. CPU使用率測定
# Activity Monitor (macOS) または Task Manager (Windows)
# 最適化前後でCPU使用率を比較

# 3. オーディオドロップアウト確認
# 長時間実行 (30分以上)
# オーディオのグリッチやドロップアウトがないことを確認

# 4. プロファイリング (オプション)
# Instruments (macOS) でTime Profilerを実行
# audioOut()の実行時間が短縮されていることを確認
```

#### 成功基準:
- CPU使用率が低減
- オーディオ出力品質に変化なし
- 長時間動作でもドロップアウトなし

---

### **Unit 8: ルーティングGUI実装 (ImGui)**

**ファイル**: `src/ofApp.cpp`, `src/ofApp.h`
**優先度**: 🟡 重大 (動的設定に必須)

#### 背景:
- 現在のルーティングはコードで固定
- デモ時に柔軟な設定が必要 (1人モード、2人モード、テストモード等)

#### 指示A: ofApp.h にGUI状態変数追加

```cpp
// ofApp.h private members:
private:
    // Routing GUI state
    bool showRoutingPanel_ = false;
    int selectedOutputCh_ = 0;  // 0=CH1, 1=CH2, 2=CH3, 3=CH4
    int selectedSourceParticipant_ = 0;  // 0=P1, 1=P2, 2=Synthetic
    int selectedMixMode_ = 0;  // 0=Self, 1=Partner, 2=Haptic, 3=Silent
    float manualGainDb_ = 0.0f;
    float manualPan_ = 0.0f;
```

#### 指示B: ofApp.cpp にGUIパネル描画追加

**挿入位置**: drawGui() (L750付近)

```cpp
void ofApp::drawGui() {
    // ... existing GUI code ...

    if (ImGui::CollapsingHeader("Audio Routing", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::Button("Show Routing Panel")) {
            showRoutingPanel_ = !showRoutingPanel_;
        }

        // Display current routing rules
        ImGui::Text("Active Rules: %zu", audioRouter_.rules().size());

        if (ImGui::BeginTable("RoutingRules", 5, ImGuiTableFlags_Borders)) {
            ImGui::TableSetupColumn("Output");
            ImGui::TableSetupColumn("Source");
            ImGui::TableSetupColumn("Mode");
            ImGui::TableSetupColumn("Gain (dB)");
            ImGui::TableSetupColumn("Pan");
            ImGui::TableHeadersRow();

            for (const auto& rule : audioRouter_.rules()) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::Text("CH%d", static_cast<int>(rule.outputChannel));
                ImGui::TableNextColumn();
                ImGui::Text("P%d", static_cast<int>(rule.sourceParticipant));
                ImGui::TableNextColumn();
                ImGui::Text("%s", mixModeToString(rule.mode));
                ImGui::TableNextColumn();
                ImGui::Text("%.1f", rule.gainDb);
                ImGui::TableNextColumn();
                ImGui::Text("%.2f", rule.panLR);
            }
            ImGui::EndTable();
        }
    }

    // Routing panel window
    if (showRoutingPanel_) {
        ImGui::Begin("Audio Routing Configuration", &showRoutingPanel_);

        ImGui::Text("Add New Routing Rule");
        ImGui::Separator();

        ImGui::Combo("Output Channel", &selectedOutputCh_,
                     "CH1 (Headphone L)\0CH2 (Headphone R)\0CH3 (Haptic P1)\0CH4 (Haptic P2)\0");
        ImGui::Combo("Source Participant", &selectedSourceParticipant_,
                     "Participant1\0Participant2\0Synthetic\0");
        ImGui::Combo("Mix Mode", &selectedMixMode_,
                     "Self\0Partner\0Haptic\0Silent\0");
        ImGui::SliderFloat("Gain (dB)", &manualGainDb_, -40.0f, 6.0f);
        ImGui::SliderFloat("Pan", &manualPan_, -1.0f, 1.0f);

        if (ImGui::Button("Add Rule")) {
            RoutingRule newRule;
            newRule.sourceParticipant = static_cast<ParticipantId>(selectedSourceParticipant_);
            newRule.outputChannel = static_cast<OutputChannel>(selectedOutputCh_);
            newRule.mode = static_cast<MixMode>(selectedMixMode_);
            newRule.gainDb = manualGainDb_;
            newRule.panLR = manualPan_;
            audioRouter_.addRule(newRule);
        }

        ImGui::SameLine();
        if (ImGui::Button("Clear All Rules")) {
            audioRouter_.clearRules();
        }

        ImGui::SameLine();
        if (ImGui::Button("Reset to Scene Preset")) {
            audioRouter_.applyScenePreset(sceneController_.currentScene());
        }

        ImGui::End();
    }
}

// Helper function to convert MixMode to string
const char* ofApp::mixModeToString(MixMode mode) {
    switch (mode) {
        case MixMode::Self: return "Self";
        case MixMode::Partner: return "Partner";
        case MixMode::Haptic: return "Haptic";
        case MixMode::Silent: return "Silent";
        default: return "Unknown";
    }
}
```

#### 指示C: AudioRouter.h に公開メソッド追加

```cpp
// AudioRouter.h public methods:
public:
    const std::vector<RoutingRule>& rules() const { return rules_; }
    void addRule(const RoutingRule& rule) { rules_.push_back(rule); }
    void clearRules() { rules_.clear(); }
```

#### テスト方法:

```bash
# 1. ビルド
make Release

# 2. GUI表示確認
# "Show Routing Panel" ボタンをクリック
# ルーティングパネルが表示されることを確認

# 3. ルール追加テスト
# Output Channel: CH1
# Source: Participant1
# Mode: Self
# Gain: 0.0dB
# Pan: -1.0 (Full Left)
# "Add Rule" クリック
# テーブルに新しいルールが表示されることを確認

# 4. 音響確認
# 追加したルールに従って音が出力されることを確認

# 5. ルールクリアテスト
# "Clear All Rules" → 全ルールが削除される
# "Reset to Scene Preset" → シーンプリセットに戻る
```

#### 成功基準:
- GUIパネルが正しく表示される
- ルールの追加/削除が動作する
- リアルタイムでルーティングが切り替わる
- プリセットのリセットが動作する

#### リスク:
- **失敗時の影響**: 動的設定不可、コード変更が必要
- **依存関係**: Unit 1-6が完了していること

---

### **Unit 9: 1人モード (Synthetic心拍生成) 実装**

**ファイル**: `src/audio/AudioPipeline.cpp`, `src/audio/AudioPipeline.h`
**優先度**: 🟢 通常 (ソロデモ対応)

#### 背景:
- 2人参加が前提だが、1人でのデモも必要
- 片方の参加者がいない場合、Synthetic心拍を生成して疑似的な相互作用を実現

#### 指示A: Synthetic心拍生成メソッド追加

**src/audio/AudioPipeline.h に追加**:

```cpp
public:
    void enableSyntheticParticipant(ParticipantId id, float bpm, bool enable);

private:
    struct SyntheticState {
        bool enabled = false;
        float bpm = 70.0f;
        double phase = 0.0;
    };
    std::array<SyntheticState, 2> syntheticStates_;
```

**src/audio/AudioPipeline.cpp に追加**:

```cpp
void AudioPipeline::enableSyntheticParticipant(ParticipantId id, float bpm, bool enable) {
    const std::size_t idx = static_cast<std::size_t>(id);
    if (idx >= 2) return;

    syntheticStates_[idx].enabled = enable;
    syntheticStates_[idx].bpm = bpm;
    syntheticStates_[idx].phase = 0.0;
}

// processInput() の最後に追加:
void AudioPipeline::processInput(const float* input, std::size_t numFrames) {
    // ... existing code ...

    // Generate synthetic heartbeat if enabled
    for (std::size_t channel = 0; channel < 2; ++channel) {
        if (!syntheticStates_[channel].enabled) continue;

        auto& state = syntheticStates_[channel];
        const float beatsPerSecond = state.bpm / 60.0f;
        const float phaseIncrement = beatsPerSecond / sampleRate_;

        std::vector<float> syntheticBuffer(numFrames);
        for (std::size_t i = 0; i < numFrames; ++i) {
            // Generate pulse envelope (Gaussian-like)
            const float t = static_cast<float>(state.phase);
            const float pulse = std::exp(-50.0f * (t - 0.5f) * (t - 0.5f));
            syntheticBuffer[i] = pulse * 0.3f;  // Amplitude

            state.phase += phaseIncrement;
            if (state.phase >= 1.0) {
                state.phase -= 1.0;
            }
        }

        // Process synthetic signal through BeatTimeline
        const double startSampleIndex = sampleCounter_ - numFrames;
        beatTimelines_[channel].processBuffer(syntheticBuffer.data(), numFrames, startSampleIndex);
    }
}
```

#### 指示B: GUI でSyntheticモード切替

**ofApp.cpp drawGui() に追加**:

```cpp
if (ImGui::CollapsingHeader("Synthetic Participants")) {
    static bool enableSyntheticP1 = false;
    static bool enableSyntheticP2 = false;
    static float syntheticBpmP1 = 70.0f;
    static float syntheticBpmP2 = 65.0f;

    ImGui::Checkbox("Enable Synthetic Participant 1", &enableSyntheticP1);
    if (enableSyntheticP1) {
        ImGui::SliderFloat("P1 BPM", &syntheticBpmP1, 40.0f, 180.0f);
    }

    ImGui::Checkbox("Enable Synthetic Participant 2", &enableSyntheticP2);
    if (enableSyntheticP2) {
        ImGui::SliderFloat("P2 BPM", &syntheticBpmP2, 40.0f, 180.0f);
    }

    if (ImGui::Button("Apply Synthetic Settings")) {
        std::lock_guard<std::mutex> lock(audioMutex_);
        audioPipeline_.enableSyntheticParticipant(ParticipantId::Participant1, syntheticBpmP1, enableSyntheticP1);
        audioPipeline_.enableSyntheticParticipant(ParticipantId::Participant2, syntheticBpmP2, enableSyntheticP2);
    }
}
```

#### テスト方法:

```bash
# 1. ビルド
make Release

# 2. 1人モードテスト
# マイク1本だけを接続 (Participant1)
# GUI で "Enable Synthetic Participant 2" をチェック
# P2 BPM を70に設定
# "Apply Synthetic Settings" をクリック

# 3. Exchange シーンテスト
# Exchangeシーンに遷移
# P1 のヘッドフォンでSynthetic P2の心拍音が聞こえることを確認

# 4. BPM変化テスト
# Synthetic BPMを40→180まで変化
# 心拍音のテンポが変化することを確認

# 5. オフテスト
# "Enable Synthetic Participant 2" をオフ
# Synthetic心拍が停止することを確認
```

#### 成功基準:
- 1人でもデモが動作する
- Synthetic心拍がリアルタイムで生成される
- BPMが設定通りに反映される
- 実際の参加者とSynthetic参加者が区別なく動作する

#### リスク:
- **失敗時の影響**: 1人モードが動作しない、デモの柔軟性低下
- **依存関係**: Unit 1-8が完了していること

---

## 📊 実装進捗チェックリスト

各Unit完了時にチェックしてください:

- [ ] **Unit 1**: 4チャンネル出力有効化
- [ ] **Unit 2**: AudioRouterインスタンス作成
- [ ] **Unit 3**: audioOut()でroute()呼び出し
- [ ] **Unit 4**: ハプティクス信号生成実装
- [ ] **Unit 5**: シーン別ルーティングプリセット
- [ ] **Unit 6**: シーン遷移時プリセット自動適用
- [ ] **Unit 7**: パフォーマンス最適化
- [ ] **Unit 8**: ルーティングGUI実装
- [ ] **Unit 9**: 1人モード (Synthetic心拍)

---

## 🔍 統合テストシナリオ

全Unit完了後、以下のシナリオでエンドツーエンドテストを実施:

### シナリオ1: 2人モード完全フロー

```
1. 2名の参加者がマイクを装着
2. キャリブレーション実行 (既存機能)
3. Start シーン開始
4. FirstPhase: 各自が自分の心拍を聞く
   - P1: CH1 (L) で自分の心拍
   - P2: CH2 (R) で自分の心拍
   - CH3/4 でハプティクス振動
5. Exchange シーン自動遷移
   - ベル音再生
   - 10秒フェードアウト → ルーティング切替 → 10秒フェードイン
   - P1: CH1 (L) でP2の心拍を聞く
   - P2: CH2 (R) でP1の心拍を聞く
6. Mixed シーン自動遷移
   - 両方の心拍がステレオミックスで聞こえる
7. End シーン
   - フェードアウト
   - 15秒後にIdle自動復帰
```

### シナリオ2: 1人モード (Synthetic)

```
1. 1名の参加者がマイクを装着
2. GUI で Synthetic Participant 2 を有効化 (BPM=70)
3. シナリオ1と同様のフローを実行
4. Exchange/Mixed でSynthetic心拍が聞こえることを確認
```

### シナリオ3: 動的ルーティング変更

```
1. FirstPhase シーンで開始
2. GUI の Routing Panel を開く
3. 手動でルールを追加:
   - CH1 → Participant2, Self, 0dB, Pan=-1.0
   - CH2 → Participant1, Self, 0dB, Pan=1.0
4. 音響が即座に切り替わることを確認
5. "Reset to Scene Preset" で元に戻る
```

---

## ⚠️ 既知の制約と今後の改善

### 現時点で実装しないもの (後のフェーズで対応):

1. **バイノーラル音響 (HRTF)**
   - 要件: `bin/data/hrir/` のHRTFデータ読み込みと畳み込み
   - 理由: 基本デモには不要、計算コスト高い
   - 優先度: 低

2. **高度なハプティクス信号処理**
   - 要件: BPMに応じた動的周波数変調、バンドパスフィルタ
   - 理由: 基本的な50Hz正弦波で十分
   - 優先度: 中

3. **ネットワーク同期**
   - 要件: 複数デバイス間でのオーディオ同期
   - 理由: 単一デバイスでの動作が前提
   - 優先度: 低

4. **長期安定性の検証**
   - 要件: 24時間連続動作テスト
   - 理由: 短時間デモには不要
   - 優先度: 中

### パフォーマンス制約:

- **CPU使用率**: 目標 <30% (48kHz, 512buffer)
- **メモリ使用量**: 目標 <500MB
- **レイテンシ**: 512samples @ 48kHz = 10.7ms (許容範囲内)

---

## 📞 トラブルシューティング

### 問題1: CH3/4 に音が出ない

**確認事項**:
1. `numOutputChannels = 4` になっているか (Unit 1)
2. オーディオインターフェースが4ch対応か
3. `generateHapticSample()` が0.0f以外を返しているか (Unit 4)
4. ルーティングルールに CH3/4 が含まれているか (Unit 5)

**デバッグ方法**:
```cpp
// audioOut() に追加:
static int frameCount = 0;
if (frameCount++ % 4800 == 0) {  // 0.1秒ごと
    std::cout << "CH3: " << outputBuffer_[2] << ", CH4: " << outputBuffer_[3] << std::endl;
}
```

### 問題2: Exchange シーンで音が切り替わらない

**確認事項**:
1. `applyScenePreset()` が実装されているか (Unit 5)
2. `handleTransitionEvent()` でプリセット適用されているか (Unit 6)
3. ルーティングルールが正しく設定されているか

**デバッグ方法**:
```cpp
// applyScenePreset() の最初に追加:
std::cout << "Applying preset for scene: " << static_cast<int>(scene) << std::endl;
std::cout << "Rules count: " << rules_.size() << std::endl;
```

### 問題3: Synthetic心拍が生成されない

**確認事項**:
1. `enableSyntheticParticipant()` が呼ばれているか (Unit 9)
2. GUIの "Apply Synthetic Settings" をクリックしたか
3. `processInput()` でSynthetic信号生成部分が実行されているか

**デバッグ方法**:
```cpp
// processInput() Synthetic生成部分に追加:
if (syntheticStates_[channel].enabled) {
    std::cout << "Generating synthetic for channel " << channel
              << " at BPM " << syntheticStates_[channel].bpm << std::endl;
}
```

---

## ✅ 完了基準

全ての実装が完了したと判断できる基準:

1. **機能完全性**:
   - [ ] 2人モードで全シーンが動作
   - [ ] 1人モード (Synthetic) が動作
   - [ ] ハプティクス出力が動作
   - [ ] GUI で動的ルーティング変更が可能

2. **品質基準**:
   - [ ] 30分以上の連続動作でクラッシュなし
   - [ ] オーディオドロップアウトなし
   - [ ] CPU使用率 <30%
   - [ ] 全Unitのテストが成功

3. **ユーザー体験**:
   - [ ] シーン遷移がスムーズ
   - [ ] ベル音とフェードが自然
   - [ ] ハプティクス振動が心地よい
   - [ ] 音響の交換が明確に感じられる

---

## 📚 参照ドキュメント

- **MASTERDOCUMENT.md**: プロジェクト全体仕様
- **DUAL_CHANNEL_ROUTING_REQUIREMENTS.md**: ルーティング設計詳細
- **config/scene_timing.json**: シーン遷移タイミング設定
- **src/audio/AudioRouter.h**: ルーティングAPI仕様
- **src/audio/BeatTimeline.h**: 心拍検出API仕様

---

**最終更新**: 2025-10-29
**ドキュメントバージョン**: 1.0
**対象リリース**: MVP (Minimum Viable Product)
