# 実装前提事項の明確化 (2025-10-29)

**作成者**: プロジェクト統括責任者
**対象**: Phase 1 (P0) 実装着手前の前提条件確定
**承認**: 本ドキュメントをチーム全体でレビュー後、実装開始

---

## 1. シーン持続時間パラメータと遷移条件

### 設計方針
**外部設定ファイルで管理** (app_config.json に統合せず、専用ファイル `config/scene_timing.json` を作成)

### 理由
- 展示現場でのチューニング (参加者の反応を見ながら滞在時間調整) を容易にする
- テストシナリオで異なるタイミングを使い分ける (短縮版: 各シーン5s、本番版: 規定時間)
- app_config.json がオーディオ・テレメトリ設定で肥大化するのを防ぐ

### ファイル構造: `config/scene_timing.json`

```json
{
  "version": "1.0",
  "description": "Scene duration and transition conditions",
  "scenes": {
    "Idle": {
      "autoDuration": null,
      "comment": "Manual transition only. No auto-advance."
    },
    "Start": {
      "autoDuration": 30.0,
      "stages": [
        { "name": "textFadeIn", "startAt": 0.0, "duration": 1.0 },
        { "name": "textDisplay", "startAt": 1.0, "duration": 9.0 },
        { "name": "textFadeOut", "startAt": 10.0, "duration": 1.0 },
        { "name": "breathingGuide", "startAt": 12.0, "duration": 8.0 },
        { "name": "bellSound", "startAt": 25.0, "duration": 2.0 }
      ],
      "transitionTo": "FirstPhase"
    },
    "FirstPhase": {
      "autoDuration": 60.0,
      "transitionCondition": {
        "type": "timeout",
        "comment": "Advance to Exchange after 60s"
      },
      "transitionTo": "Exchange"
    },
    "Exchange": {
      "autoDuration": 60.0,
      "transitionCondition": {
        "type": "timeout",
        "comment": "Advance to Mixed after 60s"
      },
      "transitionTo": "Mixed"
    },
    "Mixed": {
      "autoDuration": 90.0,
      "transitionCondition": {
        "type": "timeout",
        "comment": "Advance to End after 90s"
      },
      "transitionTo": "End"
    },
    "End": {
      "autoDuration": 20.0,
      "stages": [
        { "name": "fadeOut", "startAt": 0.0, "duration": 10.0 },
        { "name": "ambient", "startAt": 10.0, "duration": 5.0 },
        { "name": "silence", "startAt": 15.0, "duration": 5.0 }
      ],
      "transitionTo": "Idle",
      "idleReturnDelay": 15.0
    }
  },
  "testMode": {
    "enabled": false,
    "comment": "When enabled, all autoDuration values are divided by 6 (e.g., 60s → 10s)",
    "scaleFactor": 0.1667
  }
}
```

### 実装クラス: `SceneTimingConfig`

```cpp
// src/SceneTimingConfig.h
struct SceneTimingConfig {
    struct Stage {
        std::string name;
        double startAt;
        double duration;
    };

    struct SceneConfig {
        std::optional<double> autoDuration;
        std::vector<Stage> stages;
        std::string transitionTo;
        std::optional<double> idleReturnDelay;
    };

    std::map<SceneState, SceneConfig> scenes;
    bool testMode = false;
    double testScaleFactor = 0.1667;

    static SceneTimingConfig load(const std::filesystem::path& path);
    double getEffectiveDuration(SceneState state) const;
};
```

### 測定ログの粒度

**ログファイル**: `logs/scene_transitions.csv`

**カラム構成**:
```csv
timestampMicros,sceneFrom,sceneTo,transitionType,triggerReason,timeInStateSec,expectedDurationSec,deviation
```

**記録タイミング**:
- シーン遷移開始時 (requestState 成功時)
- シーン遷移完了時 (blend=1.0 到達時)
- 自動遷移発火時 (autoDuration 到達時)

**例**:
```csv
1698765432100000,Idle,Start,manual,button_press,12.5,null,null
1698765462100000,Start,FirstPhase,auto,timeout,30.02,30.0,0.02
1698765522150000,FirstPhase,Exchange,auto,timeout,60.05,60.0,0.05
```

---

## 2. アセット準備状況と配置方針

### 現状: 未配置アセット一覧

| アセット種別 | ファイル | 配置先 | ステータス | 対応方針 |
|------------|---------|--------|----------|----------|
| **フォント** | Noto Sans JP (Thin 120pt) | `bin/data/fonts/NotoSansJP-Thin.otf` | ❌ 未配置 | ダミー作成 → 後で差し替え |
| **フォント** | Noto Sans JP (Regular 48pt) | `bin/data/fonts/NotoSansJP-Regular.otf` | ❌ 未配置 | 同上 |
| **サウンド** | bell.wav | `bin/data/audio/bell.wav` | ❌ 未配置 | ダミー作成 (1kHz sine 2s) |
| **サウンド** | white_noise_base.wav | `bin/data/audio/white_noise_base.wav` | ❌ 未配置 | 自動生成スクリプト追加 |
| **HRTF** | MIT KEMAR SOFA | `bin/data/hrir/mit_kemar_normal_pinna.sofa` | ❌ 未配置 | ダウンロードスクリプト追加 |
| **GLSL** | starfield.vert / .frag | `bin/data/shaders/starfield.{vert,frag}` | ❌ 未配置 | プレースホルダ作成 |
| **GLSL** | torus.vert / .frag | `bin/data/shaders/torus.{vert,frag}` | ❌ 未配置 | 同上 |
| **GLSL** | ripple.frag | `bin/data/shaders/ripple.frag` | ❌ 未配置 | 同上 |

### アセット配置の指示

#### フォント
- **ダミー運用**: 開発初期は **Helvetica / Arial** で代替
- **実装**: `ofApp::setup()` でフォント読込失敗時のフォールバック処理を実装
  ```cpp
  if (!displayFont_.load("fonts/NotoSansJP-Thin.otf", 120)) {
      ofLogWarning("ofApp") << "Noto Sans JP not found. Using system font.";
      displayFont_.load(OF_TTF_SANS, 120);  // openFrameworks デフォルト
  }
  ```
- **本番配置**: PM が Noto Sans JP を Google Fonts からダウンロードし、`bin/data/fonts/` に配置
  - ライセンス: SIL Open Font License (商用利用可)
  - ダウンロード URL: https://fonts.google.com/noto/specimen/Noto+Sans+JP

#### サウンドアセット
- **bell.wav**:
  - **ダミー**: `scripts/generate_bell.py` で 1kHz sine wave (2s, -12dBFS) を生成
  - **本番**: PM が実際の鐘の音を録音 or フリー素材を配置
  - **仕様**: 48kHz / 24bit / モノラル / WAV形式
- **white_noise_base.wav**:
  - **自動生成**: `scripts/generate_noise.sh` で 10s のホワイトノイズを生成 (ofSoundPlayer で loop 再生)
  - **不要**: AudioPipeline でリアルタイム生成する場合はファイル不要 (推奨)

#### HRTF / SOFA データ
- **配置先**: `bin/data/hrir/mit_kemar_normal_pinna.sofa`
- **ダウンロードスクリプト**: `scripts/download_hrtf.sh`
  ```bash
  #!/bin/bash
  HRTF_URL="https://sofacoustics.org/data/database/mit/mit_kemar_normal_pinna.sofa"
  DEST="bin/data/hrir/mit_kemar_normal_pinna.sofa"
  mkdir -p "$(dirname "$DEST")"
  curl -L -o "$DEST" "$HRTF_URL"
  echo "HRTF data downloaded to $DEST (size: $(du -h "$DEST" | cut -f1))"
  ```
- **Git LFS 運用**:
  - ファイルサイズが 10MB 以上の場合は `.gitattributes` に追加
  ```
  *.sofa filter=lfs diff=lfs merge=lfs -text
  ```
- **フォールバック**: SOFA ファイルがない場合は Binaural 処理をスキップし、ステレオミックスで動作

#### GLSL シェーダ
- **プレースホルダ**: 最小限の動作確認用シェーダを作成
  - `starfield.frag`: 画面全体を単色 (青) で塗りつぶす
  - `torus.frag`: 画面中央に円を描画
  - `ripple.frag`: 波紋エフェクト (distance field ベース)
- **本番実装**: Phase 1 で詳細なシェーダを実装
- **配置**: `bin/data/shaders/` 配下
- **読込エラー処理**: シェーダ読込失敗時は CPU フォールバック (ofDrawCircle 等) で描画

---

## 3. 自動遷移の検証方法とログ形式

### ログ出力先

| ログ種別 | ファイルパス | 形式 | 用途 |
|---------|-------------|------|------|
| **シーン遷移** | `logs/scene_transitions.csv` | CSV | 遷移タイミング・遅延計測 |
| **セッション全体** | `logs/proto_session.csv` | CSV | BPM/envelope/sceneId (既存) |
| **ハプティクス** | `logs/haptic_events.csv` | CSV | 触覚イベント (既存) |
| **サマリー** | `logs/proto_summary.json` | JSON | 集計統計 (既存) |

### 新規: `logs/scene_transitions.csv`

**カラム**:
- `timestampMicros`: エポックマイクロ秒
- `sceneFrom`: 遷移元シーン (enum 文字列)
- `sceneTo`: 遷移先シーン
- `transitionType`: `manual` | `auto` | `error`
- `triggerReason`: `button_press` | `timeout` | `hrv_threshold` | `system_error`
- `timeInStateSec`: 遷移元シーンでの滞在時間
- `expectedDurationSec`: 設定ファイルの `autoDuration` 値 (null if manual)
- `deviationSec`: `timeInStateSec - expectedDurationSec` (自動遷移の精度)
- `blendDurationSec`: フェード時間 (SceneController の `fadeDuration_`)

**実装**: `SceneTransitionLogger` クラス

```cpp
// src/infra/SceneTransitionLogger.h
class SceneTransitionLogger {
public:
    struct TransitionRecord {
        uint64_t timestampMicros;
        SceneState sceneFrom;
        SceneState sceneTo;
        std::string transitionType;  // manual, auto, error
        std::string triggerReason;
        double timeInStateSec;
        std::optional<double> expectedDurationSec;
        std::optional<double> deviationSec;
        double blendDurationSec;
    };

    void setup(const std::filesystem::path& csvPath);
    void recordTransition(const TransitionRecord& record);
    void flush();

private:
    std::ofstream csvStream_;
    std::vector<TransitionRecord> buffer_;
};
```

### 展示オペレーション用ダッシュボード

**Phase 1 では不要** (GUI の statusPanel_ で十分)

**Phase 2 以降の拡張案** (optional):
- Grafana + Prometheus でリアルタイムモニタリング
- CSV を定期的にパースし、Prometheus exporter で公開
- ダッシュボードで表示:
  - 現在のシーン・滞在時間
  - BPM/HRV のリアルタイムグラフ
  - 過去1時間のセッション数・平均滞在時間

---

## 4. GUI 非表示モード仕様

### 設定方法: `app_config.json` に追加

```json
{
  "operationMode": "exhibition",
  "gui": {
    "showControlPanel": false,
    "showStatusPanel": false,
    "allowKeyboardToggle": false,
    "keyboardToggleKey": "g",
    "keyboardToggleHoldTime": 3.0
  }
}
```

### モード定義

| モード | controlPanel | statusPanel | キーボードショートカット | 用途 |
|--------|-------------|-------------|----------------------|------|
| **exhibition** | 非表示 | 非表示 | 無効 (holdTime=0) | 本番展示 |
| **debug** | 表示 | 表示 | 有効 (g キー長押し3s) | 開発・デバッグ |
| **operator** | 非表示 | 表示のみ | 有効 | 展示スタッフ用 (状態監視のみ) |

### キーボードロック運用ルール

#### 展示環境
- **物理的対策**: キーボードを Mac 背面に隠蔽、または USB キーボードを接続せずタッチスクリーンのみ
- **ソフトウェア対策**: `operationMode=exhibition` 時はキーボード入力を完全無視
  ```cpp
  void ofApp::keyPressed(int key) {
      if (appConfig_.operationMode == "exhibition") {
          return;  // Ignore all keyboard input
      }
      // ... existing logic
  }
  ```

#### 緊急時のGUI表示
- **隠しコマンド**: タッチスクリーン4隅を同時タップ (5秒以内) → GUI 一時表示
- **実装**: `ofApp::mousePressed()` で4点同時タッチを検出
  ```cpp
  std::vector<glm::vec2> touchPoints_;
  if (touchPoints_.size() == 4 && checkCorners(touchPoints_)) {
      temporarilyShowGui_ = true;
  }
  ```

---

## 5. オートロック条件 (操作禁止条件)

### 実装: `ofApp::isInteractionLocked()`

```cpp
bool ofApp::isInteractionLocked() const {
    // 既存: キャリブレーション中
    if (audioPipeline_.isCalibrationActive()) return true;
    if (audioPipeline_.isEnvelopeCalibrationActive()) return true;
    if (envelopeCalibrationRunning_) return true;

    // 新規追加: シーン遷移中
    if (sceneController_.isTransitioning()) return true;

    // 新規追加: Start シーンのステージ実行中 (テキスト表示中は遷移禁止)
    if (sceneController_.currentState() == SceneState::Start) {
        const double timeInState = sceneController_.timeInState(ofGetElapsedTimef());
        if (timeInState < 11.0) {  // textFadeIn + textDisplay
            return true;
        }
    }

    // 新規追加: End シーンのフェードアウト中
    if (sceneController_.currentState() == SceneState::End) {
        const double timeInState = sceneController_.timeInState(ofGetElapsedTimef());
        if (timeInState < 10.0) {  // fadeOut 中
            return true;
        }
    }

    return false;
}
```

### ロック条件一覧

| 条件 | 理由 | GUI表示 | ロック期間 |
|------|------|---------|----------|
| **キャリブレーション中** | 音声入力の正確な測定のため | "キャリブレーション中です..." | ~5秒 |
| **エンベロープキャリブ中** | 包絡ベースライン測定のため | "包絡キャリブレーション中..." | ~3秒 |
| **シーン遷移中** | フェード演出の中断防止 | "遷移中 (XX%)" | ~1.2秒 |
| **Start シーン テキスト表示中** | 参加者へのメッセージ表示完了待ち | (表示なし) | ~11秒 |
| **End シーン フェードアウト中** | 体験の余韻を保つため | (表示なし) | ~10秒 |

### 手動遷移の可否

| 現在シーン | 遷移先 | 許可 | 条件 |
|----------|-------|------|------|
| Idle | Start | ✅ | ロック中でなければ常に可能 |
| Start | FirstPhase | ❌ | 自動遷移のみ (30s 経過後) |
| FirstPhase | Exchange | ✅ | デバッグ用に許可 (本番は自動) |
| FirstPhase | End | ✅ | 緊急中断用 |
| Exchange | Mixed | ✅ | デバッグ用に許可 (本番は自動) |
| Mixed | End | ✅ | 同上 |
| End | Idle | ❌ | 自動遷移のみ (15s 経過後) |

**実装**: `SceneController::canTransition()` を拡張

```cpp
bool SceneController::canTransition(SceneState from, SceneState to, bool manualRequest) const {
    // Auto-transition always allowed
    if (!manualRequest) {
        return true;
    }

    // Manual transition rules
    switch (from) {
        case SceneState::Idle:
            return to == SceneState::Start;
        case SceneState::Start:
            return false;  // Auto-only
        case SceneState::FirstPhase:
            return to == SceneState::Exchange || to == SceneState::End;
        case SceneState::Exchange:
            return to == SceneState::Mixed || to == SceneState::End;
        case SceneState::Mixed:
            return to == SceneState::End;
        case SceneState::End:
            return false;  // Auto-only
    }
    return false;
}
```

---

## 6. 次のステップ: 実装順序

### ✅ 即座に着手可能な作業 (本日~明日)

1. **アセット準備スクリプト作成** (MemberC: 2h):
   ```bash
   scripts/setup_assets.sh
   scripts/generate_bell.py
   scripts/download_hrtf.sh
   ```
   - 実行後、`bin/data/` 配下にダミーアセットが配置される
   - README に「初回セットアップ時に実行」と記載

2. **scene_timing.json 作成** (MemberC: 1h):
   - 上記の JSON を `config/scene_timing.json` に配置
   - `SceneTimingConfig` クラスのスケルトンを作成

3. **SceneTransitionLogger 実装** (MemberC: 2h):
   - CSV ヘッダー書き込み
   - `recordTransition()` の実装
   - `ofApp::update()` に統合

4. **GUI 非表示モード実装** (MemberB: 2h):
   - `app_config.json` に `operationMode`, `gui` セクション追加
   - `ofApp::setup()` で `operationMode` を読込
   - `controlPanel_.draw()`, `statusPanel_.draw()` を条件分岐

### 📋 並行作業 (明日~)

5. **SceneState 拡張** (MemberB: 4h):
   - `SceneState` enum に `Start/Exchange/Mixed` 追加
   - `SceneController::canTransition()` 拡張 (手動/自動判定)
   - `SceneController::update()` にタイマーベース自動遷移追加

6. **SceneTimingConfig 統合** (MemberB: 4h):
   - `SceneController` に `SceneTimingConfig` を読込
   - `autoDuration` に基づく自動遷移トリガー
   - `SceneTransitionLogger` に記録

7. **Start/Exchange/Mixed プレースホルダ描画** (MemberB: 6h):
   - `drawStartScene()`: テキスト + フェードイン/アウト
   - `drawExchangeScene()`: 2つの円リング (CPU描画)
   - `drawMixedScene()`: 共有ノード (CPU描画)

8. **フォント統合** (MemberB: 2h):
   - `ofTrueTypeFont` 読込 + フォールバック処理
   - `ofDrawBitmapString()` → `displayFont_.drawString()` 置換

### 🔍 検証作業 (並行)

9. **シーン遷移ログの検証** (MemberC: 2h):
   - 手動遷移・自動遷移の両方でログ記録を確認
   - `deviation` が ±100ms 以内に収まるか計測

10. **ロック条件のテスト** (全員: 1h):
    - 各ロック条件でボタンが無効化されることを確認
    - GUI に「操作がロックされています (理由)」を表示

---

## 7. アセット準備スクリプト (即実装)

### `scripts/setup_assets.sh`

```bash
#!/bin/bash
set -e

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DATA_DIR="$PROJECT_ROOT/bin/data"

echo "=== KNOT Asset Setup ==="

# Create directories
mkdir -p "$DATA_DIR/fonts"
mkdir -p "$DATA_DIR/audio"
mkdir -p "$DATA_DIR/hrir"
mkdir -p "$DATA_DIR/shaders"
mkdir -p "$DATA_DIR/config"

# Download HRTF (if not exists)
HRTF_FILE="$DATA_DIR/hrir/mit_kemar_normal_pinna.sofa"
if [ ! -f "$HRTF_FILE" ]; then
    echo "Downloading MIT KEMAR HRTF..."
    curl -L -o "$HRTF_FILE" "https://sofacoustics.org/data/database/mit/mit_kemar_normal_pinna.sofa" || {
        echo "HRTF download failed. Skipping (binaural will be disabled)."
    }
else
    echo "HRTF already exists: $HRTF_FILE"
fi

# Generate bell.wav (dummy)
echo "Generating dummy bell.wav..."
python3 "$PROJECT_ROOT/scripts/generate_bell.py" "$DATA_DIR/audio/bell.wav"

# Copy scene_timing.json
cp "$PROJECT_ROOT/config/scene_timing.json" "$DATA_DIR/config/scene_timing.json"

echo "✅ Asset setup complete!"
echo "⚠️  Please replace dummy assets with production files:"
echo "   - bin/data/fonts/NotoSansJP-*.otf"
echo "   - bin/data/audio/bell.wav (real bell sound)"
```

### `scripts/generate_bell.py`

```python
#!/usr/bin/env python3
import numpy as np
import wave
import sys

def generate_bell(output_path, duration=2.0, sample_rate=48000):
    """Generate a simple 1kHz sine wave as placeholder bell sound."""
    t = np.linspace(0, duration, int(sample_rate * duration), False)
    # 1kHz sine with exponential decay
    signal = np.sin(2 * np.pi * 1000 * t) * np.exp(-t * 2)
    # Scale to -12dBFS (amplitude ~0.25)
    signal *= 0.25
    # Convert to 16-bit PCM
    signal_int = (signal * 32767).astype(np.int16)

    with wave.open(output_path, 'w') as wav:
        wav.setnchannels(1)  # Mono
        wav.setsampwidth(2)  # 16-bit
        wav.setframerate(sample_rate)
        wav.writeframes(signal_int.tobytes())

    print(f"✅ Generated dummy bell: {output_path}")

if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("Usage: generate_bell.py <output.wav>")
        sys.exit(1)
    generate_bell(sys.argv[1])
```

---

## 8. 承認と次のアクション

### チームレビュー必須項目

- [ ] シーン持続時間パラメータ (scene_timing.json) の構造に同意
- [ ] アセット配置方針 (ダミー → 本番差し替え) に同意
- [ ] ログ形式 (scene_transitions.csv) に同意
- [ ] GUI 非表示モード仕様 (operationMode) に同意
- [ ] オートロック条件に同意

### 実装開始条件

上記5項目に全員が同意した時点で、**即座に実装開始**。

**目標**: 今週末 (2025-11-01) までに:
- SceneState 拡張完了
- scene_timing.json ベースの自動遷移動作
- Start/Exchange/Mixed プレースホルダ表示
- scene_transitions.csv にログ記録

---

**作成者**: プロジェクト統括責任者
**レビュー期限**: 2025-10-29 18:00 (本日中)
**承認後の開始**: 即時
