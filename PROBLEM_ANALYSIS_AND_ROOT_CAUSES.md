# 問題分析と根本原因 (2025-10-29)

**テスト運用結果**: 一部動作、複数の致命的な問題あり

---

## 🔴 発見された4つの主要問題

### **問題1: Exchangeシーンでオーディオルーティングが切り替わらない**
**症状**: Exchangeシーンに遷移しても、相手の心拍音が聞こえない

**根本原因**:
1. **AudioRouterインスタンスが存在しない**
   - `src/ofApp.h`: `audioRouter_` メンバー変数が宣言されていない
   - `src/ofApp.cpp`: AudioRouterのsetup()やroute()が呼ばれていない

2. **applyScenePreset()がスタブのまま**
   - `src/audio/AudioRouter.cpp:34-37`: 空実装
   ```cpp
   void AudioRouter::applyScenePreset(SceneState scene) {
       (void)scene;
       // Phase 4: Scene-dependent routing will be implemented later.
   }
   ```

3. **audioOut()でルーティングが適用されていない**
   - `src/ofApp.cpp:450`: AudioPipeline::audioOut()を呼ぶだけ
   - AudioRouter::route()が呼ばれていない

**影響**: 🔴 致命的
- FirstPhase, Exchange, Mixedの全シーンで音響が同じ
- 心拍音の交換が実現できない
- 体験の核となる相互作用が機能しない

---

### **問題2: 片方のマイクだけがビジュアルと連動している**
**症状**: 2つのマイクは認識されているが、1つのマイクの心拍だけがビジュアルに反映される

**根本原因**:
1. **単一のBeatEventストリームしか使っていない**
   - `src/ofApp.cpp:281`: `audioPipeline_.pollBeatEvents()` (引数なし)
   - AudioPipelineは2チャンネル分のBeatEventを持つが、統合されたストリームしか返していない

2. **ビジュアル更新が1つのエンベロープだけを使用**
   - `src/ofApp.cpp:297`: `displayEnvelope_ = blendedEnvelope();`
   - AudioPipeline::latestMetrics()は統合メトリクスを返す (1チャンネル分のみ)
   - Participant1とParticipant2の独立したエンベロープがビジュアルに反映されていない

3. **シェーダーに渡すエンベロープが1つだけ**
   - `src/ofApp.cpp:745`: `starfieldShader_.setUniform1f("uEnvelope", env);`
   - `src/ofApp.cpp:761`: `rippleShader_.setUniform1f("uEnvelope", env);`
   - 2つの参加者を区別するビジュアル表現がない

**影響**: 🔴 致命的
- 2人目の参加者の心拍がビジュアルに現れない
- 2人デモが実質1人デモになってしまう
- 相互作用が視覚的に確認できない

---

### **問題3: ルーティングGUIが実装されていない**
**症状**: GUIからオーディオルーティングを設定できない

**根本原因**:
1. **ルーティングパネルのGUIコードが存在しない**
   - `src/ofApp.cpp`にImGuiのルーティング設定UIが実装されていない
   - AudioRouter::rules()へのアクセスコードがない

2. **AudioRouterの公開APIが不足**
   - `src/audio/AudioRouter.h`: `rules()`、`addRule()`、`clearRules()` メソッドが存在しない
   - 外部からルールを追加/削除する方法がない

**影響**: 🟡 重大
- デモ時の柔軟な設定変更ができない
- テストモード、1人モード、2人モードの切り替えが手動で不可能
- コード変更が必要 (リコンパイル必須)

---

### **問題4: Haptics信号生成が未実装**
**症状**: CH3/4 (ハプティクス出力) に信号が出ない

**根本原因**:
1. **numOutputChannels = 2のまま**
   - `src/ofApp.cpp:172`: `settings.numOutputChannels = 2;`
   - 4チャンネル出力が有効化されていない

2. **generateHapticSample()がスタブのまま**
   - `src/audio/AudioRouter.cpp:71-75`: `return 0.0f;`
   ```cpp
   float AudioRouter::generateHapticSample(float envelope, ParticipantId id) {
       (void)id;
       (void)envelope;
       return 0.0f;
   }
   ```

3. **AudioRouter統合の前提問題**
   - 問題1と同根: AudioRouter自体が統合されていない

**影響**: 🔴 致命的
- ハプティクストランスデューサーが動作しない
- 触覚フィードバックが全く提供できない
- 体験の重要な要素(触覚)が欠落

---

## 📊 実装状況マトリクス

| コンポーネント | 期待状態 | 実際の状態 | ギャップ |
|--------------|---------|-----------|---------|
| **AudioPipeline 2ch分離** | ✅ 実装済み | ✅ 動作中 | なし |
| **BeatTimeline×2** | ✅ 実装済み | ✅ 動作中 | なし |
| **AudioRouterフレームワーク** | ✅ 実装済み | ⚠️ 未統合 | ofApp統合なし |
| **4ch出力設定** | ✅ numOutputChannels=4 | ❌ 2のまま | 1行変更必要 |
| **AudioRouter::route()呼び出し** | ✅ audioOut()内 | ❌ 未呼び出し | 統合コード必要 |
| **applyScenePreset()実装** | ✅ シーン別ルール | ❌ スタブ | 実装コード必要 |
| **generateHapticSample()実装** | ✅ 50Hz正弦波 | ❌ return 0.0f | 実装コード必要 |
| **2ch独立ビジュアル** | ✅ 2人分表示 | ❌ 1人分のみ | ビジュアルロジック必要 |
| **ルーティングGUI** | ✅ ImGuiパネル | ❌ 未実装 | GUI実装必要 |
| **ParticipantId別BeatEvent取得** | ✅ pollBeatEvents(id) | ⚠️ 使用されていない | API呼び出し変更 |

**実装率**: 約40% (基盤は整っているが、統合と実装が不足)

---

## 🔍 技術的な詳細分析

### 問題1の詳細: AudioRouter未統合

**現在のコールスタック**:
```
ofApp::audioOut()
  └─ audioPipeline_.audioOut(buffer)
       └─ 2ch stereo output only
       └─ AudioRouter::route() は呼ばれない ❌
```

**期待されるコールスタック**:
```
ofApp::audioOut()
  ├─ audioPipeline_.channelMetrics(Participant1) → envelope1
  ├─ audioPipeline_.channelMetrics(Participant2) → envelope2
  ├─ audioRouter_.route([envelope1, envelope2], outputBuffer)
  └─ buffer[CH1/2/3/4] = outputBuffer[0/1/2/3]
```

**AudioPipeline::audioOut()の現在の動作**:
```cpp
// src/audio/AudioPipeline.cpp:314-353
void AudioPipeline::audioOut(ofSoundBuffer& buffer) {
    // ... calibration handling ...

    // 2チャンネルステレオ出力を生成
    for (std::size_t frame = 0; frame < numFrames; ++frame) {
        // ノイズ + セルフモニタリング
        output[frame * 2 + 0] = /* ... */;  // CH1 (L)
        output[frame * 2 + 1] = /* ... */;  // CH2 (R)
    }
    // CH3/4 への出力なし ❌
}
```

### 問題2の詳細: 単一メトリクスストリーム

**現在のupdate()フロー**:
```cpp
// src/ofApp.cpp:276-283
const auto metrics = audioPipeline_.latestMetrics();  // 統合メトリクス (1チャンネル分)
applyBeatMetrics(metrics, nowSeconds);
const auto events = audioPipeline_.pollBeatEvents();  // 統合イベント
handleBeatEvents(events, nowSeconds);
```

**AudioPipeline内部**:
```cpp
// src/audio/AudioPipeline.h:60-65
std::array<BeatTimeline, 2> beatTimelines_{};  // ✅ 2つ存在
std::array<ChannelMetrics, 2> channelMetrics_{};  // ✅ 2つ存在
std::array<std::deque<BeatEvent>, 2> pendingEventsByChannel_{};  // ✅ 2つ存在

// しかし使われているAPI:
AudioPipeline::Metrics latestMetrics() const;  // 統合メトリクス返す ❌
std::vector<BeatEvent> pollBeatEvents();  // 統合イベント返す ❌

// 使われていないAPI:
ChannelMetrics channelMetrics(ParticipantId id) const;  // ✅ 個別取得可能だが未使用
std::vector<BeatEvent> pollBeatEvents(ParticipantId id);  // ✅ 個別取得可能だが未使用
```

**期待されるフロー**:
```cpp
const auto metricsP1 = audioPipeline_.channelMetrics(ParticipantId::Participant1);
const auto metricsP2 = audioPipeline_.channelMetrics(ParticipantId::Participant2);
const auto eventsP1 = audioPipeline_.pollBeatEvents(ParticipantId::Participant1);
const auto eventsP2 = audioPipeline_.pollBeatEvents(ParticipantId::Participant2);

// ビジュアルを2人分独立して更新
updateVisuals(metricsP1, metricsP2);
```

### 問題3の詳細: GUI API不足

**AudioRouter.h の現在の公開API**:
```cpp
// src/audio/AudioRouter.h
class AudioRouter {
public:
    void setRoutingRule(OutputChannel channel, const RoutingRule& rule);
    const RoutingRule& routingRule(OutputChannel channel) const;
    void applyScenePreset(SceneState scene);  // スタブ
    void route(const std::array<float, 2>& inputEnvelopes,
               std::array<float, 4>& outputBuffer);

    // 不足している公開API: ❌
    // const std::vector<RoutingRule>& rules() const;
    // void addRule(const RoutingRule& rule);
    // void clearRules();
    // std::size_t ruleCount() const;
};
```

### 問題4の詳細: ハプティクス生成

**要求仕様** (MASTERDOCUMENT.mdより):
- Dayton Audio DAEX25 ハプティクストランスデューサー
- 動作周波数: 20-150Hz
- 心拍BPMに同期した触覚パルス生成

**現在の実装**:
```cpp
// src/audio/AudioRouter.cpp:71-75
float AudioRouter::generateHapticSample(float envelope, ParticipantId id) {
    (void)id;
    (void)envelope;
    return 0.0f;  // ❌ 何も生成しない
}
```

**必要な実装例**:
```cpp
float AudioRouter::generateHapticSample(float envelope, ParticipantId id) {
    // 50Hz正弦波をエンベロープで変調
    constexpr float kHapticFrequency = 50.0f;  // Hz
    constexpr float kHapticGain = 0.8f;

    // Phase accumulator per participant
    static std::array<double, 2> phase = {0.0, 0.0};
    const std::size_t idx = static_cast<std::size_t>(id);

    const float sample = std::sin(phase[idx] * 2.0 * M_PI);
    phase[idx] += kHapticFrequency / sampleRate_;
    if (phase[idx] >= 1.0) phase[idx] -= 1.0;

    return sample * envelope * kHapticGain;
}
```

---

## 🎯 優先順位付け

### 致命的 (🔴 即座に修正必要):
1. **問題1**: AudioRouter統合 + applyScenePreset()実装
2. **問題2**: 2チャンネル独立ビジュアル
3. **問題4**: ハプティクス信号生成 + 4ch出力有効化

### 重大 (🟡 近日中に修正):
4. **問題3**: ルーティングGUI実装

---

## 📋 2フェーズ実装計画サマリー

### **Phase 1: コア機能修復 (問題1, 2, 4)**
- AudioRouter統合とシーンプリセット実装
- 2チャンネル独立処理とビジュアル対応
- 4ch出力とハプティクス生成
- **目標**: 2人デモが基本動作する

### **Phase 2: GUI機能追加 (問題3)**
- ルーティングGUI実装
- 1人モード (Synthetic心拍)
- 動的ルーティング変更
- **目標**: デモの柔軟性とテスト容易性向上

---

## 📁 影響を受けるファイル一覧

| ファイル | Phase 1変更 | Phase 2変更 | 変更の性質 |
|---------|-----------|-----------|----------|
| `src/ofApp.h` | ✅ 必須 | ✅ 必須 | AudioRouterメンバー追加、GUI状態変数追加 |
| `src/ofApp.cpp` | ✅ 必須 | ✅ 必須 | setup(), audioOut(), update(), drawGUI()変更 |
| `src/audio/AudioRouter.h` | - | ✅ 必須 | 公開API追加 (rules, addRule, clearRules) |
| `src/audio/AudioRouter.cpp` | ✅ 必須 | - | applyScenePreset(), generateHapticSample()実装 |
| `bin/data/shaders/starfield.frag` | ✅ 任意 | - | 2人分のエンベロープ対応 (オプション) |
| `bin/data/shaders/ripple.frag` | ✅ 任意 | - | 2人分のエンベロープ対応 (オプション) |

---

## ✅ 次のステップ

2つの要件定義書を作成します:

1. **REQUIREMENTS_PHASE1_CORE_FUNCTIONS.md**
   - AudioRouter統合
   - 2チャンネル独立ビジュアル
   - 4ch出力とハプティクス生成
   - 最小単位の実装指示

2. **REQUIREMENTS_PHASE2_GUI_FLEXIBILITY.md**
   - ルーティングGUI実装
   - 1人モード (Synthetic心拍)
   - 動的ルーティング変更
   - テスト機能追加

各要件定義書には以下を含めます:
- 最小単位に分割された実装項目
- 具体的なコード変更指示 (BEFORE/AFTER)
- テスト方法と成功基準
- リスクと依存関係
- 想定所要時間 (参考)
