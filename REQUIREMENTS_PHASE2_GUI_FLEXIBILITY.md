# Phase 2 要件定義書: GUI機能と柔軟性向上

**作成日**: 2025-10-29
**対象**: 開発チーム
**優先度**: 🟡 重大 (Phase 1完了後に実装)
**目的**: ルーティングGUI、1人モード、動的設定変更を実現

---

## 📋 Phase 2 の目標

### 解決する問題:
- ✅ **問題3**: ルーティングGUIが実装されていない

### 追加機能:
- ✅ **1人モード**: Synthetic心拍生成で1人でもデモ可能
- ✅ **動的ルーティング**: 実行中にルーティングルールを変更
- ✅ **テストモード**: デバッグ用の機能追加

### 成功基準:
1. GUIからオーディオルーティングを動的に設定できる
2. 1人参加でもSynthetic心拍で相互作用を体験できる
3. デモ時の柔軟な設定変更が可能
4. Phase 1の機能が引き続き動作する

### 前提条件:
- ⚠️ **Phase 1が完了していること**
- AudioRouter統合済み
- 2ch独立処理済み
- 4ch出力動作済み

---

## 🎯 実装単位一覧

Phase 2は **8個の実装単位** で構成されます。順序通りに実施してください。

| ID | 実装単位 | ファイル | 優先度 | 依存関係 |
|----|---------|---------|-------|---------|
| **2.1** | AudioRouter公開API拡張 | AudioRouter.h/cpp | 🟡 | Phase 1完了 |
| **2.2** | ルーティングGUI: 基本パネル | ofApp.cpp | 🟡 | 2.1 |
| **2.3** | ルーティングGUI: ルール追加 | ofApp.cpp | 🟡 | 2.2 |
| **2.4** | ルーティングGUI: ルール削除 | ofApp.cpp | 🟡 | 2.3 |
| **2.5** | Synthetic心拍生成: インフラ | AudioPipeline.h/cpp | 🟡 | なし |
| **2.6** | Synthetic心拍生成: GUI制御 | ofApp.cpp | 🟡 | 2.5 |
| **2.7** | ルーティングプリセット保存/読込 | config/routing_presets.json | 🟢 | 2.1 |
| **2.8** | デバッグ機能追加 | ofApp.cpp | 🟢 | 2.1-2.6 |

---

## 📝 詳細実装指示

### **Unit 2.1: AudioRouter公開API拡張**

**ファイル**: `src/audio/AudioRouter.h`, `src/audio/AudioRouter.cpp`
**所要時間**: 30分

#### 背景:
現在のAudioRouterはsetRoutingRule()で個別設定のみ。GUI用に複数ルール管理APIが必要。

#### 実装指示A: AudioRouter.hに公開メソッド追加

```cpp
// src/audio/AudioRouter.h public section に追加:
public:
    // Existing methods...
    void setRoutingRule(OutputChannel channel, const RoutingRule& rule);
    const RoutingRule& routingRule(OutputChannel channel) const;
    void applyScenePreset(SceneState scene);
    void route(const std::array<float, 2>& inputEnvelopes,
               std::array<float, 4>& outputBuffer);

    // NEW: GUI support methods
    /// Get all routing rules as a vector (for display in GUI)
    std::vector<RoutingRule> rules() const;

    /// Get rule for specific output channel
    const RoutingRule& rule(OutputChannel channel) const;

    /// Add or update rule for a specific channel
    void setRule(OutputChannel channel, const RoutingRule& rule);

    /// Clear all routing rules (set to silent)
    void clearAllRules();

    /// Get number of configured (non-silent) channels
    std::size_t activeRuleCount() const;
```

#### 実装指示B: AudioRouter.cppに実装

```cpp
// src/audio/AudioRouter.cpp に追加:

std::vector<RoutingRule> AudioRouter::rules() const {
    std::vector<RoutingRule> result;
    result.reserve(rules_.size());
    for (const auto& rule : rules_) {
        result.push_back(rule);
    }
    return result;
}

const RoutingRule& AudioRouter::rule(OutputChannel channel) const {
    return rules_[static_cast<std::size_t>(channel)];
}

void AudioRouter::setRule(OutputChannel channel, const RoutingRule& rule) {
    rules_[static_cast<std::size_t>(channel)] = rule;
    ofLogNotice("AudioRouter") << "Rule updated for channel " << static_cast<int>(channel)
                                << ": source=" << static_cast<int>(rule.source)
                                << ", mode=" << static_cast<int>(rule.mixMode)
                                << ", gain=" << rule.gainDb << "dB";
}

void AudioRouter::clearAllRules() {
    for (auto& rule : rules_) {
        rule.source = ParticipantId::None;
        rule.mixMode = MixMode::Silent;
        rule.gainDb = -96.0f;
        rule.panLR = 0.0f;
    }
    ofLogNotice("AudioRouter") << "All routing rules cleared";
}

std::size_t AudioRouter::activeRuleCount() const {
    std::size_t count = 0;
    for (const auto& rule : rules_) {
        if (rule.mixMode != MixMode::Silent && rule.source != ParticipantId::None) {
            ++count;
        }
    }
    return count;
}
```

#### テスト方法:

```bash
# 1. ビルド
make Release

# 2. API呼び出しテスト (テストコード追加)
# ofApp::setup() に以下を追加してテスト:
auto currentRules = audioRouter_.rules();
ofLogNotice() << "Initial rule count: " << currentRules.size();

audioRouter_.clearAllRules();
ofLogNotice() << "Active rules after clear: " << audioRouter_.activeRuleCount();

RoutingRule testRule;
testRule.source = ParticipantId::Participant1;
testRule.mixMode = MixMode::Self;
testRule.gainDb = 0.0f;
testRule.panLR = 0.0f;
audioRouter_.setRule(OutputChannel::CH1_HeadphoneLeft, testRule);
ofLogNotice() << "Active rules after setRule: " << audioRouter_.activeRuleCount();
```

#### 成功基準:
- ✅ ビルドエラーなし
- ✅ rules() が全ルールを返す
- ✅ setRule() でルールが更新される
- ✅ clearAllRules() で全ルールがクリアされる
- ✅ activeRuleCount() が正しい数を返す

#### リスク:
- **低**: 既存のsetRoutingRule()との互換性
- **対策**: setRule()はsetRoutingRule()のエイリアスとして機能

---

### **Unit 2.2: ルーティングGUI: 基本パネル**

**ファイル**: `src/ofApp.h`, `src/ofApp.cpp`
**所要時間**: 45分

#### 背景:
ImGuiでルーティングルールを表示・編集するパネルを作成。

#### 実装指示A: ofApp.hに状態変数追加

```cpp
// src/ofApp.h private members に追加:
private:
    // ... existing members ...

    // Routing GUI state
    bool showRoutingPanel_ = false;
    int selectedOutputCh_ = 0;        // 0=CH1, 1=CH2, 2=CH3, 3=CH4
    int selectedSourceParticipant_ = 0;  // 0=P1, 1=P2, 2=Synthetic, 3=None
    int selectedMixMode_ = 0;         // 0=Self, 1=Partner, 2=Haptic, 3=Silent
    float manualGainDb_ = 0.0f;
    float manualPan_ = 0.0f;
```

#### 実装指示B: ofApp.cppにGUIパネル追加

**drawGui()内に追加** (ImGuiセクション):

```cpp
// src/ofApp.cpp drawGui() に追加 (既存のImGui::CollapsingHeader の後):

if (ImGui::CollapsingHeader("Audio Routing", ImGuiTreeNodeFlags_DefaultOpen)) {
    if (ImGui::Button(showRoutingPanel_ ? "Hide Routing Panel" : "Show Routing Panel")) {
        showRoutingPanel_ = !showRoutingPanel_;
    }

    ImGui::Separator();
    ImGui::Text("Active Routing Rules: %zu / 4", audioRouter_.activeRuleCount());

    // Display current routing rules in a table
    if (ImGui::BeginTable("RoutingRulesTable", 5,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("Output");
        ImGui::TableSetupColumn("Source");
        ImGui::TableSetupColumn("Mode");
        ImGui::TableSetupColumn("Gain (dB)");
        ImGui::TableSetupColumn("Pan");
        ImGui::TableHeadersRow();

        const char* channelNames[] = {"CH1 (HP L)", "CH2 (HP R)", "CH3 (Haptic P1)", "CH4 (Haptic P2)"};
        const char* participantNames[] = {"P1", "P2", "Synth", "None"};
        const char* modeNames[] = {"Self", "Partner", "Haptic", "Silent"};

        for (std::size_t i = 0; i < 4; ++i) {
            const auto channel = static_cast<knot::audio::OutputChannel>(i);
            const auto& rule = audioRouter_.rule(channel);

            ImGui::TableNextRow();

            // Output channel
            ImGui::TableNextColumn();
            ImGui::Text("%s", channelNames[i]);

            // Source participant
            ImGui::TableNextColumn();
            const int sourceIdx = static_cast<int>(rule.source);
            if (sourceIdx >= 0 && sourceIdx < 4) {
                ImGui::Text("%s", participantNames[sourceIdx]);
            } else {
                ImGui::Text("Invalid");
            }

            // Mix mode
            ImGui::TableNextColumn();
            const int modeIdx = static_cast<int>(rule.mixMode);
            if (modeIdx >= 0 && modeIdx < 4) {
                ImGui::Text("%s", modeNames[modeIdx]);
            } else {
                ImGui::Text("Unknown");
            }

            // Gain
            ImGui::TableNextColumn();
            ImGui::Text("%.1f", rule.gainDb);

            // Pan
            ImGui::TableNextColumn();
            ImGui::Text("%.2f", rule.panLR);
        }

        ImGui::EndTable();
    }
}
```

#### テスト方法:

```bash
# 1. ビルド
make Release

# 2. GUI表示確認
# アプリケーション起動
# "Audio Routing" セクションが表示される
# "Show Routing Panel" ボタンが表示される

# 3. ルールテーブル確認
# 4行のテーブルが表示される (CH1-CH4)
# 各行に現在のルーティングルールが表示される
# FirstPhaseシーンのルールが正しく表示される

# 4. シーン遷移確認
# FirstPhase → Exchange に遷移
# テーブルの内容が更新される
# Exchangeのルールが反映される
```

#### 成功基準:
- ✅ ビルドエラーなし
- ✅ "Audio Routing" セクションが表示される
- ✅ ルールテーブルが正しく表示される
- ✅ シーン遷移時にテーブルが更新される

#### リスク:
- **低**: ImGuiのテーブルレイアウト
- **対策**: ImGui::BeginTable()のフラグを調整

---

### **Unit 2.3: ルーティングGUI: ルール追加**

**ファイル**: `src/ofApp.cpp`
**所要時間**: 45分

#### 背景:
GUIから新しいルーティングルールを追加・編集する機能。

#### 実装指示:

**Unit 2.2のdrawGui()に続けて追加**:

```cpp
// Routing panel window (separate window)
if (showRoutingPanel_) {
    ImGui::SetNextWindowSize(ImVec2(400, 500), ImGuiCond_FirstUseEver);
    ImGui::Begin("Audio Routing Configuration", &showRoutingPanel_);

    ImGui::Text("Configure Audio Routing Rules");
    ImGui::Separator();

    // Output channel selection
    ImGui::Text("Output Channel:");
    const char* channelLabels[] = {
        "CH1 (Headphone Left)",
        "CH2 (Headphone Right)",
        "CH3 (Haptic P1)",
        "CH4 (Haptic P2)"
    };
    ImGui::Combo("##OutputChannel", &selectedOutputCh_, channelLabels, 4);

    // Source participant selection
    ImGui::Text("Source Participant:");
    const char* participantLabels[] = {
        "Participant 1",
        "Participant 2",
        "Synthetic",
        "None (Silent)"
    };
    ImGui::Combo("##SourceParticipant", &selectedSourceParticipant_, participantLabels, 4);

    // Mix mode selection
    ImGui::Text("Mix Mode:");
    const char* modeLabels[] = {
        "Self (Own Heartbeat)",
        "Partner (Partner's Heartbeat)",
        "Haptic (Vibration Signal)",
        "Silent (No Output)"
    };
    ImGui::Combo("##MixMode", &selectedMixMode_, modeLabels, 4);

    // Gain slider
    ImGui::Text("Gain:");
    ImGui::SliderFloat("##Gain", &manualGainDb_, -40.0f, 6.0f, "%.1f dB");

    // Pan slider (only for CH1/CH2 headphones)
    if (selectedOutputCh_ < 2) {
        ImGui::Text("Pan (L/R):");
        ImGui::SliderFloat("##Pan", &manualPan_, -1.0f, 1.0f, "%.2f");
    } else {
        ImGui::TextDisabled("Pan (not applicable for haptics)");
        manualPan_ = 0.0f;
    }

    ImGui::Separator();

    // Preview current rule for selected channel
    ImGui::Text("Current Rule for %s:", channelLabels[selectedOutputCh_]);
    const auto currentChannel = static_cast<knot::audio::OutputChannel>(selectedOutputCh_);
    const auto& currentRule = audioRouter_.rule(currentChannel);
    ImGui::Text("  Source: %s", participantLabels[static_cast<int>(currentRule.source)]);
    ImGui::Text("  Mode: %s", modeLabels[static_cast<int>(currentRule.mixMode)]);
    ImGui::Text("  Gain: %.1f dB", currentRule.gainDb);
    ImGui::Text("  Pan: %.2f", currentRule.panLR);

    ImGui::Separator();

    // Apply button
    if (ImGui::Button("Apply Rule", ImVec2(-1, 40))) {
        knot::audio::RoutingRule newRule;
        newRule.source = static_cast<knot::audio::ParticipantId>(selectedSourceParticipant_);
        newRule.mixMode = static_cast<knot::audio::MixMode>(selectedMixMode_);
        newRule.gainDb = manualGainDb_;
        newRule.panLR = manualPan_;

        const auto outputChannel = static_cast<knot::audio::OutputChannel>(selectedOutputCh_);
        {
            std::lock_guard<std::mutex> lock(audioMutex_);
            audioRouter_.setRule(outputChannel, newRule);
        }

        ofLogNotice("ofApp") << "Manual routing rule applied: "
                              << "CH" << selectedOutputCh_
                              << " <- " << participantLabels[selectedSourceParticipant_]
                              << " (" << modeLabels[selectedMixMode_] << ")";
    }

    ImGui::End();
}
```

#### テスト方法:

```bash
# 1. ビルド
make Release

# 2. ルーティングパネル表示
# "Show Routing Panel" ボタンをクリック
# 独立したウィンドウが表示される

# 3. ルール設定テスト
# Output Channel: CH1 (Headphone Left)
# Source: Participant 2
# Mode: Partner
# Gain: -6.0 dB
# Pan: -1.0 (Full Left)
# "Apply Rule" クリック

# 4. 音響確認
# CH1から Participant2の心拍音が聞こえる
# 音量が-6dB減衰されている
# 完全に左チャンネル

# 5. GUI更新確認
# メインのルールテーブルに変更が反映される
# "Current Rule for CH1" に新しいルールが表示される
```

#### 成功基準:
- ✅ ビルドエラーなし
- ✅ ルーティングパネルが表示される
- ✅ ルール設定UIが動作する
- ✅ "Apply Rule" で即座にルーティングが変更される
- ✅ 音響が設定通りに変化する

#### リスク:
- **中**: 実行中のルーティング変更でノイズ発生
- **対策**: audioMutex_で適切にロック、クリックノイズは許容

---

### **Unit 2.4: ルーティングGUI: ルール削除とプリセット復帰**

**ファイル**: `src/ofApp.cpp`
**所要時間**: 20分

#### 背景:
手動設定をクリアし、シーンプリセットに戻す機能。

#### 実装指示:

**Unit 2.3のルーティングパネル内に追加** (Apply Buttonの後):

```cpp
    // ... Apply button ...

    ImGui::Separator();
    ImGui::Text("Bulk Operations:");

    // Clear all rules button
    if (ImGui::Button("Clear All Rules", ImVec2(-1, 0))) {
        if (ImGui::GetIO().KeyShift) {  // Shift+Click for safety
            std::lock_guard<std::mutex> lock(audioMutex_);
            audioRouter_.clearAllRules();
            ofLogNotice("ofApp") << "All routing rules cleared";
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f),
                             "Hold SHIFT and click to confirm");
        }
    }

    // Reset to scene preset button
    if (ImGui::Button("Reset to Scene Preset", ImVec2(-1, 0))) {
        const auto currentScene = sceneController_.currentScene();
        {
            std::lock_guard<std::mutex> lock(audioMutex_);
            audioRouter_.applyScenePreset(currentScene);
        }
        ofLogNotice("ofApp") << "Routing reset to preset for scene: "
                              << sceneStateToString(currentScene);
    }

    ImGui::Separator();
    ImGui::TextWrapped("Tip: Manual routing changes override scene presets. "
                      "Use 'Reset to Scene Preset' to restore automatic routing.");

    ImGui::End();
}
```

#### テスト方法:

```bash
# 1. ビルド
make Release

# 2. 手動ルール設定
# Unit 2.3で複数のルールを手動設定

# 3. Reset to Scene Preset テスト
# "Reset to Scene Preset" ボタンをクリック
# ルールがシーンプリセット (FirstPhase等) に戻る
# 音響が元に戻る

# 4. Clear All Rules テスト
# SHIFT を押しながら "Clear All Rules" をクリック
# 全チャンネルがSilentになる
# 音が完全に止まる

# 5. シーン遷移後の復帰テスト
# FirstPhase → Exchange に遷移
# 自動的にExchangeプリセットが適用される
# 手動設定が上書きされる
```

#### 成功基準:
- ✅ ビルドエラーなし
- ✅ "Reset to Scene Preset" で元に戻る
- ✅ "Clear All Rules" で全ルールがクリアされる
- ✅ シーン遷移で自動復帰する

#### リスク:
- **低**: 誤操作でルールが消える
- **対策**: SHIFT+Clickでの確認ステップ

---

### **Unit 2.5: Synthetic心拍生成: インフラ**

**ファイル**: `src/audio/AudioPipeline.h`, `src/audio/AudioPipeline.cpp`
**所要時間**: 60分

#### 背景:
1人参加時に、もう1人の参加者をSynthetic心拍で代替する。

#### 実装指示A: AudioPipeline.hに状態追加

```cpp
// src/audio/AudioPipeline.h private members に追加:
private:
    // ... existing members ...

    // Synthetic heartbeat generation for solo mode
    struct SyntheticState {
        bool enabled = false;
        float bpm = 70.0f;           // Target BPM
        double phase = 0.0;          // Phase for pulse generation [0, 1)
        double lastBeatTime = 0.0;   // Timestamp of last beat (seconds)
    };
    std::array<SyntheticState, 2> syntheticStates_;
```

#### 実装指示B: 公開API追加

```cpp
// src/audio/AudioPipeline.h public methods に追加:
public:
    /// Enable or disable synthetic heartbeat for a participant
    /// @param id Participant to generate synthetic beats for
    /// @param bpm Target beats per minute (40-180)
    /// @param enable True to enable, false to disable
    void enableSyntheticParticipant(ParticipantId id, float bpm, bool enable);

    /// Check if synthetic mode is enabled for a participant
    bool isSyntheticEnabled(ParticipantId id) const;

    /// Get current synthetic BPM
    float syntheticBpm(ParticipantId id) const;
```

#### 実装指示C: AudioPipeline.cppに実装

```cpp
// src/audio/AudioPipeline.cpp に追加:

void AudioPipeline::enableSyntheticParticipant(ParticipantId id, float bpm, bool enable) {
    std::lock_guard<std::mutex> lock(mutex_);

    const auto idx = participantIndex(id);
    if (!idx) {
        ofLogWarning("AudioPipeline") << "Invalid participant ID for synthetic mode";
        return;
    }

    auto& state = syntheticStates_[*idx];
    state.enabled = enable;
    state.bpm = std::clamp(bpm, 40.0f, 180.0f);  // Safety clamp
    state.phase = 0.0;
    state.lastBeatTime = 0.0;

    ofLogNotice("AudioPipeline") << "Synthetic participant " << static_cast<int>(id)
                                  << " " << (enable ? "enabled" : "disabled")
                                  << " at " << state.bpm << " BPM";
}

bool AudioPipeline::isSyntheticEnabled(ParticipantId id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto idx = participantIndex(id);
    return idx ? syntheticStates_[*idx].enabled : false;
}

float AudioPipeline::syntheticBpm(ParticipantId id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto idx = participantIndex(id);
    return idx ? syntheticStates_[*idx].bpm : 0.0f;
}
```

#### 実装指示D: processInput()でSynthetic信号生成

**processInput()の最後に追加**:

```cpp
// src/audio/AudioPipeline.cpp processInput() の最後に追加:

void AudioPipeline::processInput(const float* input, std::size_t numFrames) {
    // ... existing processing ...

    // Generate synthetic heartbeat signals if enabled
    for (std::size_t channel = 0; channel < 2; ++channel) {
        if (!syntheticStates_[channel].enabled) {
            continue;
        }

        auto& state = syntheticStates_[channel];
        const float beatsPerSecond = state.bpm / 60.0f;
        const float periodSeconds = 1.0f / beatsPerSecond;

        // Generate pulse envelope (Gaussian-like pulse)
        std::vector<float> syntheticBuffer(numFrames);
        for (std::size_t i = 0; i < numFrames; ++i) {
            // Phase in [0, 1) within each beat period
            const float t = static_cast<float>(state.phase);

            // Gaussian pulse centered at t=0.2 (systolic peak)
            const float pulseCenter = 0.2f;
            const float pulseWidth = 0.1f;
            const float dt = t - pulseCenter;
            const float pulse = std::exp(-0.5f * (dt * dt) / (pulseWidth * pulseWidth));

            // Amplitude: 0.3 (moderate synthetic signal)
            syntheticBuffer[i] = pulse * 0.3f;

            // Advance phase
            state.phase += static_cast<double>(beatsPerSecond) / sampleRate_;
            if (state.phase >= 1.0) {
                state.phase -= 1.0;

                // Emit beat event when phase wraps
                const double beatTime = totalSamplesProcessed_ / sampleRate_;
                BeatEvent event;
                event.participantId = static_cast<ParticipantId>(channel);
                event.timestampSec = beatTime;
                event.bpm = state.bpm;
                event.envelope = 0.3f;
                event.isSynthetic = true;  // Mark as synthetic
                pendingEventsByChannel_[channel].push_back(event);

                state.lastBeatTime = beatTime;
            }
        }

        // Process synthetic signal through BeatTimeline
        const double startSampleIndex = totalSamplesProcessed_ - numFrames;
        beatTimelines_[channel].processBuffer(syntheticBuffer.data(), numFrames, startSampleIndex);

        // Update channel metrics with synthetic data
        channelMetrics_[channel].envelope = 0.3f * static_cast<float>(
            std::exp(-0.5f * (state.phase - 0.2f) * (state.phase - 0.2f) / 0.01f));
        channelMetrics_[channel].bpm = state.bpm;
        channelMetrics_[channel].participantId = static_cast<ParticipantId>(channel);
    }
}
```

#### 実装指示E: BeatEvent構造体にisSyntheticフラグ追加

```cpp
// src/audio/BeatEvent.h に追加:
struct BeatEvent {
    ParticipantId participantId = ParticipantId::None;
    double timestampSec = 0.0;
    float bpm = 0.0f;
    float envelope = 0.0f;
    bool isSynthetic = false;  // NEW: True if generated synthetically
};
```

#### テスト方法:

```bash
# 1. ビルド
make Release

# 2. Syntheticモード有効化テスト
# setup()に以下を追加:
audioPipeline_.enableSyntheticParticipant(ParticipantId::Participant2, 75.0f, true);

# 3. 起動確認
# ログに "Synthetic participant 1 enabled at 75 BPM" が表示される

# 4. メトリクス確認
# Participant2のメトリクスが75BPMで生成される
# displayEnvelopeP2_ が周期的に変化する

# 5. ビジュアル確認
# 右側 (P2) のビジュアルが75BPMで動く
# 左側 (P1) は実際のマイク入力
```

#### 成功基準:
- ✅ ビルドエラーなし
- ✅ Synthetic心拍が指定BPMで生成される
- ✅ BeatEventがisSynthetic=trueで生成される
- ✅ ビジュアルがSynthetic心拍に反応する

#### リスク:
- **中**: Syntheticと実信号の混同
- **対策**: isSyntheticフラグで明確に区別

---

### **Unit 2.6: Synthetic心拍生成: GUI制御**

**ファイル**: `src/ofApp.cpp`
**所要時間**: 30分

#### 背景:
GUIからSyntheticモードを有効化・BPM調整。

#### 実装指示A: ofApp.hに状態変数追加

```cpp
// src/ofApp.h private members に追加:
private:
    // Synthetic participants GUI state
    bool enableSyntheticP1_ = false;
    bool enableSyntheticP2_ = false;
    float syntheticBpmP1_ = 70.0f;
    float syntheticBpmP2_ = 65.0f;
```

#### 実装指示B: drawGui()にSyntheticパネル追加

```cpp
// src/ofApp.cpp drawGui() に追加:

if (ImGui::CollapsingHeader("Synthetic Participants (Solo Mode)")) {
    ImGui::TextWrapped("Enable synthetic heartbeat for demonstration without a second participant.");
    ImGui::Separator();

    // Participant 1
    ImGui::Checkbox("Enable Synthetic Participant 1", &enableSyntheticP1_);
    if (enableSyntheticP1_) {
        ImGui::Indent();
        ImGui::SliderFloat("P1 BPM", &syntheticBpmP1_, 40.0f, 180.0f, "%.1f BPM");
        ImGui::Text("Current: %.1f BPM", audioPipeline_.syntheticBpm(ParticipantId::Participant1));
        ImGui::Unindent();
    }

    ImGui::Spacing();

    // Participant 2
    ImGui::Checkbox("Enable Synthetic Participant 2", &enableSyntheticP2_);
    if (enableSyntheticP2_) {
        ImGui::Indent();
        ImGui::SliderFloat("P2 BPM", &syntheticBpmP2_, 40.0f, 180.0f, "%.1f BPM");
        ImGui::Text("Current: %.1f BPM", audioPipeline_.syntheticBpm(ParticipantId::Participant2));
        ImGui::Unindent();
    }

    ImGui::Spacing();
    ImGui::Separator();

    // Apply button
    if (ImGui::Button("Apply Synthetic Settings", ImVec2(-1, 0))) {
        std::lock_guard<std::mutex> lock(audioMutex_);
        audioPipeline_.enableSyntheticParticipant(ParticipantId::Participant1,
                                                    syntheticBpmP1_, enableSyntheticP1_);
        audioPipeline_.enableSyntheticParticipant(ParticipantId::Participant2,
                                                    syntheticBpmP2_, enableSyntheticP2_);
        ofLogNotice("ofApp") << "Synthetic settings applied: "
                              << "P1=" << (enableSyntheticP1_ ? "ON" : "OFF")
                              << " @ " << syntheticBpmP1_ << "BPM, "
                              << "P2=" << (enableSyntheticP2_ ? "ON" : "OFF")
                              << " @ " << syntheticBpmP2_ << "BPM";
    }

    ImGui::Spacing();
    ImGui::TextWrapped("Note: Synthetic mode overrides real microphone input for the selected participant.");
}
```

#### テスト方法:

```bash
# 1. ビルド
make Release

# 2. 1人モードテスト
# マイクを1本だけ接続 (Participant1)
# GUI で "Enable Synthetic Participant 2" をチェック
# P2 BPM を 80 に設定
# "Apply Synthetic Settings" をクリック

# 3. Exchange シーンテスト
# FirstPhase → Exchange に遷移
# P1のヘッドフォンでSynthetic P2の心拍音 (80BPM) が聞こえる
# ビジュアル: 右側がSynthetic P2で動く

# 4. BPM変化テスト
# Synthetic BPMを40→180まで変化
# リアルタイムでビジュアルと音響のテンポが変化
# "Apply Synthetic Settings" をクリックするたびに更新

# 5. オフテスト
# "Enable Synthetic Participant 2" をオフ
# "Apply Synthetic Settings" をクリック
# Synthetic心拍が停止
# 右側のビジュアルが止まる
```

#### 成功基準:
- ✅ ビルドエラーなし
- ✅ GUIからSyntheticモードを制御できる
- ✅ 1人でもデモが動作する
- ✅ BPMがリアルタイムで変更できる
- ✅ ビジュアルと音響がSynthetic心拍に反応する

#### リスク:
- **低**: 実信号とSynthetic信号の混同
- **対策**: GUI上で明確に表示

---

### **Unit 2.7: ルーティングプリセット保存/読込 (オプション)**

**ファイル**: `config/routing_presets.json`, `src/audio/AudioRouter.cpp`
**所要時間**: 60分
**優先度**: 🟢 通常 (時間に余裕があれば実装)

#### 背景:
手動設定したルーティングをJSONファイルに保存し、次回起動時に読み込む。

#### 実装指示A: JSONフォーマット定義

```json
// config/routing_presets.json
{
  "version": "1.0",
  "presets": [
    {
      "name": "Custom Preset 1",
      "description": "User-defined routing",
      "rules": [
        {
          "outputChannel": 0,
          "source": 0,
          "mixMode": 0,
          "gainDb": 0.0,
          "panLR": -1.0
        },
        {
          "outputChannel": 1,
          "source": 1,
          "mixMode": 0,
          "gainDb": 0.0,
          "panLR": 1.0
        }
      ]
    }
  ]
}
```

#### 実装指示B: AudioRouter.h/cppに保存/読込API追加

```cpp
// AudioRouter.h public に追加:
public:
    /// Save current routing rules to JSON file
    bool saveToFile(const std::string& filepath) const;

    /// Load routing rules from JSON file
    bool loadFromFile(const std::string& filepath);
```

#### 実装指示C: AudioRouter.cppに実装

```cpp
// AudioRouter.cpp に追加 (ofxJSON使用):
#include "ofxJSON.h"

bool AudioRouter::saveToFile(const std::string& filepath) const {
    ofxJSONElement json;
    json["version"] = "1.0";

    for (std::size_t i = 0; i < rules_.size(); ++i) {
        const auto& rule = rules_[i];
        ofxJSONElement ruleJson;
        ruleJson["outputChannel"] = static_cast<int>(i);
        ruleJson["source"] = static_cast<int>(rule.source);
        ruleJson["mixMode"] = static_cast<int>(rule.mixMode);
        ruleJson["gainDb"] = rule.gainDb;
        ruleJson["panLR"] = rule.panLR;
        json["rules"].append(ruleJson);
    }

    if (json.save(filepath, true)) {
        ofLogNotice("AudioRouter") << "Routing saved to: " << filepath;
        return true;
    } else {
        ofLogError("AudioRouter") << "Failed to save routing to: " << filepath;
        return false;
    }
}

bool AudioRouter::loadFromFile(const std::string& filepath) {
    ofxJSONElement json;
    if (!json.open(filepath)) {
        ofLogError("AudioRouter") << "Failed to load routing from: " << filepath;
        return false;
    }

    const auto& rulesJson = json["rules"];
    for (Json::ArrayIndex i = 0; i < rulesJson.size() && i < rules_.size(); ++i) {
        const auto& ruleJson = rulesJson[i];
        RoutingRule rule;
        rule.source = static_cast<ParticipantId>(ruleJson["source"].asInt());
        rule.mixMode = static_cast<MixMode>(ruleJson["mixMode"].asInt());
        rule.gainDb = ruleJson["gainDb"].asFloat();
        rule.panLR = ruleJson["panLR"].asFloat();

        const int outputChannel = ruleJson["outputChannel"].asInt();
        if (outputChannel >= 0 && outputChannel < 4) {
            rules_[outputChannel] = rule;
        }
    }

    ofLogNotice("AudioRouter") << "Routing loaded from: " << filepath;
    return true;
}
```

#### 実装指示D: GUIに保存/読込ボタン追加

```cpp
// drawGui()のルーティングパネルに追加:
ImGui::Separator();
ImGui::Text("Preset Management:");

static char presetFilename[128] = "config/my_routing_preset.json";
ImGui::InputText("##PresetFile", presetFilename, sizeof(presetFilename));

if (ImGui::Button("Save Preset", ImVec2(-1, 0))) {
    std::lock_guard<std::mutex> lock(audioMutex_);
    audioRouter_.saveToFile(presetFilename);
}

if (ImGui::Button("Load Preset", ImVec2(-1, 0))) {
    std::lock_guard<std::mutex> lock(audioMutex_);
    audioRouter_.loadFromFile(presetFilename);
}
```

#### テスト方法:

```bash
# 1. ビルド
make Release

# 2. ルーティング設定
# GUIで複数のルールを設定

# 3. 保存テスト
# Preset filename: "config/test_routing.json"
# "Save Preset" をクリック
# ファイルが作成される

# 4. ルールクリアテスト
# "Clear All Rules" で全削除

# 5. 読込テスト
# "Load Preset" をクリック
# 保存したルーティングが復元される
# 音響が元に戻る
```

#### 成功基準:
- ✅ JSONファイルに保存される
- ✅ 保存したルーティングが読み込める
- ✅ 読み込み後の音響が一致する

---

### **Unit 2.8: デバッグ機能追加**

**ファイル**: `src/ofApp.cpp`
**所要時間**: 30分
**優先度**: 🟢 通常 (開発効率向上)

#### 背景:
開発時のデバッグを効率化する機能。

#### 実装指示: drawGui()にデバッグパネル追加

```cpp
// drawGui() に追加:

if (ImGui::CollapsingHeader("Debug & Testing")) {
    ImGui::Text("Audio Routing Debug:");
    ImGui::Separator();

    // Display real-time envelope values
    ImGui::Text("Envelope P1: %.4f", displayEnvelopeP1_);
    ImGui::Text("Envelope P2: %.4f", displayEnvelopeP2_);

    ImGui::Spacing();

    // Display real-time BPM values
    const auto metricsP1 = audioPipeline_.channelMetrics(ParticipantId::Participant1);
    const auto metricsP2 = audioPipeline_.channelMetrics(ParticipantId::Participant2);
    ImGui::Text("BPM P1: %.1f", metricsP1.bpm);
    ImGui::Text("BPM P2: %.1f", metricsP2.bpm);

    ImGui::Spacing();

    // Display output buffer values
    ImGui::Text("Output Buffer:");
    {
        std::lock_guard<std::mutex> lock(audioMutex_);
        ImGui::Text("  CH1: %.4f", currentEnvelopes_[0]);
        ImGui::Text("  CH2: %.4f", currentEnvelopes_[1]);
        ImGui::Text("  CH3: (haptic)");
        ImGui::Text("  CH4: (haptic)");
    }

    ImGui::Spacing();
    ImGui::Separator();

    // Quick scene jump
    ImGui::Text("Quick Scene Jump:");
    const double nowSeconds = ofGetElapsedTimef();
    if (ImGui::Button("Jump to FirstPhase")) {
        sceneController_.requestState(SceneState::FirstPhase, nowSeconds, true, "debug_jump");
    }
    ImGui::SameLine();
    if (ImGui::Button("Jump to Exchange")) {
        sceneController_.requestState(SceneState::Exchange, nowSeconds, true, "debug_jump");
    }
    ImGui::SameLine();
    if (ImGui::Button("Jump to Mixed")) {
        sceneController_.requestState(SceneState::Mixed, nowSeconds, true, "debug_jump");
    }

    ImGui::Spacing();
    ImGui::Separator();

    // Test tones
    ImGui::Text("Test Audio Generation:");
    static bool enableTestTone = false;
    ImGui::Checkbox("Enable 440Hz Test Tone (CH1/2)", &enableTestTone);

    // Note: 実際のテストトーン生成はaudioOut()に実装が必要
    // ここではUIのみ提供

    ImGui::Spacing();
    ImGui::Separator();

    // Performance metrics
    ImGui::Text("Performance:");
    ImGui::Text("FPS: %.1f", ofGetFrameRate());
    ImGui::Text("CPU: (monitor externally)");
}
```

#### テスト方法:

```bash
# 1. ビルド
make Release

# 2. デバッグパネル確認
# "Debug & Testing" セクションが表示される

# 3. リアルタイム値確認
# Envelope P1/P2 が心拍に応じて変化
# BPM P1/P2 が表示される

# 4. クイックジャンプテスト
# "Jump to Exchange" をクリック
# 即座にExchangeシーンに遷移
# 通常のシーン遷移ロジックをバイパス

# 5. パフォーマンス確認
# FPS が安定して60付近
```

#### 成功基準:
- ✅ デバッグパネルが表示される
- ✅ リアルタイム値が正しく表示される
- ✅ クイックシーンジャンプが動作する

---

## ✅ Phase 2 完了チェックリスト

- [ ] **Unit 2.1**: AudioRouter公開API拡張
- [ ] **Unit 2.2**: ルーティングGUI: 基本パネル
- [ ] **Unit 2.3**: ルーティングGUI: ルール追加
- [ ] **Unit 2.4**: ルーティングGUI: ルール削除
- [ ] **Unit 2.5**: Synthetic心拍生成: インフラ
- [ ] **Unit 2.6**: Synthetic心拍生成: GUI制御
- [ ] **Unit 2.7**: ルーティングプリセット保存/読込 (オプション)
- [ ] **Unit 2.8**: デバッグ機能追加

---

## 🧪 Phase 2 統合テスト

### テストシナリオ1: GUIルーティング変更

```
1. FirstPhaseシーンで開始
2. Routing Panel を開く
3. CH1 → Participant2, Partner, 0dB, -1.0に変更
4. "Apply Rule" をクリック
5. 即座に CH1 から Participant2の心拍が聞こえる
6. "Reset to Scene Preset" をクリック
7. 元の FirstPhase ルーティングに戻る
```

### テストシナリオ2: 1人モード (Synthetic)

```
1. Participant1のマイクのみ接続
2. GUI で "Enable Synthetic Participant 2" をチェック
3. P2 BPM を 80 に設定
4. "Apply Synthetic Settings" をクリック
5. FirstPhase → Exchange に遷移
6. P1のヘッドフォンで Synthetic P2 (80BPM) が聞こえる
7. ビジュアル: 右側がSynthetic P2で動作
```

### テストシナリオ3: プリセット保存/読込

```
1. 複数のルールを手動設定
2. "config/my_preset.json" に保存
3. "Clear All Rules" で全削除
4. アプリケーション再起動
5. "Load Preset" で復元
6. 保存したルーティングが復元される
```

---

## 🎯 Phase 2 成功基準

### 機能完全性:
- ✅ **問題3解決**: GUIからルーティングを動的に設定できる
- ✅ **1人モード**: Synthetic心拍で1人デモが可能
- ✅ **柔軟性**: 実行中にルーティング変更可能

### 品質基準:
- ✅ Phase 1の機能が引き続き動作
- ✅ GUI操作でクラッシュしない
- ✅ Synthetic心拍が自然に聞こえる

### ユーザー体験:
- ✅ デモ設定が簡単
- ✅ テストモードで開発効率向上
- ✅ 1人でも体験デモが可能

---

## 📚 参照ドキュメント

- **REQUIREMENTS_PHASE1_CORE_FUNCTIONS.md**: Phase 1要件 (前提条件)
- **PROBLEM_ANALYSIS_AND_ROOT_CAUSES.md**: 問題分析
- **MASTERDOCUMENT.md**: プロジェクト全体仕様

---

**最終更新**: 2025-10-29
**ドキュメントバージョン**: 1.0
**対象リリース**: MVP Phase 2
