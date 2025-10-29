# Phase 2 柔軟性向上要件

**優先度**: 🟡 重大 (Phase 1 完了後に着手)  
**目的**: 実行中にルーティングと信号を調整可能にし、デモ運用・単独検証をスムーズに行えるようにする。

---

## 達成条件
- AudioRouter のルールを GUI から一覧・追加・削除・復帰できる。
- Synthetic 心拍を生成・制御し、1 人参加でも体験・テストが可能。
- ルーティングプリセットを JSON で保存／読込できる。
- 追加のデバッグ可視化／ログ機能で現場調整を効率化。
- Phase 1 の機能 (4ch 出力・ハプティクス・2ch ビジュアル) を回 regress しない。

### 前提条件
- Phase 1 の 12 ユニットが完了済みであり、AudioRouter が統合されている。
- シーン遷移・ログ・ハプティクス生成が既に動作している。

---

## 実装ユニット一覧
| Unit | 名称 | 主担当ファイル | 依存 | 所要時間 (目安) |
|------|------|----------------|------|------------------|
| 2.1 | AudioRouter 公開 API 拡張 | `src/audio/AudioRouter.{h,cpp}` | Phase 1 完了 | 1.0h |
| 2.2 | ルーティング GUI 基本パネル | `src/ofApp.{h,cpp}` | 2.1 | 2.0h |
| 2.3 | GUI からルール追加・編集 | `src/ofApp.cpp` | 2.2 | 1.5h |
| 2.4 | GUI からルール削除・プリセット復帰 | `src/ofApp.cpp` | 2.3 | 1.0h |
| 2.5 | Synthetic 心拍生成インフラ | `src/audio/AudioPipeline.{h,cpp}` | なし | 2.0h |
| 2.6 | Synthetic 心拍 GUI 制御 | `src/ofApp.{h,cpp}` | 2.5 | 1.5h |
| 2.7 | ルーティングプリセット保存／読込 | `src/audio/AudioRouter.cpp`, `bin/data/config/routing_presets.json` | 2.1 | 1.5h |
| 2.8 | デバッグ補助機能の追加 | `src/ofApp.cpp`, `src/infra/TelemetryLogging.*` | 2.1–2.6 | 1.5h |

---

## ユニット詳細

### Unit 2.1 — AudioRouter 公開 API 拡張
- **目的**: GUI からルーティングルールを編集できるよう、AudioRouter に完全な CRUD API を用意する。
- **対象ファイル**: `src/audio/AudioRouter.h`, `src/audio/AudioRouter.cpp`
- **依存関係**: Phase 1 完了
- **所要時間目安**: 1.0h
- **変更内容**:
  ```diff
  class AudioRouter {
  public:
      void setup(float sampleRateHz);
      void applyScenePreset(SceneState scene);
-     void setRoutingRule(OutputChannel channel, const RoutingRule& rule);
-     const RoutingRule& routingRule(OutputChannel channel) const;
-     void route(const std::array<float, 2>& headphoneInput,
-                const std::array<float, 2>& inputEnvelopes,
-                std::array<float, 4>& outputFrame);
+     void setRoutingRule(OutputChannel channel, const RoutingRule& rule);
+     const RoutingRule& routingRule(OutputChannel channel) const;
+     void route(const std::array<float, 2>& headphoneInput,
+                const std::array<float, 2>& inputEnvelopes,
+                std::array<float, 4>& outputFrame);
+     std::vector<RoutingRule> rules() const;
+     void setRule(OutputChannel channel, const RoutingRule& rule);
+     void clearAllRules();
+     void restorePreset(SceneState scene);
+     bool savePreset(const std::string& name, const std::filesystem::path& file) const;
+     bool loadPreset(const std::string& name, const std::filesystem::path& file);
  private:
      void clearRules();
  ```
  - `rules()` は `rules_` をコピーして返却。
  - `setRule()` は `setRoutingRule()` のラッパーとしてログ出力を加える。
  - `restorePreset()` はシーン識別子から再適用する糖衣メソッド (GUI から呼び出し)。
- **テスト方法**:
  - 単体テストまたは仮コードで `rules()`, `clearAllRules()`, `restorePreset()` を順に呼び、想定通りミュート→再設定されるか確認。
- **成功基準**:
  - GUI 実装で必要な情報とオペレーションが全て公開されている。
- **リスク / 備考**:
  - 既存コードで `clearRules()` を直接呼んでいた箇所があれば `clearAllRules()` に差し替える。

---

### Unit 2.2 — ルーティング GUI 基本パネル
- **目的**: ImGui 上で現在のルール一覧を可視化し、編集 UI の土台を作成。
- **対象ファイル**: `src/ofApp.h`, `src/ofApp.cpp`
- **依存関係**: Unit 2.1
- **所要時間目安**: 2.0h
- **変更内容**:
  - `ofApp.h` に GUI 状態を保持するメンバーを追加。
    ```diff
    bool showRoutingPanel_ = false;
    int selectedOutputCh_ = 0;
    RoutingRule stagedRule_{};
    std::array<char, 32> presetNameInput_{{0}};
    ```
  - `drawGui()` (または GUI 描画関数) に以下のパネルを追加。
    ```cpp
    if (showRoutingPanel_) {
        ImGui::Begin("Audio Routing");
        auto rules = audioRouter_.rules();
        for (std::size_t idx = 0; idx < rules.size(); ++idx) {
            ImGui::Text("CH%zu: %s / %s / %.1fdB",
                        idx + 1,
                        participantToString(rules[idx].source),
                        mixModeToString(rules[idx].mixMode),
                        rules[idx].gainDb);
            if (ImGui::Selectable("Edit", selectedOutputCh_ == static_cast<int>(idx))) {
                selectedOutputCh_ = static_cast<int>(idx);
                stagedRule_ = rules[idx];
            }
        }
        ImGui::End();
    }
    ```
  - `keyPressed` 等で `showRoutingPanel_` をトグルするショートカット (`'r'`) を追加。
- **テスト方法**:
  - アプリ起動 → `r` キーでパネル表示 → ルール一覧が現在のプリセットに対応しているか確認。
- **成功基準**:
  - 現行ルールが GUI 上で確認でき、選択状態が保持される。
- **リスク / 備考**:
  - ImGui 実装はデバッグビルドのみ表示したい場合、`#ifdef` などで制御。

---

### Unit 2.3 — GUI からルール追加・編集
- **目的**: GUI 上で参加者・モード・ゲインなどを編集し、AudioRouter に即時反映できるようにする。
- **対象ファイル**: `src/ofApp.cpp`
- **依存関係**: Unit 2.2
- **所要時間目安**: 1.5h
- **変更内容**:
  - ルール編集セクションにドロップダウンとスライダを追加。
    ```cpp
    ImGui::Separator();
    ImGui::Text("Edit Channel %d", selectedOutputCh_ + 1);
    ImGui::Combo("Source", reinterpret_cast<int*>(&stagedRule_.source), kParticipantLabels, 4);
    ImGui::Combo("Mode", reinterpret_cast<int*>(&stagedRule_.mixMode), kMixModeLabels, 4);
    ImGui::SliderFloat("Gain (dB)", &stagedRule_.gainDb, -60.0f, 6.0f, "%.1f");
    ImGui::SliderFloat("Pan", &stagedRule_.panLR, -1.0f, 1.0f, "%.2f");
    if (ImGui::Button("Apply")) {
        audioRouter_.setRule(static_cast<OutputChannel>(selectedOutputCh_), stagedRule_);
    }
    ```
  - 変更適用後にコンソールへログを出して検証しやすくする。
- **テスト方法**:
  - GUI でゲインを大きく操作 → オーディオ出力が追従するか確認。
  - ハプティクスモードに切り替え → CH3/CH4 が信号を生成するか確認。
- **成功基準**:
  - 設定変更がリアルタイムに反映され、audioOut で即時動作する。
- **リスク / 備考**:
  - 録音中に過大なゲイン設定を行うとハウリングを起こす恐れがあるため、デフォルト上限を 0dB に設定し、必要時のみ +6dB などに許可する。

---

### Unit 2.4 — GUI からルール削除・プリセット復帰
- **目的**: 個別ルールのクリアやシーンプリセットへの巻き戻しを GUI で行えるようにする。
- **対象ファイル**: `src/ofApp.cpp`
- **依存関係**: Unit 2.3
- **所要時間目安**: 1.0h
- **変更内容**:
  ```cpp
  if (ImGui::Button("Clear Channel")) {
      RoutingRule mute{};
      audioRouter_.setRule(static_cast<OutputChannel>(selectedOutputCh_), mute);
  }
  ImGui::SameLine();
  if (ImGui::Button("Restore Preset")) {
      audioRouter_.restorePreset(sceneController_.currentScene());
  }
  ```
  - プリセット復帰時には HUD へ通知を出し、現場で操作ログが確認できるようにする。
- **テスト方法**:
  - 任意のチャンネルを Silent にした後、「Restore Preset」で元に戻るか確認。
- **成功基準**:
  - ルールクリアで対象チャンネルがミュートされ、プリセット復帰で元の設定に戻る。
- **リスク / 備考**:
  - シーン遷移完了時に自動で applyScenePreset が走るため、GUI からの変更が上書きされる旨を UI 上に警告する（メッセージ表示など）。

---

### Unit 2.5 — Synthetic 心拍生成インフラ
- **目的**: AudioPipeline 側に擬似心拍を生成する仕組みを追加し、実入力がない場合でも beat イベントや envelope を供給する。
- **対象ファイル**: `src/audio/AudioPipeline.h`, `src/audio/AudioPipeline.cpp`
- **依存関係**: なし
- **所要時間目安**: 2.0h
- **変更内容**:
  - `AudioPipeline` に以下のメンバーを追加。
    ```diff
    bool syntheticEnabled_ = false;
    std::array<float, 2> syntheticBpm_{70.0f, 72.0f};
    std::array<float, 2> syntheticPhase_{0.0f, 0.5f};
    ```
  - `setSyntheticEnabled(bool)` / `setSyntheticBpm(ParticipantId, float)` を公開 API として追加。
  - `audioOut` と `audioIn` の先頭で `if (syntheticEnabled_)` の場合にノイズバッファではなく合成サイン波とエンベロープを生成し、`pendingEventsByChannel_` に擬似イベントを積む。
- **テスト方法**:
  - ユニットテストまたはログで `syntheticEnabled_` 時に BPM が設定値に固定されるか確認。
- **成功基準**:
  - 実入力がなくても `channelMetrics()` から BPM/Envelope が取得できる。
- **リスク / 備考**:
  - キャリブレーション中は synthetic を無効化するなど、状態遷移との整合性を確保。

---

### Unit 2.6 — Synthetic 心拍 GUI 制御
- **目的**: GUI から Synthetic 心拍の ON/OFF や BPM 調整を行い、1 人デモや現場検証を容易化。
- **対象ファイル**: `src/ofApp.h`, `src/ofApp.cpp`
- **依存関係**: Unit 2.5
- **所要時間目安**: 1.5h
- **変更内容**:
  - `ofParameter<bool> useSyntheticP1_`, `useSyntheticP2_` を追加し、GUI トグルと連動。
  - BPM スライダ (`ofParameter<float> syntheticBpmP1_`, `syntheticBpmP2_`) を GUI へ配置し、変更時に `audioPipeline_.setSyntheticBpm(...)` を呼ぶ。
  - Synthetic 有効時は GUI から `audioRouter_.restorePreset(SceneState::FirstPhase)` を呼び、自分自身の鼓動を再現できるように案内。
- **テスト方法**:
  - 各トグルを ON にして実入力がなくても鼓動音・ビジュアル・ハプティクスが動くか確認。
  - BPM を変化させた際の追従性を目視・耳・ログで確認。
- **成功基準**:
  - 片側のみ Synthetic、もう片側実入力といった混合モードでも破綻しない。
- **リスク / 備考**:
  - Synthetic ON 時のログ出力を行い、誤って本番で有効化されたままにならないよう注意喚起。

---

### Unit 2.7 — ルーティングプリセット 保存／読込
- **目的**: 編集したルーティングを JSON として保管し、 GUI からロード可能にする。
- **対象ファイル**: `src/audio/AudioRouter.cpp`, `bin/data/config/routing_presets.json`
- **依存関係**: Unit 2.1
- **所要時間目安**: 1.5h
- **変更内容**:
  - `AudioRouter::savePreset` / `loadPreset` を実装。フォーマット例:
    ```json
    {
      "presets": {
        "exchange": [
          { "channel": 0, "source": "P2", "mode": "Partner", "gainDb": 0.0, "pan": -1.0 },
          { "channel": 1, "source": "P1", "mode": "Partner", "gainDb": 0.0, "pan":  1.0 }
        ]
      }
    }
    ```
  - GUI から `ImGui::InputText("Preset", presetNameInput_.data(), presetNameInput_.size())` と保存／読込ボタンを追加。
  - デフォルトのプリセットファイル `bin/data/config/routing_presets.json` を作成し、FirstPhase / Exchange / Mixed を格納。
- **テスト方法**:
  - GUI で編集 → 保存 → ファイルの JSON を確認 → アプリ再起動 → 読込で同じ状態になるか検証。
- **成功基準**:
  - 保存・読込でルールが失われず、ファイル破損時はエラーメッセージを表示。
- **リスク / 備考**:
  - JSON 書き込み時の競合を避けるため、`std::filesystem::rename` を使った一時ファイル書き換えを推奨。

---

### Unit 2.8 — デバッグ補助機能の追加
- **目的**: 現場調整を迅速化するため、ルーティング／ハプティクス／Synthetic 状態をリアルタイムに追跡するツールを追加。
- **対象ファイル**: `src/ofApp.cpp`, `src/infra/TelemetryLogging.h/cpp`
- **依存関係**: Unit 2.1〜2.6
- **所要時間目安**: 1.5h
- **変更内容**:
  - GUI パネルに「現在の出力レベル」「ハプティクス振幅」「Synthetic ON/OFF」を表示するウィジェットを追加。
  - Telemetry ログへ `routingPreset`, `syntheticEnabledP1`, `syntheticEnabledP2` を追加フィールドとして書き出す。
  - `ofApp::keyPressed` にデバッグショートカット (例: `Shift + R` でルールリロード、`Shift + H` でハプティクスモニタ表示) を追加。
- **テスト方法**:
  - Telemetry CSV/JSON を確認し、新フィールドが出力されているか検証。
  - GUI の各指標がリアルタイムに更新されるか確認。
- **成功基準**:
  - 現場でルーティング状態を即座に把握でき、ログから後追い分析が可能。
- **リスク / 備考**:
  - デバッグ表示は本番モードで非表示にできるよう、構成ファイルまたはコマンドラインで切り替え可能にしておく。

---

## Phase 2 完了後の効果
- GUI からルーティングと Synthetic 信号を即時調整でき、現場での段取り替えや 1 人デモが可能になる。
- プリセット保存／読込により、事前リハーサルで調整した設定を本番で素早く再現できる。
- デバッグ情報が充実し、トラブル発生時の原因切り分けが迅速化する。
- Phase 3 以降 (高度なビジュアル連携・複数プリセット切替など) への拡張が容易になる。
