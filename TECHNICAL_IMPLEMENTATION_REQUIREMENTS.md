# Mixedフェーズ実装要件 - 技術的な実現項目

このドキュメントは、USER_EXPERIENCE_PHASES.mdで定義されたMixedフェーズの視覚体験を実現するために、技術的に実装・補完すべき項目を整理したものです。

---

## 1. 2つの心拍の波の干渉パターン実装

### 現状
- 現在の実装では、単純なwobble効果（`std::sin(nowSeconds * 1.1f + i * 0.27f)`）のみ
- 2つの心拍の相互作用が明確に表現されていない

### 実装すべき項目

#### 1.1 心拍リズムの位相計算
- **必要なデータ**:
  - 各参加者の心拍位相（`participantHeartbeatPhase_[0]`, `participantHeartbeatPhase_[1]`）
  - 心拍の周波数（BPMから計算）
  - エンベロープ値（`participantEnvelopes_[0]`, `participantEnvelopes_[1]`）

- **実装内容**:
  ```cpp
  // 各参加者の心拍位相を時間に基づいて計算
  float phase1 = calculateHeartbeatPhase(participantBpms_[0], nowSeconds);
  float phase2 = calculateHeartbeatPhase(participantBpms_[1], nowSeconds);
  
  // 2つの波の干渉を計算
  float interference = std::sin(phase1) + std::sin(phase2);
  float interferenceAmplitude = std::sqrt(
      std::sin(phase1) * std::sin(phase1) + 
      std::sin(phase2) * std::sin(phase2) + 
      2.0f * std::sin(phase1) * std::sin(phase2) * std::cos(phase1 - phase2)
  );
  ```

#### 1.2 波の干渉による半径変形
- **実装内容**:
  - 各角度位置での半径を、2つの心拍の波の干渉により計算
  - 建設的干渉（同期）: 半径が大きくなる
  - 破壊的干渉（位相差）: 半径が小さくなる
  - 高次調波を追加して、有機的な不規則性を実現

- **コード例**:
  ```cpp
  // 基本半径
  float baseRadius = safeLerp(160.0f, 320.0f, envelope);
  
  // 2つの心拍の波
  float wave1 = std::sin(phase1 + angle * frequency1) * envelopeP1;
  float wave2 = std::sin(phase2 + angle * frequency2) * envelopeP2;
  
  // 干渉パターン
  float interference = wave1 + wave2;
  float interferenceStrength = std::abs(interference);
  
  // 高次調波
  float harmonic2 = 0.3f * std::sin(phase1 * 2.0f + angle * frequency1 * 2.0f);
  float harmonic3 = 0.1f * std::sin(phase2 * 3.0f + angle * frequency2 * 3.0f);
  
  // 最終的な半径
  float radius = baseRadius * (1.0f + 0.2f * interferenceStrength + 0.1f * harmonic2 + 0.05f * harmonic3);
  ```

---

## 2. 連続性のある表現（切れ目のない輪郭）

### 現状
- セグメント数: 180
- 角度計算: `i / segments * 360.0f`（0度から360度まで）
- 問題: 0度と360度の間で切れ目が生じる可能性

### 実装すべき項目

#### 2.1 高解像度なセグメント分割
- **実装内容**:
  - セグメント数を180から360以上に増加
  - より滑らかな曲線を実現

#### 2.2 角度計算の改善
- **実装内容**:
  - 時間ベースのオフセットを追加
  - スプライン補間またはベジェ曲線を使用
  - 最後の点と最初の点を滑らかに接続

- **コード例**:
  ```cpp
  const int segments = 360;  // 180から360に増加
  const float angleOffset = nowSeconds * 0.1f;  // 時間ベースのオフセット
  
  for (int i = 0; i <= segments; ++i) {
      // 角度にオフセットを加えて、切れ目を解消
      float normalizedAngle = static_cast<float>(i) / segments;
      float angle = normalizedAngle * 2.0f * glm::pi<float>() + angleOffset;
      
      // 最後の点を最初の点と滑らかに接続
      if (i == segments) {
          angle = angleOffset;  // 最初の点に戻る
      }
      
      // ... 半径計算など
  }
  ```

#### 2.3 スプライン補間の実装（オプション）
- **実装内容**:
  - Catmull-RomスプラインまたはBスプラインを使用
  - より滑らかな輪郭を実現

---

## 3. 光の拡散実装

### 現状
- BloomRendererは実装済み（`BloomRenderer.cpp`）
- 基本的なガウシアンブラーとコンポジットシェーダー

### 実装すべき項目

#### 3.1 オーロラのような光の流れ
- **実装内容**:
  - 複数の層に光を配置
  - 各層が異なる速度で動く
  - 流動的な光の軌跡を実現

- **コード例**:
  ```cpp
  // 複数の光の層
  for (int layer = 0; layer < 3; ++layer) {
      float layerSpeed = 0.5f + layer * 0.3f;
      float layerRadius = radius * (0.8f + layer * 0.2f);
      float layerAlpha = clampedAlpha * (1.0f - layer * 0.3f);
      
      // オーロラのような流動的な動き
      float flowAngle = nowSeconds * layerSpeed + layer * glm::pi<float>() * 0.5f;
      float flowRadius = layerRadius * (1.0f + 0.1f * std::sin(flowAngle));
      
      // 光の描画
      drawAuroraLayer(center, flowRadius, flowAngle, layerAlpha);
  }
  ```

#### 3.2 深度のある光の階層
- **実装内容**:
  - 前景・中景・背景の3層構造
  - 各層が異なるブラー強度と透明度を持つ
  - 深度感を演出

- **実装方法**:
  ```cpp
  // 前景: 明るく鮮明
  bloomRenderer_.setBloomIntensity(1.0f);
  bloomRenderer_.setBloomRadius(2.0f);
  drawForegroundLayer();
  
  // 中景: やや拡散
  bloomRenderer_.setBloomIntensity(1.5f);
  bloomRenderer_.setBloomRadius(4.0f);
  drawMidgroundLayer();
  
  // 背景: 大きく拡散
  bloomRenderer_.setBloomIntensity(2.0f);
  bloomRenderer_.setBloomRadius(8.0f);
  drawBackgroundLayer();
  ```

#### 3.3 光の散乱シミュレーション（オプション）
- **実装内容**:
  - レイリー散乱: 短波長の光がより強く散乱
  - ミー散乱: 大きな粒子による散乱
  - シェーダーで実装可能

---

## 4. 色彩の変化実装

### 現状
- HSL色空間での基本的なグラデーション
- 色相: 0.55-0.63の範囲
- エンベロープに応じた彩度・明度の変化

### 実装すべき項目

#### 4.1 心拍同期による色彩統一
- **実装内容**:
  - 2つの心拍の同期度合いを計算
  - 同期度が高いほど、色彩が統一される
  - 色相の範囲を0.55-0.73に拡張

- **コード例**:
  ```cpp
  // 2つの心拍の同期度を計算
  float phaseDiff = std::abs(phase1 - phase2);
  float syncLevel = 1.0f - std::min(phaseDiff / glm::pi<float>(), 1.0f);
  
  // 同期度に応じて色相を統一
  float baseHue = 0.55f + 0.18f * (1.0f - syncLevel);  // 0.55-0.73
  float hueVariation = 0.08f * (1.0f - syncLevel);  // 同期度が高いほど変化が小さい
  
  // 色相の計算
  float hue = baseHue + hueVariation * std::sin(angle * 3.0f + noisePhase);
  ```

#### 4.2 動的なグラデーション
- **実装内容**:
  - 時間とともに色相が変化
  - 中心部から外側への放射状グラデーション
  - 自然光の色温度変化を参考

- **コード例**:
  ```cpp
  // 時間による色相の変化
  float timeHue = 0.55f + 0.18f * std::sin(nowSeconds * 0.1f);
  
  // 距離による色相の変化
  float distance = glm::length(pos - center);
  float maxDistance = radius;
  float distanceHue = timeHue + 0.1f * (distance / maxDistance);
  
  // 最終的な色相
  float hue = distanceHue + hueVariation * std::sin(angle * 3.0f + noisePhase);
  ```

#### 4.3 深度による透明度の変化
- **実装内容**:
  - 中心部は明るく、外側へ向かって徐々に暗くなる
  - 透明度のグラデーションにより、光が拡散しているような効果

- **コード例**:
  ```cpp
  // 距離による透明度の変化
  float distance = glm::length(pos - center);
  float maxDistance = radius;
  float alphaFalloff = 1.0f - smoothstep(0.0f, maxDistance, distance);
  
  // 最終的な透明度
  float alpha = clampedAlpha * 0.6f * alphaFalloff;
  ```

---

## 5. リズミカルな脈動実装

### 現状
- 5層のパルス円（`ofDrawCircle`）
- 単純なサイン波による脈動

### 実装すべき項目

#### 5.1 心拍エンベロープに追従した脈動
- **実装内容**:
  - 心拍のエンベロープに追従して、光の強度が変化
  - 2つの心拍のエンベロープを組み合わせ

- **コード例**:
  ```cpp
  // エンベロープに追従した脈動
  float envelopePulse = (envelopeP1 + envelopeP2) * 0.5f;
  float intensity = 0.5f + 0.5f * envelopePulse;
  
  // 光の強度に適用
  float lightIntensity = intensity * clampedAlpha;
  ```

#### 5.2 ビート周波数（うなり）の実装
- **実装内容**:
  - 2つの心拍の周波数差により、うなりのような効果
  - 光の強度がうなりに合わせて変化

- **コード例**:
  ```cpp
  // 2つの心拍の周波数
  float freq1 = participantBpms_[0] / 60.0f;
  float freq2 = participantBpms_[1] / 60.0f;
  
  // ビート周波数
  float beatFreq = std::abs(freq1 - freq2);
  float beatPhase = (freq1 - freq2) * nowSeconds * 2.0f * glm::pi<float>();
  
  // うなりの効果
  float beatAmplitude = 0.1f * std::sin(beatPhase);
  float intensity = 1.0f + beatAmplitude;
  ```

#### 5.3 複雑なインターフェアレンスパターン
- **実装内容**:
  - 2つの心拍リズムが重なり、複雑なパターンを生成
  - 各層が異なる位相で脈動

- **コード例**:
  ```cpp
  // 複数の層で異なる位相
  for (int layer = 0; layer < 5; ++layer) {
      float layerPhase1 = phase1 + layer * 0.2f;
      float layerPhase2 = phase2 + layer * 0.3f;
      
      float layerInterference = std::sin(layerPhase1) + std::sin(layerPhase2);
      float layerIntensity = 0.5f + 0.5f * layerInterference;
      
      // 層の描画
      drawPulseLayer(center, radius, layerIntensity, layerAlpha);
  }
  ```

---

## 6. パフォーマンス最適化

### 実装すべき項目

#### 6.1 シェーダーへの移行
- **現状**: CPU側でメッシュを生成
- **改善**: シェーダーで計算を実行
- **メリット**: GPUで並列処理、パフォーマンス向上

#### 6.2 メッシュの最適化
- **実装内容**:
  - メッシュの生成を最適化
  - 不要な頂点の削減
  - インスタンシングの活用

#### 6.3 描画の最適化
- **実装内容**:
  - 描画コールの削減
  - バッチ処理の活用
  - フレームレートの監視と調整

---

## 7. 実装優先度

### 高優先度（必須）
1. **2つの心拍の波の干渉パターン実装**（項目1）
2. **連続性のある表現（切れ目のない輪郭）**（項目2）
3. **心拍同期による色彩統一**（項目4.1）

### 中優先度（推奨）
4. **オーロラのような光の流れ**（項目3.1）
5. **深度のある光の階層**（項目3.2）
6. **リズミカルな脈動実装**（項目5）

### 低優先度（オプション）
7. **光の散乱シミュレーション**（項目3.3）
8. **スプライン補間の実装**（項目2.3）
9. **パフォーマンス最適化**（項目6）

---

## 8. 実装の進め方

### ステップ1: 基礎実装
1. 心拍位相の計算関数を実装
2. 波の干渉パターンの計算を実装
3. 連続性のある輪郭の描画を実装

### ステップ2: 視覚効果の強化
1. 色彩の変化を実装
2. 光の拡散を強化
3. リズミカルな脈動を実装

### ステップ3: 最適化と調整
1. パフォーマンス最適化
2. 視覚的な調整
3. ユーザーテストとフィードバック

---

## 9. 参考実装

### 既存の実装を参考にする
- `BloomRenderer.cpp`: 光の拡散処理
- `drawHeartbeatLight()`: 心拍の可視化
- `drawHeartbeatRipples()`: 波紋の描画

### 新しい実装が必要な部分
- 2つの心拍の波の干渉計算
- 連続性のある輪郭の描画
- オーロラのような光の流れ
- 心拍同期による色彩統一

---

## 10. Exchangeフェーズの音響演出実装（HRTFによる音源の移動・交換）

### 現状
- 基本的なステレオパンニングのみ実装
- HRTFによるバイノーラルレンダリングは未実装
- 音源の空間的な移動は未実装

### 実装すべき項目

#### 10.1 HRTFライブラリの統合
- **必要なライブラリ**: 
  - MIT KEMAR HRIR SOFAファイル（`bin/data/hrir/mit_kemar_normal_pinna.sofa`）
  - libmysofa（SOFAファイル読み込み用）

- **実装内容**:
  - SOFAファイルの読み込み
  - HRIRテーブルの構築
  - リアルタイムでのHRIR適用

#### 10.2 音源の3D空間位置の計算
- **実装内容**:
  - 各参加者の心音を3D空間上の音源として扱う
  - 初期位置: 中央付近（距離: 1.0m, 方位角: 0°, 仰角: 0°）
  - 目標位置: 外側（距離: 3.0m, 方位角: ±90°, 仰角: 0°）
  - パートナーの心音: 遠方から中央へ移動

- **コード例**:
  ```cpp
  // 音源の位置計算（20秒間の移動）
  float exchangeProgress = std::clamp(timeInExchange / 20.0f, 0.0f, 1.0f);
  float easedProgress = 0.5f - 0.5f * std::cos(exchangeProgress * glm::pi<float>());
  
  // 自分の心音: 中央 → 外側
  float myDistance = 1.0f + 2.0f * easedProgress;
  float myAzimuth = (participantId == 1) ? 90.0f * easedProgress : -90.0f * easedProgress;
  
  // パートナーの心音: 遠方 → 中央
  float partnerDistance = 3.0f - 2.0f * easedProgress;
  float partnerAzimuth = (participantId == 1) ? -90.0f * (1.0f - easedProgress) : 90.0f * (1.0f - easedProgress);
  ```

#### 10.3 距離に応じた音の変化
- **実装内容**:
  - 距離減衰: 音量が距離に応じて減衰
  - スペクトラル変化: 距離が遠くなるほど高周波成分が減少
  - 空気吸収: 距離に応じた高周波の減衰

- **コード例**:
  ```cpp
  // 距離減衰（逆二乗則）
  float distanceAttenuation = 1.0f / (distance * distance);
  
  // スペクトラル変化（ローパスフィルター）
  float cutoffFreq = 20000.0f / (1.0f + distance * 0.5f);
  applyLowPassFilter(sample, cutoffFreq);
  
  // 空気吸収（高周波の減衰）
  float airAbsorption = std::exp(-distance * 0.1f);
  applyHighFrequencyAttenuation(sample, airAbsorption);
  ```

#### 10.4 HRTFによるバイノーラルレンダリング
- **実装内容**:
  - HRIRテーブルから適切なインパルス応答を選択
  - 左右チャンネルに異なるHRIRを適用
  - リアルタイムでの畳み込み処理

- **コード例**:
  ```cpp
  // HRIRの選択
  int hrirIndex = findHRIRIndex(azimuth, elevation);
  const float* hrirLeft = hrirTable_[hrirIndex].left;
  const float* hrirRight = hrirTable_[hrirIndex].right;
  
  // 畳み込み処理
  float leftOut = convolve(sample, hrirLeft, hrirLength);
  float rightOut = convolve(sample, hrirRight, hrirLength);
  
  // 距離減衰とスペクトラル変化を適用
  leftOut *= distanceAttenuation;
  rightOut *= distanceAttenuation;
  applySpectralChanges(leftOut, distance);
  applySpectralChanges(rightOut, distance);
  ```

#### 10.5 音源の移動アニメーション
- **実装内容**:
  - 20秒間かけて音源が移動
  - 滑らかなイーズイン・アウト曲線
  - フレームごとに音源位置を更新

- **コード例**:
  ```cpp
  // 音源位置の更新
  void updateSoundSourcePosition(double timeInExchange) {
      float progress = std::clamp(timeInExchange / 20.0f, 0.0f, 1.0f);
      float eased = easeInOutCubic(progress);
      
      // 自分の心音の位置更新
      mySoundSource_.distance = 1.0f + 2.0f * eased;
      mySoundSource_.azimuth = (participantId == 1) ? 90.0f * eased : -90.0f * eased;
      
      // パートナーの心音の位置更新
      partnerSoundSource_.distance = 3.0f - 2.0f * eased;
      partnerSoundSource_.azimuth = (participantId == 1) ? -90.0f * (1.0f - eased) : 90.0f * (1.0f - eased);
  }
  ```

#### 10.6 パフォーマンス最適化
- **実装内容**:
  - 畳み込み処理の最適化（FFT畳み込みなど）
  - HRIRテーブルのキャッシュ
  - リアルタイム処理の最適化

---

## 11. 実装優先度（更新）

### 高優先度（必須）
1. **2つの心拍の波の干渉パターン実装**（項目1）
2. **連続性のある表現（切れ目のない輪郭）**（項目2）
3. **心拍同期による色彩統一**（項目4.1）
4. **HRTFライブラリの統合**（項目10.1）
5. **音源の3D空間位置の計算**（項目10.2）

### 中優先度（推奨）
6. **オーロラのような光の流れ**（項目3.1）
7. **深度のある光の階層**（項目3.2）
8. **リズミカルな脈動実装**（項目5）
9. **距離に応じた音の変化**（項目10.3）
10. **HRTFによるバイノーラルレンダリング**（項目10.4）

### 低優先度（オプション）
11. **光の散乱シミュレーション**（項目3.3）
12. **スプライン補間の実装**（項目2.3）
13. **パフォーマンス最適化**（項目6, 10.6）
14. **音源の移動アニメーション**（項目10.5）

---

## 12. テスト項目（更新）

### 視覚的なテスト
- [ ] 2つの心拍の干渉パターンが正しく表示される
- [ ] 輪郭に切れ目がない
- [ ] 光の拡散が美しく表現される
- [ ] 色彩が心拍の同期に応じて変化する
- [ ] リズミカルな脈動が正しく動作する

### パフォーマンステスト
- [ ] 60fpsを維持できる
- [ ] GPU使用率が適切
- [ ] メモリ使用量が適切

### ユーザーテスト
- [ ] ユーザーが美しいと感じる
- [ ] 2つの心拍の相互作用が感じられる
- [ ] 共鳴の体験ができる

### 音響テスト
- [ ] HRTFによる空間定位が正しく動作する
- [ ] 音源の移動が滑らかに実現される
- [ ] 距離に応じた音の変化が感じられる
- [ ] 20秒間の交換プロセスが正しく動作する
- [ ] 2つの心音が正しくミックスされる（Mixedフェーズ）

