# 開発環境セットアップガイド

## 1. 前提条件
- macOS Sonoma 14.x
- Xcode 15.x + Command Line Tools
- Git, Python 3.10 以上

## 2. openFrameworks 0.12.0 の導入
1. [official site](https://openframeworks.cc/download/) から `of_v0.12.0_osx_release.zip` をダウンロード。  
   - 備考: `0.12.1` でもビルド検証済み。警告内容（OpenGL 非推奨、`sprintf`、未使用関数など）は既知のものとして扱い、将来的に Metal/`snprintf` への移行を検討する。
2. 推奨パスに展開（例: `~/Developer/of_v0.12.0_osx_release`）。以降、このパスを `$OF_ROOT` と記載。（ /Users/ksk432/Developer/of_v0.12.0_osx_release に配置）
3. `projectGenerator-osx/projectGenerator.app` を初回起動し、Gatekeeper 設定を解除。
4. Xcode 用テンプレートをインストール:
   ```bash
   cd "/Users/ksk432/Developer/of_v0.12.0_osx_release/scripts/osx"
   ./download_libs.sh
   ```

## 3. 必要アドオンの準備
```
cd "/Users/ksk432/Developer/addons"
git clone https://github.com/falcon4ever/ofxMaxim.git
git clone https://github.com/openframeworks/ofxGui.git
git clone https://github.com/paulvollmer/ofxCsv.git
```

- `ofxMaxim` は `libs/maximilian` 内に FFTW をバンドル。Xcode でビルドエラーが発生した場合は `Build Settings > Header Search Paths` に `$(OF_ROOT)/addons/ofxMaxim/src` を追加。
- `ofxCsv` はヘッダオンリー。`projectGenerator` で追加し、`src` フォルダにリンクされているか確認。

## 4. リポジトリ構造とフォルダ同期
```
KNOTInBetweenUs/
├── calibration/               # キャリブレーション成果
├── data/test_signals/         # テストトーン・心拍 WAV
├── logs/                      # セッションログ・集計
├── reports/                   # 役割別レポート
├── scripts/                   # 支援スクリプト
└── src/                       # (プロジェクト生成後に配置)
```

- `calibration/channel_separator.json` は実行時生成。Git には含めず、テンプレートで初期化。
- `logs/` 配下は `.gitignore` でランタイム成果物のみ除外。

## 5. テスト信号の生成
```
cd /path/to/KNOTInBetweenUs
python3 scripts/generate_test_signals.py
```

生成されるファイル:
- `data/test_signals/tone_1kHz_-12dBFS_5s.wav`
- `data/test_signals/rect_pulse_512samples_-18dBFS.wav`
- `data/test_signals/heartbeat_demo.wav`

いずれも 48kHz / 24-bit PCM (モノ) で書き出され、キャリブレーションや BeatTimeline の検証に利用可能。

## 6. プロジェクトの生成
1. `projectGenerator` を起動し、新規プロジェクトを `apps/myApps/knot_proto` として作成。
2. Addons に `ofxMaxim`, `ofxGui`, `ofxCsv` を追加。
3. 生成された `src` と `bin/data` を本リポジトリにコピー（またはシンボリックリンク）し、ビルド後に差分を `git status` で確認。

## 7. 開発時の推奨設定
- Audio input device: Hollyland A1。`ofSoundStreamSetup` で 48kHz / 2ch / buffer 512 を指定。
- `bin/data/config/` に JSON 設定を置き、App/Infra メンバーが管理。
- Xcode Scheme で `Run` → `Arguments Passed On Launch` に `--use-recording` を追加すると録音ファイルモードを切り替え可能 (実装予定)。
- Python ログ解析や KPI 集計は `reports/memberC/test_results/` 配下で管理し、週次レビューに提出。

## 8. 確認チェックリスト (Phase0)
- [ ] openFrameworks 0.12.0 がコンパイル可能。
- [ ] ofxMaxim/ofxGui/ofxCsv がプロジェクトに組み込まれている。
- [ ] `scripts/generate_test_signals.py` で WAV が作成できる。
- [ ] `calibration/`, `logs/`, `data/test_signals/` が repository root に存在。
- [ ] レポートフォーマット (`reports/member*/PROGRESS_REPORT.md`) を参照できる。

完了後は `reports/memberC/PROGRESS_REPORT.md` に進捗を記述し、Slack で共有する。
