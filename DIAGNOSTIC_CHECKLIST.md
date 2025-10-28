# 診断チェックリスト - ビジュアルが消える問題

**作成日**: 2025-10-29
**対象**: 実装担当エンジニア
**目的**: 「一瞬だけビジュアルが出て消える」「シェーダーが適用されていない」「チャンネル分離が不明」の3つの問題を診断・修正する

---

## 現状の動作確認 (ログから判明した事実)

✅ **正常動作している部分**:
- Scene transition: Idle→Start 遷移が2回成功 (scene_transitions.csv)
- Telemetry: 613行のデータ記録 = 約2.5分間動作継続
- Envelope detection: 0.007 → 0.436 まで増加中 (proto_session.csv:613行目)
- BPM detection: 15.8 → 58-59 の範囲で検出中

❌ **異常な部分**:
- Calibration: CH1 gain=27.97x (28.93dB), CH2 gain=3.98x (12dB) = マイク入力が極端に小さい
- Envelope値: 0.01-0.10 (正常値 0.3-0.8 の 3-25%)
- Shader files: `bin/data/shaders/*.{vert,frag}` が1ファイルも存在しない

---

## 問題1: ビジュアルが一瞬で消える

### 診断手順

#### STEP 1-1: Scene transition の alpha blend を確認

**場所**: [src/ofApp.cpp:861-867](src/ofApp.cpp#L861-L867)

```cpp
if (sceneController_.isTransitioning()) {
    const float blend = easedBlend(sceneController_.transitionBlend());
    drawLayer(sceneController_.currentState(), 1.0f - blend);  // ← 旧シーンの alpha
    drawLayer(sceneController_.targetState(), blend);           // ← 新シーンの alpha
} else {
    drawLayer(state, std::clamp(alpha, 0.0f, 1.0f));
}
```

**確認事項**:
- [ ] `sceneController_.isTransitioning()` が true のまま固着していないか?
  - **検証方法**: `ofApp::draw()` 冒頭に `ofLogNotice() << "isTransitioning=" << sceneController_.isTransitioning() << " blend=" << blend;` を追加
  - **期待結果**: 1.2秒後に `isTransitioning=false` になる
  - **異常時**: ずっと `isTransitioning=true` のまま → `SceneController::update()` が呼ばれていない

#### STEP 1-2: SceneController::update() の呼び出しを確認

**場所**: [src/ofApp.cpp:182](src/ofApp.cpp#L182)

```cpp
void ofApp::update() {
    const double nowSeconds = ofGetElapsedTimef();
    sceneController_.update(nowSeconds);  // ← これが呼ばれているか?
    // ...
}
```

**確認事項**:
- [ ] `ofApp::update()` 自体が呼ばれているか?
  - **検証方法**: `update()` 冒頭に `static int count = 0; if (++count % 60 == 0) ofLogNotice() << "update() called: " << count;` を追加
  - **期待結果**: 1秒に1回ログが出る
  - **異常時**: ログが出ない → openFrameworks のメインループが停止している

#### STEP 1-3: Envelope threshold による描画スキップを確認

**場所**: [src/ofApp.cpp:886-903](src/ofApp.cpp#L886-L903) (Idle scene の ripple 描画)

```cpp
const float envelope = latestMetrics_.envelope;  // ← 0.01-0.10 という極小値
const float baseRadius = 180.0f;
const float radiusOffset = envelope * 240.0f;    // ← 0.01 * 240 = 2.4 pixel しか変化しない
// ...
ofColor rippleBase(120, 140, 255, static_cast<unsigned char>(safeLerp(20.0f, 90.0f, envelope)));
// ← envelope=0.01 のとき alpha=21 (ほぼ透明)
```

**確認事項**:
- [ ] envelope が小さすぎて視認できない描画になっていないか?
  - **検証方法**: `drawIdleScene()` 冒頭に `ofLogNotice() << "drawIdleScene alpha=" << alpha << " envelope=" << latestMetrics_.envelope;` を追加
  - **期待結果**: `envelope` が 0.3 以上
  - **異常時**: `envelope < 0.1` → マイク入力が小さすぎる (問題3へ)

#### STEP 1-4: Start scene の textFadeOut stage を確認

**場所**: [src/ofApp.cpp:955-962](src/ofApp.cpp#L955-L962)

```cpp
const SceneTimingConfig::Stage* fadeInStage = stageLookup("textFadeIn");
const SceneTimingConfig::Stage* fadeOutStage = stageLookup("textFadeOut");
float fadeInFactor = fadeInStage ? stageProgress(fadeInStage) : std::clamp(static_cast<float>(timeInState), 0.0f, 1.0f);
float fadeOutFactor = 1.0f;
if (fadeOutStage && timeInState >= fadeOutStage->startAt) {
    const float progress = stageProgress(fadeOutStage);
    fadeOutFactor = std::clamp(1.0f - progress, 0.0f, 1.0f);  // ← これが 0.0 に固着?
}
```

**確認事項**:
- [ ] `config/scene_timing.json` の `textFadeOut` stage が即座に開始されていないか?
  - **検証方法**: `scene_timing.json` の Start scene で `textFadeOut.startAt` を確認
  - **期待値**: `startAt >= 3.0` (3秒後に fade out 開始)
  - **異常時**: `startAt = 0.0` → 即座に fade out → ビジュアルが消える
  - **修正**: `textFadeOut.startAt` を 8.0 秒に設定 (Start scene の duration=10秒 の 80% 時点)

**修正例 (config/scene_timing.json)**:
```json
{
  "scenes": [
    {
      "state": "Start",
      "duration": 10.0,
      "transitionTo": "FirstPhase",
      "stages": [
        {"name": "textFadeIn", "startAt": 0.0, "duration": 1.5},
        {"name": "breathingGuide", "startAt": 2.0, "duration": 6.0},
        {"name": "bellSound", "startAt": 2.5, "duration": 0.5},
        {"name": "textFadeOut", "startAt": 8.0, "duration": 2.0}  // ← これが 0.0 になっていないか確認
      ]
    }
  ]
}
```

---

## 問題2: GLSL シェーダーが適用されていない

### 診断結果: **シェーダーファイルが存在しない**

**確認済み**: `bin/data/shaders/` ディレクトリに `.vert`, `.frag` ファイルが1つも存在しない (Glob 検索結果)

### 修正手順

#### STEP 2-1: シェーダーファイルの作成

必要なシェーダー (MASTERDOCUMENT.md:186-203 参照):
- [ ] `bin/data/shaders/starfield.vert` + `starfield.frag` (2000点のパーティクル、HRV→速度変調)
- [ ] `bin/data/shaders/torus.vert` + `torus.frag` (トーラス形状、envelope→radius/厚み変調)
- [ ] `bin/data/shaders/ripple.frag` (フルスクリーン波紋、beat event→放射状波)

**検証方法**:
```bash
ls -lh bin/data/shaders/
```

**期待結果**:
```
starfield.vert  (GLSL 4.1, attribute vec3 position; uniform mat4 modelViewProjectionMatrix; ...)
starfield.frag  (GLSL 4.1, uniform float uEnvelope; out vec4 fragColor; ...)
torus.vert      (GLSL 4.1, normal transformation, tangent-space for lighting)
torus.frag      (GLSL 4.1, phong shading, envelope-driven emission)
ripple.frag     (GLSL 4.1, fragment shader only, distance field-based ripple)
```

#### STEP 2-2: ofShader のロード・コンパイル確認

**場所**: [src/ofApp.cpp:69-80](src/ofApp.cpp#L69-L80) (setup() 内でシェーダーロード)

**予想される実装** (現在未実装の可能性が高い):
```cpp
void ofApp::setup() {
    // ...
    starfieldShader_.load("shaders/starfield");  // ← これが追加されているか?
    if (!starfieldShader_.isLoaded()) {
        ofLogError("ofApp") << "Failed to load starfield shader";
    }
    torusShader_.load("shaders/torus");
    rippleShader_.load("shaders/ripple");
    // ...
}
```

**確認事項**:
- [ ] `ofApp.h` に `ofShader starfieldShader_, torusShader_, rippleShader_;` が宣言されているか?
  - **検証方法**: `grep "ofShader" src/ofApp.h`
  - **期待結果**: 3つの ofShader メンバー変数が存在
  - **異常時**: 存在しない → メンバー変数の追加が必要

#### STEP 2-3: 描画関数でのシェーダー使用確認

**場所**: [src/ofApp.cpp:870-922](src/ofApp.cpp#L870-L922) (drawIdleScene)

**現在の実装**: CPU 描画 (ofDrawCircle, ofDrawRectangle)
**期待される実装**: GPU 描画 (ofShader::begin() → ofVbo::draw() → ofShader::end())

**確認事項**:
- [ ] `drawIdleScene()` で `starfieldShader_.begin()` が呼ばれているか?
  - **検証方法**: `grep "shader.*begin" src/ofApp.cpp`
  - **期待結果**: 各 draw*Scene() 関数で shader.begin()/end() が存在
  - **異常時**: 存在しない → シェーダー描画コードが未実装

**修正の優先度**:
- **P0**: シェーダーファイルの作成 (starfield.{vert,frag}, torus.{vert,frag}, ripple.frag)
- **P1**: ofApp.h への ofShader メンバー変数追加
- **P2**: setup() でのシェーダーロード処理
- **P3**: draw*Scene() での shader.begin()/end() 呼び出し

---

## 問題3: チャンネル分離が動作しているか不明

### 診断結果: **チャンネル分離は動作しているが、入力レベルが異常に小さい**

#### STEP 3-1: Calibration 結果の解釈

**ファイル**: `calibration copy/channel_separator.json`
```json
{
  "channels": [
    {"delaySamples": 136, "gain": 27.966468811035156, "phaseDeg": 25.42854881286621},
    {"delaySamples": 34, "gain": 3.981050968170166, "phaseDeg": 85.9465560913086}
  ]
}
```

**解釈**:
- **CH1 (participant1)**: gain=27.97x = +28.93dB のブーストが必要 → 入力が **3.5%** しかない
- **CH2 (participant2)**: gain=3.98x = +12.0dB のブーストが必要 → 入力が **25%** しかない
- **Delay差**: 136 - 34 = 102 samples (2.125ms @ 48kHz) → チャンネル分離は**動作している**

**結論**: チャンネル分離のアルゴリズムは正常。問題は**マイク入力レベルが低すぎる**こと。

#### STEP 3-2: Calibration の分離効果を確認

**場所**: [src/audio/AudioPipeline.cpp:94-97](src/audio/AudioPipeline.cpp#L94-L97)

```cpp
void AudioPipeline::applyCalibration(float& ch1, float& ch2) const {
    ch1 *= calibrationValues_[0].gain;  // ← 27.97倍
    ch2 *= calibrationValues_[1].gain;  // ← 3.98倍
}
```

**確認事項**:
- [ ] この関数が呼ばれているか?
  - **検証方法**: `audioIn()` (AudioPipeline.cpp:152) で `applyCalibration()` 呼び出しの前後で値をログ
  - **期待結果**: 呼び出し前 0.001-0.01、呼び出し後 0.03-0.28 (gain補正後)
  - **異常時**: 呼び出し後も 0.001-0.01 のまま → calibrationValues_ が {gain=1.0, gain=1.0} に初期化されている

**確認コード追加例**:
```cpp
applyCalibration(ch1, ch2);
if (frame == 0) {  // バッファの最初のフレームのみログ
    ofLogNotice("AudioPipeline") << "CH1 raw=" << input[0] << " calib=" << ch1
                                  << " (gain=" << calibrationValues_[0].gain << ")";
}
```

#### STEP 3-3: モノラル合成の確認

**場所**: [src/audio/AudioPipeline.cpp:153](src/audio/AudioPipeline.cpp#L153)

```cpp
monoBuffer_[frame] = 0.5f * (ch1 + ch2);  // ← 2ch を平均してモノラル化
```

**問題点**:
- MASTERDOCUMENT.md では「参加者ごとに別々の BeatTimeline」を要求 (MASTERDOCUMENT.md:143-145)
- 現在の実装では ch1+ch2 を合成 → **参加者ごとの BPM が分離できない**

**確認事項**:
- [ ] 2つの BeatTimeline インスタンスが存在するか?
  - **検証方法**: `grep "BeatTimeline" src/audio/AudioPipeline.h`
  - **期待結果**: `BeatTimeline beatTimeline1_, beatTimeline2_;` (2つ)
  - **現在の実装**: `BeatTimeline beatTimeline_;` (1つのみ) → **仕様違反**

**修正の優先度**:
- **P1**: 2ch → mono 合成を廃止し、ch1/ch2 を別々に BeatTimeline に渡す
- **P2**: `AudioPipeline::latestMetrics()` を participant1/2 別に返すように変更
- **P3**: Exchange/Mixed シーンで「相手の心拍を聴く」機能を実装

---

## 問題4: マイク入力レベルが異常に小さい (根本原因)

### 診断結果: ハードウェア設定の問題

#### STEP 4-1: macOS の入力ゲイン設定を確認

**確認方法**:
```bash
# システム環境設定 → サウンド → 入力 → Hollyland A1
# 入力音量スライダーが最小 (左端) になっていないか確認
```

**または** Audio MIDI 設定.app で確認:
```bash
open "/System/Applications/Utilities/Audio MIDI Setup.app"
# Hollyland A1 を選択 → 「デバイスを構成」→ マスター音量 / 入力ゲイン を確認
```

**期待値**: 入力ゲイン 70-90% (最大の手前)
**異常時**: 入力ゲイン 10-30% → スライダーを右に移動

#### STEP 4-2: Hollyland A1 のファームウェア設定を確認

**確認方法** (デバイス本体):
- Hollyland A1 の INPUT GAIN ノブ / スイッチを確認
- LINE / MIC 切り替えスイッチが MIC 側になっているか
- GAIN ノブが時計回りに 70% 以上回っているか

**期待値**: MIC モード、GAIN ノブ 70-90%
**異常時**: LINE モード or GAIN ノブが左端 → マイク入力が -40dB 以上減衰

#### STEP 4-3: ソフトウェア Emergency Gain の適用

**場所**: [src/audio/AudioPipeline.cpp:148-151](src/audio/AudioPipeline.cpp#L148-L151)

```cpp
if (inputGainLinear_ != 1.0f) {
    ch1 = std::clamp(ch1 * inputGainLinear_, -1.0f, 1.0f);
    ch2 = std::clamp(ch2 * inputGainLinear_, -1.0f, 1.0f);
}
```

**確認事項**:
- [ ] `inputGainLinear_` が 1.0 (0dB) に固定されていないか?
  - **検証方法**: GUI の Input Gain スライダーを +20dB に設定 → `inputGainLinear_` = 10.0 になるか確認
  - **期待結果**: `setInputGainDb(20.0f)` → `inputGainLinear_ = std::pow(10.0f, 20.0f/20.0f) = 10.0`
  - **異常時**: スライダーを動かしても `inputGainLinear_` が 1.0 のまま → `setInputGainDb()` が呼ばれていない

**緊急対応**:
```cpp
// AudioPipeline::setup() に追加
inputGainLinear_ = std::pow(10.0f, 20.0f / 20.0f);  // +20dB の固定ブースト
ofLogWarning("AudioPipeline") << "Emergency gain boost +20dB applied (inputGainLinear=" << inputGainLinear_ << ")";
```

---

## 修正の優先順位 (緊急度順)

| 優先度 | 問題 | 修正内容 | 検証方法 | 期待結果 |
|--------|------|----------|----------|----------|
| **P0** | マイク入力レベル (4-3) | Emergency gain +20dB を `AudioPipeline::setup()` に追加 | calibration 実行 → envelope 値を確認 | envelope 0.3-0.8 (正常範囲) |
| **P0** | Scene fade-out (1-4) | `scene_timing.json` の `textFadeOut.startAt` を 8.0 に修正 | Start scene でビジュアルが10秒間表示されるか | テキストが8秒間表示され、8-10秒で fade out |
| **P1** | Shader 不在 (2-1) | `bin/data/shaders/starfield.{vert,frag}` を作成 | `ls bin/data/shaders/` でファイル存在確認 | 6ファイル (starfield, torus, ripple × 2) |
| **P1** | Shader 未ロード (2-2) | `ofApp.h` に `ofShader` メンバー追加 + `setup()` でロード | アプリ起動時にシェーダーコンパイルエラーが出ないか | "Shader compiled successfully" ログ |
| **P2** | Shader 未使用 (2-3) | `draw*Scene()` で `shader.begin()/end()` 追加 | ビジュアルがパーティクルシステムで描画されるか | CPU 円描画 → GPU パーティクル描画に変化 |
| **P2** | Channel 分離 (3-3) | `BeatTimeline beatTimeline_` → `beatTimeline1_, beatTimeline2_` に分割 | proto_session.csv に bpm1/bpm2 列が追加されるか | 2つの BPM 値が独立に記録される |

---

## 即時実行可能な検証コマンド

### 検証1: ログファイルの最新状態を確認
```bash
cd /Users/ksk432/KNOTInBetweenUs
tail -20 "logs copy/proto_session.csv"  # envelope 値が 0.3 以上になっているか?
```

**期待結果**: `envelope` 列の値が 0.3-0.8
**異常時**: 0.01-0.10 → Emergency gain +20dB が必要

### 検証2: Scene timing config の内容確認
```bash
cat config/scene_timing.json | grep -A 5 "Start"
```

**期待結果**: `textFadeOut.startAt >= 8.0`
**異常時**: `startAt: 0.0` or `startAt: 1.0` → 即座に fade out している

### 検証3: Shader ファイルの存在確認
```bash
ls -lh bin/data/shaders/
```

**期待結果**: `starfield.vert`, `starfield.frag`, `torus.vert`, `torus.frag`, `ripple.frag` が存在
**異常時**: `No such file or directory` → シェーダーファイルが未作成

### 検証4: Calibration gain 値の確認
```bash
cat "calibration copy/channel_separator.json" | grep "gain"
```

**期待結果**: `gain: 1.0-3.0` (±3-10dB の範囲)
**異常時**: `gain: 27.97` or `gain: 3.98` → 入力レベルが異常に低い

---

## 次のアクション

### 今すぐ実行すべき修正 (所要時間: 10分)

1. **Emergency gain の適用** (P0):
   - `src/audio/AudioPipeline.cpp:13` (setup() 関数の末尾) に以下を追加:
     ```cpp
     inputGainLinear_ = std::pow(10.0f, 20.0f / 20.0f);  // +20dB emergency boost
     ofLogWarning("AudioPipeline") << "Emergency input gain: +20dB (linear=" << inputGainLinear_ << ")";
     ```
   - 再ビルド → calibration 実行 → envelope 値を確認

2. **Scene timing config の修正** (P0):
   - `config/scene_timing.json` を開く
   - Start scene の `textFadeOut` stage の `startAt` を確認
   - 0.0 または 1.0 になっている場合 → 8.0 に変更
   - アプリ再起動 → Start scene で10秒間ビジュアルが表示されるか確認

### 検証結果の報告フォーマット

修正後、以下の形式で結果を報告してください:

```
【P0-1: Emergency gain 適用】
- 修正内容: AudioPipeline.cpp:13 に +20dB boost 追加
- 検証方法: calibration 実行 → proto_session.csv の envelope 値を確認
- 結果: envelope=0.35-0.62 (正常範囲に回復) ✅
- 副作用: なし

【P0-2: Scene timing 修正】
- 修正内容: scene_timing.json の textFadeOut.startAt を 0.0→8.0 に変更
- 検証方法: Start scene で10秒間ビジュアルが表示されるか目視確認
- 結果: テキストが8秒間表示され、8-10秒で fade out ✅
- 副作用: なし
```

---

**作成者**: 実装リーダー
**最終更新**: 2025-10-29
**関連ドキュメント**: [CRITICAL_ISSUES_AND_FIXES.md](CRITICAL_ISSUES_AND_FIXES.md), [EMERGENCY_FIX_STARTBLOCK.md](EMERGENCY_FIX_STARTBLOCK.md)
