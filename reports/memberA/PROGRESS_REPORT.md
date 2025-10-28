# メンバーA レポートテンプレート (DSP/Audio)

## 1. 実装項目一覧
| 実装ID | 概要 | ステータス (未着手/進行中/完了) | 着手日 | 完了条件 |
| --- | --- | --- | --- | --- |
| CAL-01 | ゲイン補正測定 | 進行中 | 2025-10-28 | JSON に補正値が反映 |
| CAL-02 | 遅延補正推定 | 進行中 | 2025-10-28 | 遅延 ±2 samples 以内 |
| DSP-01 | BPF/包絡ライン実装 | 進行中 | 2025-10-28 | FFT 測定で仕様どおり |
| DSP-02 | BeatEvent 精度検証 | 未着手 | - | 基準 BPM 誤差 ±3 内 |
| AUD-01 | 自心音ミックス | 進行中 | 2025-10-28 | Peak/LUFS が規定内 |
| AUD-02 | Limiter 動作検証 | 進行中 | 2025-10-28 | -3dBFS 以内で安定 |

## 2. 実施内容・成果
| 実装ID | 作業内容 | 成果物/コミット | 記録リンク |
| --- | --- | --- | --- |
| CAL-01/CAL-02 | キャリブレーション信号生成・解析クラス `CalibrationSession` 実装、ofApp からキャリブレーション起動/保存を連携 | `src/audio/Calibration.*`, `src/audio/AudioPipeline.*`, `src/ofApp.cpp` | - |
| DSP-01 | BeatTimeline 処理チェーン（HPF→LPF→包絡→動的閾値）と ofApp GUI 連携 | `src/audio/BeatTimeline.*`, `src/audio/AudioPipeline.*`, `src/ofApp.cpp` | - |
| AUD-01/AUD-02 | 自心音 + ホワイトノイズ合成、簡易リミッター適用、Limiter 減衰メータ表示 | `src/audio/AudioPipeline.cpp`, `src/ofApp.cpp` | - |

## 3. 計測・検証結果
- キャリブレーション: gain=未計測, delay=未計測, phase=未計測
- BeatTimeline: 誤差 未計測 BPM, 誤検出率 未計測 %
- オーディオ: Peak 未計測 dBFS, LUFS 未計測 dB, Limiter 発火回数 未計測
- その他 (必要なら追加)

## 4. 課題・リスク・対処
- Issue #__ : 原因 / 影響 / 暫定対応
- 要サポート事項:

## 5. 次に着手する実装
- [ ] DSP-02: BeatEvent 精度検証（録音データでの BPM 誤差算出）
- [ ] CAL-01/02: 実測ログ採取と JSON 出力の誤差評価

## 6. 添付資料
- 記録ファイル: `logs/...`
- スクリーンショット: `/reports/memberA/assets/`
