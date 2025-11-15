# ログファイルの場所

## コンソールログ（ofLogNotice等）
- **場所**: 実行中のコンソールに表示
- **Xcodeから実行**: Xcodeのコンソールパネル（下部のデバッグエリア）
- **ターミナルから実行**: ターミナルウィンドウ
- **.appとして実行**: システムコンソール（Console.appで確認可能）

## CSV/JSONログファイル
- **ディレクトリ**: `/Users/ksk432/KNOTInBetweenUs/logs/`
- **ファイル一覧**:
  - `proto_session.csv` - セッションテレメトリ（BPM、エンベロープ、シーン状態）
  - `haptic_events.csv` - ハプティクスイベントログ
  - `scene_transitions.csv` - シーン遷移ログ
  - `calibration_report.csv` - キャリブレーション結果
  - `proto_summary.json` - セッションサマリー（JSON形式）

## ログファイルの確認方法

### コンソールログを確認
```bash
# Xcodeで実行している場合
# → Xcodeのコンソールパネルを確認

# ターミナルから実行している場合
# → ターミナルウィンドウに表示される

# .appとして実行している場合
# Console.appを開いて、アプリケーション名でフィルタ
```

### CSVログファイルを確認
```bash
cd /Users/ksk432/KNOTInBetweenUs/logs
ls -la

# 最新のログを確認
tail -f proto_session.csv
tail -f scene_transitions.csv
```

## デバッグログ（FirstPhase Audio Debug）
追加したデバッグログはコンソールに出力されます：
- `ofLogNotice("ofApp::audioOut")` で出力
- FirstPhase中に2秒ごとに自動出力
- Xcodeのコンソールパネルで確認可能

## ログの設定
設定ファイル: `bin/data/config/app_config.json`
- `telemetry.sessionCsv`: セッションログのパス
- `telemetry.hapticCsv`: ハプティクスログのパス
- `sceneTransitionCsv`: シーン遷移ログのパス（デフォルト: `../logs/scene_transitions.csv`）






