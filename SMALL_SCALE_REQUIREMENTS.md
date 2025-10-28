# 小規模実装向け要件定義・労働計画

## 1. 目的
- MASTERDOCUMENT.md の全体仕様を満たす前段階として、心拍信号の取得〜多感覚出力パイプラインを小規模に構築・検証する。
- コア機能の技術的成立性（キャリブレーション、BeatTimeline 生成、音響・視覚・触覚同期）を確認し、本開発でのリスク低減と工数見積り精度を高める。

## 2. スコープ
- 対象機能: 
  - Wireless マイク 2ch からの入力取得と簡易キャリブレーションの JSON 保存。
  - BeatTimeline（BPM/Envelope）の生成と GUI プロト表示。
  - シンプルなステレオ再生（自己心音のみ）＋ホワイトノイズのミックス検証。
  - ハプティクス装置（疑似）へのトリガー出力ログ化。
  - Idle→First Phase→End の 3 シーンのみを実装し遷移する。
- 非対象: Exchange/Mixed シーン、FDN リバーブや Binaural HRTF などの高度音場処理、星空シェーダなどの高負荷ビジュアル。
- 実行環境: macOS + openFrameworks 0.12.0、2 名同時の実機検証ではなく録音データでも可。

## 3. 成果物
- プロトタイプアプリケーション（リポジトリブランチ）。
- `calibration/channel_separator.json` の小規模版サンプル。
- `logs/proto_session.csv` と `logs/proto_summary.json`。
- 検証レポート（キャリブレーション精度・BPM 精度・シーン遷移ログ）。
- 次フェーズ向け改善案メモ。

## 4. 機能要件
1. **キャリブレーション**
   - 5 秒の交互トーンを再生し、ゲイン差と遅延を推定して JSON に保存する。
   - 閾値: gain 補正 ±3dB 以内、遅延補正 ±2 サンプル以内を報告（精度不足の際はメモ）。
2. **BeatTimeline 生成**
   - BPF(20–150Hz)→整流→包絡→しきい値検出を実装し、BPM と EnvelopePeak を 250ms 毎に更新。
   - GUI プロト（ofxGui など）で BPM 数値と Envelope グラフを表示。
3. **音響出力**
   - 自心音波形（モノ）を -15dBFS でステレオ複製し、ホワイトノイズ -24dBFS をミックス。
   - 出力ピーク -3dBFS を超えた場合に limiter（簡易）で抑制。
4. **シーン遷移**
   - Idle(静止画＋入力監視) → First Phase(心音再生) → End(フェードアウト) を GUI ボタンで制御。
5. **ハプティクス**
   - BeatEvent 発生時に `HapticEvent` をログし、疑似デバイスへバッファ送出（実機未接続で可）。
6. **ロギング**
   - session CSV に timestamp/BPM/envelope/scene を 250ms 間隔で追記。
   - summary JSON に平均 BPM, SDNN を記録。

## 5. 非機能要件
- フレームレート 30fps 以上を維持。
- オーディオ処理は audioIn/audioOut の遅延が 25ms 以内となるようリングバッファで連携。
- 例外発生時は GUI にメッセージを表示し、ログにも書き込む。
- コーディング規約: MASTERDOCUMENT.md の RULES FOR CODING を準拠（RAII, const 安全, lock-free 優先）。

## 6. 体制・役割 (最大3名)
| 役割 | 主担当 | 主なタスク |
| --- | --- | --- |
| DSP/Audio | メンバーA | キャリブレーション、BeatTimeline、オーディオ出力、Limiting |
| Vis/UX | メンバーB | GUI プロト、シーン遷移演出、ハプティクスログ可視化 |
| App/Infra | メンバーC | ログ/設定管理、ファイル IO、テストスクリプト整備、ビルド設定 |

※ 2 名以下の場合は DSP/Audio と App/Infra を兼務し、GUI を簡素化する。

## 7. タスク分解と見積り
| WBS | タスク | 担当 | 期間目安 |
| --- | --- | --- | --- |
| 1 | 開発環境セットアップ (oF, アドオン確認) | C | 0.5 日 |
| 2 | キャリブレーション実装・JSON 出力 | A | 1.5 日 |
| 3 | BeatTimeline + BPM 可視化 | A/B | 2 日 |
| 4 | 自心音 + ノイズミックス & limiter | A | 1 日 |
| 5 | シーン遷移 GUI と状態管理 | B/C | 1.5 日 |
| 6 | ハプティクスイベントログ & 疑似出力 | B | 0.5 日 |
| 7 | ログ収集・summary JSON 出力 | C | 1 日 |
| 8 | 小規模結合テスト & レポート作成 | 全員 | 1 日 |
| 9 | バッファ | 全員 | 0.5 日 |

合計工数 ≒ 9 日 (3名並行時は約 4 営業日)。

## 8. 検証計画
- **キャリブレーション**: テストトーン録音を解析し補正量を算出、補正前後のクロストーク差 (dB) を記録。
- **BPM 精度**: 録音済み心拍（基準 BPM 付）との比較で平均誤差 ±3 BPM 以内を確認。
- **音量管理**: 出力 LUFS とピークをメーターで測定し、-3dBFS 超過がないか確認。
- **シーン遷移**: GUI 操作で Idle→First→End が停止なく遷移し、ログに SceneID が正しく記録されること。
- **ハプティクスログ**: BeatEvent→HapticEvent の遅延をタイムスタンプで算出 (目標 <40ms)。

## 9. リスクと対策
- ハードウェア未接続時の動作: 録音ファイル再生モードを用意し、入力がゼロでも UI が落ちないようガード。
- キャリブレーション精度不足: 手動パラメータ編集手順を文書化し、オペレータが JSON を微調整できるようにする。
- 工数超過: 音響演出や GUI 演出を簡易化し、ログ基盤と BeatTimeline の完成を最優先。
- 技術的課題が発覚した場合は改善提案メモに記録し、本開発計画見直しに反映する。

## 10. スケジュール案 (2 週間スプリント)
| 週 | 目標 | 主な成果 |
| --- | --- | --- |
| Week 1 | キャリブレーション + BeatTimeline 動作 | JSON 出力、BPM 可視化、ログ基盤 |
| Week 2 | シーン遷移 + 音響出力 + ハプティクスログ | Idle/First/End 動作、レポート提出 |

各週末に 30 分のレビューを設け、成果と次週タスク・リスクを更新する。

## 11. エンジニアリング工程フロー

### Phase 0: 事前調査とセットアップ (Day 0.5)
- openFrameworks 0.12.0 と必要アドオン (ofxMaxim, ofxGui, ofxCsv) を導入し、ビルド検証。
- Hollyland A1 無線受信のドライバと ofSoundStream デバイス列挙を確認。録音ファイル再生モードを切り替える CLI/GUI フラグを準備。
- 記録フォルダ (`calibration/`, `logs/`, `data/test_signals/`) を作成し、サンプルデータ (テストトーン, 心拍 WAV) を配置。

### Phase 1: キャリブレーション基盤 (Day 1-2)
1. テストトーン再生モジュールを実装。1kHz サイン, 512 サンプルパルスを交互に送出。
2. audioIn() からの 2ch サンプルをリングバッファに蓄積し、短時間 FFT / 相互相関でゲイン・遅延を推定。
3. 推定値を `CalibrationResult` 構造体で管理し、`calibration/channel_separator.json` にシリアライズ。
4. 結果を GUI (ofxGui) で可視化し、gain/phase/delay の手動調整スライダーと再測定ボタンを提供。
5. 再測定手順を README に記述し、キャリブレーション成功ログを保持。

### Phase 2: BeatTimeline とメモリ要件 (Day 2-4)
1. Signal Processing パイプライン (BPF→整流→包絡→閾値) を audioIn() で動作させ、`BeatEvent` を生成。
2. `BeatTimeline` を固定長循環バッファ (`std::array`) で実装し、ヒープ確保が無いことをユニットテスト (GoogleTest or Catch2) で検証。
3. メモリ使用量を ofGetFrameRate() 毎にログし、BeatTimeline + Envelope バッファが 5 MB 未満で安定することを確認。
4. 250ms ごとの `HeartbeatStats` を生成し、GUI に現在 BPM/SDNN/RMSSD を表示。NaN/Inf ガードを入れる。
5. `logs/proto_session.csv` に timestampMicros, bpm, envelopePeak, sceneId を追記し、書き込みバッファの flush を 1 秒周期で行う。

### Phase 3: 音響・ハプティクス・シーン遷移 (Day 4-6)
1. `AudioScene` クラスを作り、Idle/First/End の出力を切り替え。First Phase は自己心音 (-15dBFS) とホワイトノイズ (-24dBFS) を合成。
2. 簡易 limiter (`tanh` またはピーク検出 + 比率制御) を実装し、`LimiterState` をログ。-3dBFS 超過時は GUI にアラート。
3. ハプティクス疑似デバイスとして、BeatEvent 時に `HapticEvent { timestamp, intensity, duration }` を `logs/haptic_events.csv` へ書き出し。
4. `SceneController` を構築し、Idle→First→End のステートマシンとフェードスケジューラを実装。GUI ボタンで遷移、オートタイマー対応。
5. Scene 切り替え時に BeatTimeline/Envelope のリセットが行われるか確認し、残留値がないことをユニットテスト。

### Phase 4: 統合・検証・最終報告 (Day 6-9)
1. 全フローを通し 15 分の連続試験を実施。キャリブレーション→Idle→First→End を 3 サイクル実行。
2. ログ類 (session.csv, summary.json, haptic_events.csv) を解析し、欠損やフォーマット崩れ、メモリリーク兆候を確認。
3. BPM 精度、ハプティクス遅延、音量管理の KPI を測定し、検証シートに記録。
4. 改善項目・本体開発へのフィードバック (例: FDN 導入時の懸念、HRTF 接続に必要な API) をまとめる。
5. スプリントレビューで結果を共有し、MASTERDOCUMENT.md に向けた追加要件・工数見直しを提示。

## 12. 検証項目一覧 (最小構成)

| カテゴリ | ID | テスト項目 | 観点 | 手順 | 合格基準 | ログ/証跡 |
| --- | --- | --- | --- | --- | --- | --- |
| キャリブレーション | CAL-01 | ゲイン補正算出 | 精度 | テストトーン再生→JSON 出力確認 | ±3dB 以内、JSON 保存成功 | calibration/channel_separator.json, console log |
| キャリブレーション | CAL-02 | 遅延補正算出 | 時間 | パルス列を記録→相互相関→差分算出 | ±2 samples 以内 | 同上 |
| キャリブレーション | CAL-03 | 再測定 UI | UX | GUI 再測定ボタン使用 | 再測定結果が JSON に反映、UI 更新 | GUI スクリーンショット |
| 信号処理 | DSP-01 | BPF レスポンス | 周波数 | テスト信号 sweep を入力 | 20-150Hz 通過、外帯域 -12dB 以下 | FFT キャプチャ |
| 信号処理 | DSP-02 | BeatEvent 精度 | 検出 | 基準 BPM 付録音を再生 | 平均誤差 ±3 BPM、誤検出率 <5% | session.csv, 検証シート |
| 信号処理 | DSP-03 | BeatTimeline メモリ | リソース | 15 分稼働中のメモリ監視 | 変動 <5MB、ヒープ再割当 0 回 | ofLog, Activity Monitor |
| 音響 | AUD-01 | 自心音ミックス | 音量 | First Phase を再生 | 片ch -15±1dBFS、ノイズ -24±1dBFS | Audio meter スクショ |
| 音響 | AUD-02 | Limiter 動作 | クリップ | 過大入力（テスト波形） | 出力ピーク -3dBFS 以下、歪み最小 | 波形/ログ |
| シーン制御 | SCN-01 | Idle->First 遷移 | 状態 | GUI から遷移 | タイマー・音声・GUI が同期 | session.csv sceneId |
| シーン制御 | SCN-02 | First->End フェード | 演出 | 60s 経過後自動遷移 | 音量 -60dBFS まで 5s 以内 | Audio log |
| ハプティクス | HPT-01 | イベント生成 | 反応 | BeatEvent 受信時ログ | `<40ms` 遅延、CSV 記録 | haptic_events.csv |
| ハプティクス | HPT-02 | 停止制御 | 安全 | End シーン時 | 新規イベント発生無し、ログ終了 | 同上 |
| ロギング | LOG-01 | CSV 書き込み | 完整 | 15 分稼働 | 欠損行 0、エラー出力 0 | session.csv |
| ロギング | LOG-02 | summary.json | 集計 | 終了時 | 平均 BPM, SDNN が更新 | logs/proto_summary.json |
| 全体 | SYS-01 | 長時間安定性 | 信頼性 | 30 分連続試験 | クラッシュ 0、CPU 70% 未満 | Console log, Activity Monitor |

## 13. テストデータと環境整備
- **テストトーン**: 48kHz/24bit、1kHz サイン (5s)、矩形パルス (512 samples) を `data/test_signals/` に配置。キャリブレーションで使用。
- **心拍サンプル**: 実測または公開データ (MIT-BIH など) から 60s の心拍 WAV を準備し、基準 BPM を CSV に添付。
- **疑似ノイズケース**: ホワイトノイズ、無音、スパイク (クリック) を生成して誤検出試験に利用。
- **計測ツール**: 
  - Audio: `ofxAudioAnalyzer` あるいは `iZotope Insight` 等を用い LUFS/Peak を記録。
  - メモリ/CPU: macOS Activity Monitor、`vmmap`、`Instruments` の Time Profiler。
  - ログ解析: Python スクリプトで session.csv をチャート化し BPM 誤差やイベント遅延を算出。
- **環境要件**: Xcode 15 + clang++ 15、macOS Sonoma を想定。テスト時は省電力モードをオフにし、外部オーディオインターフェースを固定設定。

## 14. 計測指標とトラッキング
- **キャリブレーション KPI**: ゲイン差 dB, 遅延サンプル数, 交差相関スコア。試行ごとに `calibration_report.csv` に追記。
- **BeatTimeline KPI**: 誤検出率、欠落率、BPM 平均誤差、SDNN/RMSSD 差分。週次レビューで報告。
- **オーディオ KPI**: LUFS, Peak, TruePeak, THD (可能なら)、Limiter 発火回数。
- **遅延 KPI**: audioIn→BeatEvent→HapticEvent の遅延、Scene 遷移フェード時間、ログ書き込み遅延。
- **安定性 KPI**: CPU 使用率 (%), メモリ (MB), スレッド数、GC/ヒープ割当回数。
- KPI は Notion や Spreadsheet にダッシュボード化し、各テスト回の結果・日時・担当者を記録。

## 15. 最終検証項目 (MASTERDOCUMENT.md への橋渡し)
1. **信号品質評価**: 小規模構成で得られたゲイン補正・遅延測定結果を MASTERDOCUMENT の `channel separator` 要件に照らし、不足があれば本番用アルゴリズム（SVD/逆行列）へ展開する計画を立案。
2. **BeatTimeline 拡張性**: 循環バッファのパフォーマンスと統計更新の負荷を測定し、本番仕様 (最大 18,000 イベント, 5ms グリッド) にスケール可能か判断。必要なら moodycamel::ConcurrentQueue 連携の調査を行う。
3. **音場処理準備**: 自心音ステレオ再生の測定値を基準として、今後 FDN リバーブや HRTF 処理を追加した際のヘッドルーム・遅延・CPU 予算を見積もる。
4. **ハプティクス連携**: 疑似デバイスログから得た遅延・イベント密度を用い、実機 (LED/モーター) 導入時のプロトコル (USB/OSC 等) とバッファ管理の要件を策定。
5. **データ保存・再現性**: config/session_seed.json の利用で再現性が確保できるか検証し、MASTERDOCUMENT の完全版で必要なログローテーションやリプレイ機能の実装計画を整える。
6. **UX/オペレーション**: Idle→First→End の運用手順をドキュメント化し、Init/Exchange/Mixed/End の完全版へ拡張する際のシーン構成・GUI 配置のプロト化を進める。
7. **リスク洗い出し**: 小規模試験で発生した問題 (例: 音声ドロップ, GUI レイアウト崩れ, ファイル書き込み遅延) を抽出し、MASTERDOCUMENT 版のリスク対策表に反映。

最終検証では上記成果を週次レビューで共有し、全項目が合格または改善策付きで完了した時点で「小規模実装フェーズ完了」と判定する。

## 16. 3名体制の作業割当と進行管理

### 16.1 メンバー役割
- **メンバーA (DSP/Audio リード)**  
  - 主担当: キャリブレーション、信号処理、オーディオ出力、Limiter、検証データ解析。  
  - 副担当: ハプティクス遅延分析、KPI ダッシュボード更新 (音響関連)。
- **メンバーB (Vis/UX & Haptics)**  
  - 主担当: GUI プロト、シーン遷移演出、BeatTimeline 可視化、ハプティクスイベントログ設計。  
  - 副担当: オペレータ導線ドキュメント化、UI ベースの再測定手順書。
- **メンバーC (App/Infra & QA)**  
  - 主担当: ログ/設定 IO、循環バッファ実装評価、テスト自動化、ビルド設定・CI 整備。  
  - 副担当: KPI 集計スクリプト、最終報告書の取りまとめ。

### 16.2 フェーズ別タスク割当

| フェーズ | 主要タスク | メンバーA | メンバーB | メンバーC | 依存/備考 |
| --- | --- | --- | --- | --- | --- |
| Phase0 | 開発環境・フォルダ整備 | 共同サポート | UI プロファイル確認 | リード (ビルド検証) | C が最終チェック |
| Phase1 | テストトーン生成・キャリブレーション推定 | リード (CAL-01/02) | GUI 再測定 UI (CAL-03) | JSON I/O 実装 (CAL-01) | 完了後 CAL レポート提出 |
| Phase2 | Signal Processing, BeatTimeline | コア実装 (DSP-01/02) | 可視化・グラフ作成 (DSP-02) | 循環バッファテスト (DSP-03), ログ (LOG-01) | 週中レビューで BPM 誤差報告 |
| Phase3 | AudioScene + Limiter + イベント | オーディオ合成 (AUD-01/02) | SceneController, GUI (SCN-01/02, HPT-01) | ハプティクスCSV, ログ整備 (HPT-01/02, LOG-02) | End シーン時の停止仕様共有 |
| Phase4 | 統合試験・計測・報告 | KPI 測定 (音響, 遅延) | UX/導線文書, スクショ | ログ解析スクリプト, レポートドラフト | SYS-01 を全員で確認 |

### 16.3 日次・週次運用
- **デイリースタンドアップ (15分)**: 進捗・阻害要因・当日タスクを共有。CAL/DSP/AUD などテスト結果はログ参照リンク付きで報告。
- **週次レビュー (60分)**: フェーズ成果・KPI を確認し、MASTERDOCUMENT への反映事項を議論。各メンバーが担当セクションのレポートを更新。
- **コードレビュー体制**: PR は担当外のメンバーがレビュー。例: A の DSP PR は C がレビュー、B の GUI PR は A or C がレビュー。
- **ドキュメント更新**: Notion/Confluence 等で `Calibration Report`, `Test Matrix`, `KPI Dashboard` を共有。メンバーC が更新リマインドを送る。

### 16.4 成果物オーナーシップ
- Calibration レポート (CAL-01〜03): メンバーA (著者) + C (レビュー)。
- BeatTimeline 実装仕様・メモリ検証 (DSP-01〜03): メンバーA (著者) + C (性能測定)。
- GUI/Scene 操作ガイド (SCN-01/02, CAL-03): メンバーB (著者) + A (技術確認)。
- ハプティクスログ設計書 (HPT-01/02): メンバーB (著者) + C (ファイル構造レビュー)。
- ログ書式/API ドキュメント (LOG-01/02): メンバーC (著者) + A/B (利用確認)。
- 統合テスト結果・最終報告 (SYS-01, KPI 総括): メンバーC (編集) + A/B (技術補足)。

### 16.5 チェックポイント
- **CP1 (Day2 終了)**: Phase1 完了。キャリブレーション JSON と GUI 再測定が動作、CAL-01〜03 のテスト証跡を提出。
- **CP2 (Day4 終了)**: Phase2 完了。BeatTimeline が稼働し、BPM 誤差レポートとメモリ測定が共有されている。
- **CP3 (Day6 終了)**: Phase3 完了。シーン遷移・オーディオ・ハプティクスログが連動し、DSP-02/AUD-01/HPT-01 の KPI が計測済み。
- **CP4 (Day9 終了)**: Phase4 完了。SYS-01 含む統合試験の合格、最終レポートと MASTERDOCUMENT へのフィードバック案を提出。

### 16.6 リスク管理・エスカレーション
- KPI が閾値に未達の場合、担当メンバーは即日 Slack/issue で共有し、翌日のスタンドアップで対策案を合意する。
- 負荷過多時はサブ担当がバックアップ。例: A が音響で詰まった場合、C がログ関連を巻き取り A を支援。
- スケジュール遅延が 0.5 日超えた時点でプロジェクトオーナーへエスカレーションし、スコープ調整（演出簡略化等）を検討する。

3 名の責務を明文化することで、各フェーズに必要な実装と検証成果を確実に積み上げ、MASTERDOCUMENT の最終要件に滑らかにつなげる。

### 16.7 レポート提出体制
- 各メンバーは週次レビュー前日までに所定ディレクトリにレポートを更新する。
  - メンバーA: `reports/memberA/PROGRESS_REPORT.md`
  - メンバーB: `reports/memberB/PROGRESS_REPORT.md`
  - メンバーC: `reports/memberC/PROGRESS_REPORT.md`
- 付随データ格納場所
  - 音響・計測スクリーンショット: `reports/memberA/assets/`
  - GUI/演出素材: `reports/memberB/assets/`
  - テスト結果・スクリプト: `reports/memberC/test_results/`
- レポート更新後は Slack で共有し、レビュー担当が確認。差戻し事項は翌日までに対応。

## 17. 追加指示 (2025-10-28 改訂)
- **キャリブレーション再実施方針**  
  - 2ch/48kHz を保証できる入力・出力デバイスを明示的に選択し、交互トーンがクリップやミュートを起こさない条件で複数回測定する。  
  - 測定ごとの結果は `calibration/channel_separator.json` と `calibration_report.csv` に追記し、±3 dB・±2 samples を外れる場合は原因調査メモを残す。  
  - 必要に応じて複数回測定の平均化／外れ値除去アルゴリズムを検討し、再測定フローを GUI に反映する。

- **BeatTimeline 誤検出対策**  
  - 包絡ピーク後 300–400ms のリフラクトリ期間を設け、S1/S2 の二重検出を防ぐ。  
  - ピーククラスタリングやイベント強度フィルタを導入し、`scripts/validate_logs.py` で BPM 誤差 ±3 BPM を達成する。  
  - 対応後の `logs/proto_session.csv` と `logs/proto_summary.json` を提出し、誤検出率と RMSSD/SDNN の改善を記録する。

- **Envelope Baseline キャリブレーション**  
  - `Envelope Baseline 計測` ボタンで 3 秒間の包絡値を収集し、平均・最大・推奨トリガ比を `BeatTimeline` に反映する。  
  - 計測結果は `calibration_report.csv` に追記し、`validate_logs.py --calibration-report` で peak/mean 比が 1.15 以上であることを確認する。  
  - 計測成功率（3 回中成功 2 回以上）と再検出率（未検出復帰時間）を KPI としてレポートへ記載する。

- **GUI/UX 改修指針**  
  - シーン遷移とエンベロープを監視できるサブパネル（小ウィンドウやポップアウト）を追加し、フェード中やキャリブレーション中の状態が一目で分かるようにする。  
  - キャリブレーション中に操作が無効化される理由や、検出強度が弱い際のガイダンスを GUI 上に明示する。  
  - ハプティクスログを可視化するチャートを実装し、1 拍 1 イベントで動作しているか即時確認できるようにする。

- **App/Infra & QA 強化項目**  
  - `bin/data/config/app_config.json` のログ出力先をプロジェクトルート配下へ見直し、成果物を CI で収集しやすくする。  
  - キャリブレーション値と BPM 誤差を自動判定するチェックを `scripts/validate_logs.py` に追加し、CI の合否条件に組み込む。  
  - 30 分連続稼働テストを実施し、over/underflow の発生頻度と対策をレポートに反映する。

- **共通遵守事項**  
  - `MASTERDOCUMENT.md` の「RULES FOR CODING」を厳守し、安易なキャスト・グローバル共有を禁止。データ所有と依存関係を明確に設計する。  
  - CPU/GPU の役割分担を常に検討し、`ofGetElapsedTimeMicros()` 等で定量的に計測した結果をレポートに添付する。推測ベースの最適化は行わない。  
  - キャリブレーション中や状態遷移中の挙動はログと GUI 双方でユーザに伝え、原因不明の「ignored」ログが出ないようメッセージを整備する。  
  - KPI 未達時は即日共有し、翌日のスタンドアップで対策を合意。スケジュール遅延が 0.5 日を超える場合はプロジェクトオーナーへエスカレーションする。
