# Phase 1 コア機能復旧要件

**優先度**: 🔴 致命的  
**対象**: Exchange シーン体験の成立に必須となる最低限のコア機能  
**スコープ**: AudioRouter 統合、4ch 出力、2ch 独立ビジュアル、ハプティクス生成

---

## 達成条件
- Exchange シーンで左右のヘッドフォンに互いの鼓動が再生される。
- 参加者ごとのエンベロープが独立して取得・可視化される。
- CH3/CH4 に 50Hz ベースのハプティクス信号が出力される。
- 既存のキャリブレーション／GUI／ログ機能は回 regress しない。

---

## 実装ユニット一覧
| Unit | 名称 | 主担当ファイル | 依存 | 所要時間 (目安) |
|------|------|----------------|------|------------------|
| 1.1 | 4ch オーディオ出力を有効化 | `src/ofApp.cpp` | なし | 0.5h |
| 1.2 | AudioRouter メンバーとバッファを追加 | `src/ofApp.h` | 1.1 | 0.5h |
| 1.3 | AudioRouter 初期化と初期プリセット適用 | `src/ofApp.cpp`, `src/audio/AudioRouter.{h,cpp}` | 1.2 | 1.0h |
| 1.4 | ハプティクス信号生成 (50Hz サイン) | `src/audio/AudioRouter.{h,cpp}` | 1.3 | 1.5h |
| 1.5 | シーン別ルーティングプリセット実装 | `src/audio/AudioRouter.cpp` | 1.3 | 1.5h |
| 1.6 | audioOut: 参加者別メトリクス取得 | `src/ofApp.cpp` | 1.2 | 1.0h |
| 1.7 | audioOut: AudioRouter で4chバッファ構築 | `src/ofApp.cpp`, `src/audio/AudioRouter.cpp` | 1.6 | 2.0h |
| 1.8 | シーン遷移時のプリセット自動適用 | `src/ofApp.cpp` | 1.5 | 0.5h |
| 1.9 | 参加者別メトリクスを UI/ログへ反映 | `src/ofApp.cpp`, `src/ofApp.h` | 1.6 | 1.5h |
| 1.10 | ビジュアル更新ロジックの2ch化 | `src/ofApp.cpp` | 1.9 | 2.0h |
| 1.11 | starfield シェーダーの 2ch 対応 | `bin/data/shaders/starfield.frag` | 1.10 | 1.0h |
| 1.12 | ripple シェーダーの 2ch 対応 | `bin/data/shaders/ripple.frag` | 1.10 | 1.0h |

---

## ユニット詳細

### Unit 1.1 — 4ch オーディオ出力を有効化
- **目的**: ハプティクス出力 (CH3/4) のためにサウンドストリームを 4 チャンネルで初期化する。
- **対象ファイル**: `src/ofApp.cpp`
- **依存関係**: なし
- **所要時間目安**: 0.5h
- **変更内容**:
  ```diff
   settings.numInputChannels = 2;
-  settings.numOutputChannels = 2;
+  settings.numOutputChannels = 4;  // CH1/2: headphones, CH3/4: haptics
   settings.bufferSize = bufferSize_;
  ```
- **テスト方法**:
  - `make Release` でビルド。
  - アプリ起動後、コンソールと OS の Audio MIDI Setup で 4ch 出力になっていることを確認。
- **成功基準**:
  - アプリがクラッシュせずに起動。
  - オーディオデバイスが 4ch で初期化される。
- **リスク / 備考**:
  - 4ch 非対応インターフェース使用時は初期化失敗するため、開発環境を事前確認する。

---

### Unit 1.2 — AudioRouter メンバーとバッファを追加
- **目的**: audioOut でルーティング結果を保持できるよう ofApp に AudioRouter と作業用バッファを組み込む。
- **対象ファイル**: `src/ofApp.h`
- **依存関係**: Unit 1.1
- **所要時間目安**: 0.5h
- **変更内容**:
  ```diff
  #include "audio/AudioPipeline.h"
+#include "audio/AudioRouter.h"
  #include "infra/SceneTransitionLogger.h"
  ```
  ```diff
      bool audioFading_ = false;
+    knot::audio::AudioRouter audioRouter_;
+    ofSoundBuffer stereoScratch_;
+    std::array<float, 2> envelopeFrame_{0.0f, 0.0f};
+    std::array<float, 2> headphoneFrame_{0.0f, 0.0f};
+    std::array<float, 4> routedFrame_{0.0f, 0.0f, 0.0f, 0.0f};
  };
  ```
- **テスト方法**:
  - `make Release` を実行しビルドのみ確認。
- **成功基準**:
  - ビルドエラーが発生しない。
- **リスク / 備考**:
  - メンバー追加に伴いヘッダの依存が増える。インクルード順に注意。

---

### Unit 1.3 — AudioRouter 初期化と初期プリセット適用
- **目的**: AudioRouter を sampleRate とともに初期化し、起動時のシーンに対応するルールを適用する。
- **対象ファイル**: `src/ofApp.cpp`, `src/audio/AudioRouter.{h,cpp}`
- **依存関係**: Unit 1.2
- **所要時間目安**: 1.0h
- **変更内容**:
  - `AudioRouter` にセットアップ API を追加。
    ```diff
    class AudioRouter {
    public:
-       void setRoutingRule(OutputChannel channel, const RoutingRule& rule);
+       void setup(float sampleRateHz);
+       void setRoutingRule(OutputChannel channel, const RoutingRule& rule);
    ```
    ```diff
    void AudioRouter::setup(float sampleRateHz) {
        sampleRateHz_ = sampleRateHz;
        hapticPhase_.fill(0.0);
        clearRules();
    }
    ```
    ```diff
    private:
        std::array<RoutingRule, 4> rules_{};
-       float generateHapticSample(float envelope, ParticipantId id);
+       float sampleRateHz_ = 48000.0f;
+       std::array<double, 2> hapticPhase_{{0.0, 0.0}};
+       void clearRules();
+       float generateHapticSample(float envelope, ParticipantId id);
    ```
  - `clearRules()` を `AudioRouter.cpp` に実装。
  - `ofApp::setup()` で AudioRouter を初期化。
    ```diff
        audioPipeline_.setup(sampleRate_, inputGainDb);
+       audioRouter_.setup(static_cast<float>(settings.sampleRate));
+       audioRouter_.applyScenePreset(sceneController_.currentScene());
    ```
- **テスト方法**:
  - アプリ起動時にログへ `AudioRouter initialized` が出力されるか確認。
- **成功基準**:
  - AudioRouter 初期化後に例外が発生しない。
  - 既定シーン (通常 Idle) に対応したルールが適用され初期状態がミュートになる。
- **リスク / 備考**:
  - sampleRate を int/float で受け渡す際のキャスト漏れに注意。

---

### Unit 1.4 — ハプティクス信号生成 (50Hz サイン)
- **目的**: generateHapticSample() を実装し、各参加者に独立した 50Hz 正弦波を生成する。
- **対象ファイル**: `src/audio/AudioRouter.cpp`
- **依存関係**: Unit 1.3
- **所要時間目安**: 1.5h
- **変更内容**:
  ```diff
  float AudioRouter::generateHapticSample(float envelope, ParticipantId id) {
-    (void)id;
-    (void)envelope;
-    return 0.0f;
+    const auto idx = participantIndex(id);
+    if (!idx) {
+        return 0.0f;
+    }
+    constexpr float kHapticFreq = 50.0f;
+    constexpr float kGain = 0.8f;
+    const double phase = hapticPhase_[*idx];
+    const float carrier = std::sin(static_cast<float>(phase * TWO_PI));
+    hapticPhase_[*idx] = std::fmod(phase + (kHapticFreq / sampleRateHz_), 1.0);
+    return carrier * std::clamp(envelope, 0.0f, 1.0f) * kGain;
  }
  ```
- **テスト方法**:
  - ビルド後、オーディオインターフェースの CH3/CH4 を録音し FFT で 50Hz ピークを確認。
  - beats がゼロのときは無音、強いときは振幅増大するか確認。
- **成功基準**:
  - CH3/CH4 に分離した 50Hz 波形が出力される。
  - 参加者ごとに位相が独立している。
- **リスク / 備考**:
  - 実機トランスデューサで振幅が過大な場合は kGain を調整。

---

### Unit 1.5 — シーン別ルーティングプリセット実装
- **目的**: FirstPhase/Exchange/Mixed/Idle の各シーンに合わせたルーティングルールを AudioRouter へ定義。
- **対象ファイル**: `src/audio/AudioRouter.cpp`
- **依存関係**: Unit 1.3
- **所要時間目安**: 1.5h
- **変更内容**:
  ```diff
  void AudioRouter::applyScenePreset(SceneState scene) {
-    (void)scene;
-    // Phase 4: Scene-dependent routing will be implemented later.
+    clearRules();
+    auto setRule = [&](OutputChannel ch, ParticipantId source, MixMode mode, float gainDb, float pan) {
+        const std::size_t idx = static_cast<std::size_t>(ch);
+        rules_[idx].source = source;
+        rules_[idx].mixMode = mode;
+        rules_[idx].gainDb = gainDb;
+        rules_[idx].panLR = pan;
+    };
+    switch (scene) {
+        case SceneState::Idle:
+        case SceneState::Start:
+            break; // 全ミュート
+        case SceneState::FirstPhase:
+            setRule(OutputChannel::CH1_HeadphoneLeft, ParticipantId::Participant1, MixMode::Self, 0.0f, -1.0f);
+            setRule(OutputChannel::CH2_HeadphoneRight, ParticipantId::Participant2, MixMode::Self, 0.0f, 1.0f);
+            setRule(OutputChannel::CH3_HapticP1, ParticipantId::Participant1, MixMode::Haptic, 0.0f, 0.0f);
+            setRule(OutputChannel::CH4_HapticP2, ParticipantId::Participant2, MixMode::Haptic, 0.0f, 0.0f);
+            break;
+        case SceneState::Exchange:
+            setRule(OutputChannel::CH1_HeadphoneLeft, ParticipantId::Participant2, MixMode::Partner, 0.0f, -1.0f);
+            setRule(OutputChannel::CH2_HeadphoneRight, ParticipantId::Participant1, MixMode::Partner, 0.0f, 1.0f);
+            setRule(OutputChannel::CH3_HapticP1, ParticipantId::Participant1, MixMode::Haptic, 0.0f, 0.0f);
+            setRule(OutputChannel::CH4_HapticP2, ParticipantId::Participant2, MixMode::Haptic, 0.0f, 0.0f);
+            break;
+        case SceneState::Mixed:
+        case SceneState::End:
+            setRule(OutputChannel::CH1_HeadphoneLeft, ParticipantId::Participant1, MixMode::Self, -3.0f, -0.5f);
+            setRule(OutputChannel::CH2_HeadphoneRight, ParticipantId::Participant2, MixMode::Self, -3.0f, 0.5f);
+            setRule(OutputChannel::CH3_HapticP1, ParticipantId::Participant1, MixMode::Haptic, 0.0f, 0.0f);
+            setRule(OutputChannel::CH4_HapticP2, ParticipantId::Participant2, MixMode::Haptic, 0.0f, 0.0f);
+            break;
+    }
  }
  ```
- **テスト方法**:
  - 各シーンへ順に遷移し、ログで適用されたプリセットを確認。
  - Exchange 時に左右が入れ替わることをヘッドフォンで確認。
- **成功基準**:
  - シーン遷移ごとに期待したルーティングに切り替わる。
- **リスク / 備考**:
  - Mixed シーンで真のミックスを行うには Phase2 以降に複数ルール対応が必要 (現状は準備段階)。

---

### Unit 1.6 — audioOut: 参加者別メトリクス取得
- **目的**: audioOut 実行ごとに 2 参加者の envelope をキャッシュし、ハプティクスとビジュアル更新に利用可能な状態にする。
- **対象ファイル**: `src/ofApp.cpp`
- **依存関係**: Unit 1.2
- **所要時間目安**: 1.0h
- **変更内容**:
  ```diff
  void ofApp::audioOut(ofSoundBuffer& output) {
-    audioPipeline_.audioOut(output);
+    const auto metricsP1 = audioPipeline_.channelMetrics(knot::audio::ParticipantId::Participant1);
+    const auto metricsP2 = audioPipeline_.channelMetrics(knot::audio::ParticipantId::Participant2);
+    envelopeFrame_[0] = std::clamp(metricsP1.envelope, 0.0f, 1.0f);
+    envelopeFrame_[1] = std::clamp(metricsP2.envelope, 0.0f, 1.0f);
  ```
  - 続く Unit 1.7 で使用するため、`stereoScratch_` のサイズを `output.getNumFrames()` に合わせて確保。
- **テスト方法**:
  - `ofLogVerbose` などで envelope 値が 0〜1 の範囲で更新されていることを確認 (デバッグ用に一時ログ出力)。
- **成功基準**:
  - audioOut が 2 名分の envelope を毎バッファ更新できる。
- **リスク / 備考**:
  - `channelMetrics()` はロックを取るため、audioOut 内で呼ぶ回数を2回に抑える。

---

### Unit 1.7 — audioOut: AudioRouter で4chバッファ構築
- **目的**: AudioPipeline から得たステレオオーディオを AudioRouter に渡し、4ch 出力を組み立てる。
- **対象ファイル**: `src/ofApp.cpp`, `src/audio/AudioRouter.cpp`
- **依存関係**: Unit 1.6
- **所要時間目安**: 2.0h
- **変更内容**:
  - `AudioRouter::route` のシグネチャを拡張。
    ```diff
-   void route(const std::array<float, 2>& inputEnvelopes,
-              std::array<float, 4>& outputBuffer);
+   void route(const std::array<float, 2>& headphoneInput,
+              const std::array<float, 2>& inputEnvelopes,
+              std::array<float, 4>& outputFrame);
    ```
    ```diff
-   const auto& rule = rules_[outputIdx];
-   const auto participant = participantIndex(rule.source);
-   if (!participant || rule.mixMode == MixMode::Silent) {
-       outputBuffer[outputIdx] = 0.0f;
+   const auto& rule = rules_[outputIdx];
+   const auto participant = participantIndex(rule.source);
+   if (!participant || rule.mixMode == MixMode::Silent) {
+       outputFrame[outputIdx] = 0.0f;
        continue;
    }
-
-   float sample = 0.0f;
-   switch (rule.mixMode) {
-       case MixMode::Self:
-       case MixMode::Partner:
-           sample = inputEnvelopes[*participant];
-           break;
-       case MixMode::Haptic:
-           sample = generateHapticSample(inputEnvelopes[*participant], rule.source);
-           break;
+   float sample = 0.0f;
+   switch (rule.mixMode) {
+       case MixMode::Self:
+       case MixMode::Partner:
+           sample = headphoneInput[*participant];
+           break;
+       case MixMode::Haptic:
+           sample = generateHapticSample(inputEnvelopes[*participant], rule.source);
+           break;
        ...
-   outputBuffer[outputIdx] = sample * gainLinear;
+   outputFrame[outputIdx] = sample * gainLinear;
    ```
  - `ofApp::audioOut` を 4ch 書き込みへ変更。
    ```diff
  void ofApp::audioOut(ofSoundBuffer& output) {
-    audioPipeline_.audioOut(output);
+    const std::size_t frames = output.getNumFrames();
+    const std::size_t channels = output.getNumChannels();
+    stereoScratch_.allocate(frames, 2);
+    audioPipeline_.audioOut(stereoScratch_);
+    float* dst = output.getBuffer().data();
+    const float* src = stereoScratch_.getBuffer().data();
+    for (std::size_t frame = 0; frame < frames; ++frame) {
+        headphoneFrame_[0] = src[frame * 2];
+        headphoneFrame_[1] = src[frame * 2 + 1];
+        audioRouter_.route(headphoneFrame_, envelopeFrame_, routedFrame_);
+        dst[frame * channels + 0] = routedFrame_[0];
+        dst[frame * channels + 1] = routedFrame_[1];
+        dst[frame * channels + 2] = routedFrame_[2];
+        dst[frame * channels + 3] = routedFrame_[3];
+    }
    // 既存のフェード処理はこの直後に残す
    ```
- **テスト方法**:
  - Exchange シーンで左右の音が入れ替わるか実機ヘッドフォンで確認。
  - CH3/CH4 を DAW で録音し、ハプティクス波形が出力されるか確認。
- **成功基準**:
  - 4ch バッファが毎フレーム正しく埋まる。
  - 2ch フェード処理が継続して機能。
- **リスク / 備考**:
  - `ofSoundBuffer::allocate` は毎フレーム再確保になるため、必要に応じ `if (stereoScratch_.getNumFrames() != frames)` で最適化。

---

### Unit 1.8 — シーン遷移時のプリセット自動適用
- **目的**: TransitionEvent 完了時に AudioRouter のプリセットを更新し、Exchange 直後に正しいルーティングへ切り替える。
- **対象ファイル**: `src/ofApp.cpp`
- **依存関係**: Unit 1.5
- **所要時間目安**: 0.5h
- **変更内容**:
  ```diff
  if (event.completed) {
      if (event.to == SceneState::Exchange || event.to == SceneState::Mixed) {
          ...
      }
+     audioRouter_.applyScenePreset(event.to);
+     ofLogNotice("ofApp") << "Scene preset applied: " << sceneStateToString(event.to);
  }
  ```
- **テスト方法**:
  - シーン遷移ログにプリセット適用が出力されるか確認。
- **成功基準**:
  - Exchange への遷移完了直後にルーティングが切り替わる。
- **リスク / 備考**:
  - 連続遷移時に applyScenePreset の多重呼び出しが起こっても安全な実装にする（Unit1.5の `clearRules()` が担保）。

---

### Unit 1.9 — 参加者別メトリクスを UI/ログへ反映
- **目的**: 2 人分の BPM/Envelope/Beat イベントを個別に取得し、GUI／ログ／ハプティクス履歴に反映。
- **対象ファイル**: `src/ofApp.cpp`, `src/ofApp.h`
- **依存関係**: Unit 1.6
- **所要時間目安**: 1.5h
- **変更内容**:
  - `ofApp.h` に 2 名分のメトリクスとイベントバッファを追加。
    ```diff
    std::array<float, 2> envelopeFrame_{0.0f, 0.0f};
+   std::array<float, 2> bpmFrame_{0.0f, 0.0f};
+   std::array<std::uint64_t, 2> beatCount_{0, 0};
    ```
  - `ofApp::update()` で `audioPipeline_.pollBeatEvents(ParticipantId::Participant1/2)` を使い、`handleBeatEvents` を 2 名分で呼び分け。
  - `applyBeatMetrics` を拡張し、`latestMetricsP1_` / `latestMetricsP2_` (新規メンバー) に保存。
- **テスト方法**:
  - GUI パネルに P1/P2 の BPM・Envelope を表示して値が独立更新されるか確認。
- **成功基準**:
  - 参加者ごとのメトリクスがログおよび GUI 上で分離表示される。
- **リスク / 備考**:
  - 既存の単一 `latestMetrics_` を参照している箇所が多いため、影響範囲を洗い出して差し替える。

---

### Unit 1.10 — ビジュアル更新ロジックの 2ch 化
- **目的**: starfield/ripple の描画呼び出しを 2 名分の envelope に対応させ、シーンごとの表示ロジックを更新。
- **対象ファイル**: `src/ofApp.cpp`
- **依存関係**: Unit 1.9
- **所要時間目安**: 2.0h
- **変更内容**:
  - `drawScene()` 内で `drawStarfieldLayer` / `drawRippleLayer` を呼ぶ際に `envelopeFrame_[0/1]` を渡す。
  - `drawStarfieldLayer` / `drawRippleLayer` のシグネチャを `float envelopeP1, float envelopeP2` に変更。
    ```diff
-void ofApp::drawStarfieldLayer(float alpha, double nowSeconds, float envelope);
+void ofApp::drawStarfieldLayer(float alpha, double nowSeconds, float envelopeP1, float envelopeP2);
    ```
    同様に ripple も変更し、内部でシェーダーへ 2 つの uniform を送る。
- **テスト方法**:
  - P1 のマイクのみ入力した場合、左側ビジュアルが強く右側は弱いことを確認。
  - P2 を有効にすると逆側に反映されるか確認。
- **成功基準**:
  - 2 名の envelope に応じた明滅／アニメーションが確認できる。
- **リスク / 備考**:
  - 既存の単一 `displayEnvelope_` を参照している UI へ影響しないよう段階的に移行。

---

### Unit 1.11 — starfield シェーダーの 2ch 対応
- **目的**: GLSL シェーダーで 2 名分の envelope を受け取り、左右または奥行き表現に反映。
- **対象ファイル**: `bin/data/shaders/starfield.frag`
- **依存関係**: Unit 1.10
- **所要時間目安**: 1.0h
- **変更内容**:
  ```diff
-uniform float uEnvelope;
+uniform float uEnvelopeP1;
+uniform float uEnvelopeP2;
  ...
-float intensity = uEnvelope;
+float intensity = mix(uEnvelopeP1, uEnvelopeP2, uv.x);
  ```
  - `intensity` を左右位置などに応じてブレンドし、2 名の差が視覚化されるよう調整。
- **テスト方法**:
  - P1 のみ信号 → 左半分中心の星が活性化。
  - P2 のみ信号 → 右半分中心の星が活性化。
- **成功基準**:
  - シェーダーがコンパイル成功し、描画が 2 ch に反応。
- **リスク / 備考**:
  - `starfield.vert` に変更が不要か確認。uniform 追加のみで完結する想定。

---

### Unit 1.12 — ripple シェーダーの 2ch 対応
- **目的**: ripple エフェクトを 2 名分の envelope で制御し、中心位置や波数を個別化。
- **対象ファイル**: `bin/data/shaders/ripple.frag`
- **依存関係**: Unit 1.10
- **所要時間目安**: 1.0h
- **変更内容**:
  ```diff
-uniform float uEnvelope;
+uniform vec2 uEnvelopePair; // x=P1, y=P2
  ...
-float amp = uEnvelope;
+float amp = mix(uEnvelopePair.x, uEnvelopePair.y, uv.x);
  ```
  - 必要に応じて 2 つの円を同時発生させるなど調整。
- **テスト方法**:
  - 参加者ごとの入力で波紋の位置や強度が変化するか確認。
- **成功基準**:
  - ripple エフェクトが 2 名分の envelope に追従。
- **リスク / 備考**:
  - モバイル GPU などで uniform 追加によるパフォーマンス影響がないか軽く確認。

---

## Phase 1 完了後に得られる結果
- Exchange シーンで心拍が正しく交換され、2 人の存在感と没入感が復活する。
- 触覚フィードバックが復旧し、CH3/CH4 から実機トランスデューサが駆動。
- ビジュアルが 2 名分の鼓動に追従し、デモ体験の公平性が保たれる。
- Phase 2 (GUI/柔軟性) 実装に進むための技術的土台が整う。
