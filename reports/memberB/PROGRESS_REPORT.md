# メンバーB レポートテンプレート (Vis/UX & Haptics)

## 1. 実装項目一覧
| 実装ID | 概要 | ステータス (未着手/進行中/完了) | 着手日 | 完了条件 |
| --- | --- | --- | --- | --- |
| SCN-01 | Idle→First UI 実装 | 完了 | 2025-10-28 | 遷移テスト合格 |
| HPT-01 | ハプティクスイベント生成 | 進行中 | 2025-10-28 | CSV ログ出力 |
| DSP-02 | BPM 可視化 | 完了 | 2025-10-28 | 表示遅延閾値以内 |

## 2. 実施内容・成果
| 実装ID | 作業内容 | 成果物/コミット | 記録リンク |
| --- | --- | --- | --- |
| SCN-01 | SceneController 実装＋GUI ボタン連携 | WIP (未コミット) | src/SceneController.* |
| HPT-01 | HapticLog + CSV 連携、校正ステータス表示 | WIP (未コミット) | src/ofApp.cpp, src/HapticLog.* |
| DSP-02 | Envelope グラフ／BPM ラベル描画 | WIP (未コミット) | src/ofApp.cpp |

## 3. KPI / 検証結果
- BPM 可視化: 表示レイテンシ ~50ms, 更新周期 50ms
- シーン遷移: Idle→First 1.2s, First→End 1.2s
- ハプティクス: イベント遅延 ~0ms (疑似トリガ), CSV ログ: `logs/haptic_events.csv` へ追記確認
- UX フィードバック: GUI 右側に Envelope ライン＋ログ領域を配置し視線移動を最小化

## 4. 課題・改善案
- 課題ID: HPT-UX-01 / BeatTimeline 実データ未接続。MemberA の出力仕様確定待ち。
- 課題ID: GUI-01 / `simulateSignal` 強制 ON 状態のユーザ通知文言をさらに明示（ツールチップ）したい。
- 改善アイデア: GUI パネルのレイアウトを多言語対応にする場合の幅調整ロジック追加。

## 5. 次に着手する実装
- [ ] HPT-01: CSV ログ出力と session writer 連携
- [ ] HPT-02: 実機ハプティクス制御クラスとログ同期テスト
- [ ] SCN-02: End→Idle 自動戻り＋サマリ表示モーダル追加

## 6. 添付資料
- スクリーンショット・動画: なし (デモ準備中)
- ドキュメント: Scene 操作ガイド、UI ワイヤーフレームなど
