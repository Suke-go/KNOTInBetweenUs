# KNOT -An Emergent Image Between Us-

This is the visualization/sonification system of people's heartbeat. 

## What need to be coded?
- channel separator
    - a system to separate channels(2ch input need to be independetly processed)
- signal processing system
    - a system to count heartbeat from raw audio signal from microphone
- visualization system
- audio synth system
    - a system to add pink noise
    - a system to localize sound source
    - a system to simulate spatial characteristics
        - like reberberation of sound
- audio output system
    - output sound to speaker, subwoofer(for 4 channel with audio interface)
- GUI

## Technical Stuff
- C++
- openFrameworks
- GLSL shader(for beautiful visualization)

## Purpose
- 心音を多感覚で外化し、自己と他者の存在を静かに実感できる場をつくる。
- 他者の心拍を共有することで、身体リズムの共鳴や相互理解を促す。
- 視覚・触覚・聴覚を統合した演出によって、没入を支える体験環境を提供する。
- 心音が生成するリアクティブビジュアルの鑑賞を通じ、参加者同士の関係性や体験の余韻を深める。

## UX
- 開始準備
    - Init Scene(最初だけ)
        - Speaker Selection(play pink noise)
            - Headphone 1
            - Headphone 2
            - Bass Speaker 1
            - Bass Speaker 2
        - Init microphone
            - visualize signals
    - Idle状態
        - 常時マイクからの入力をripplesとして表現する(心音のチェック)
            - ripplesはcountheartrateクラスによってcountedされた心音のピーク
                - つまり心音がきちんと入力されているか，されていないかを目視できるようなシステム
            - ripplesのもとは全画面に対して，中心-画面端の中間位置で二つ配置する
            - 背景は黒色
                - 背景の中に星空のように白色のglowしている点を描画する
            - ripplesは白色，ripplesの円には一部noiseを入れる（noisy rateは5%-7%, random, per second）
                - 動きに合わせて白の透明度合を変化させていく，中心から外に行けば行くだけ，薄くなっていく
                - ripplesの一度に描画できる円の数は8つまで
    - オペレータが「開始」ボタンを押下。
    - 「体験を始めます」を10秒間表示
        - 背景は黒色
            - 背景の中に星空のように白色のglowしている点を描画する
        - 日本語のフォントはNoto Sans JP Thinで単色白色
    - play bell.wav
    - Start First Phase: 自分の心音を他感覚的に近くする（1min）
        - 各自に自身の心音を再生
            - 100Hzでローパスされた心音リズムに同期した振動で調和を促進
        - 背景は黒色
            - 背景の中に星空のように白色のglowしている点を描画する
            - 全体の面積を中心線（x軸｜縦軸）で区切ってその中にプロットされている星空のパルスがcountheartrateによってcountされるたびにglowする感じ



音: 特別な演出は加えず静寂を維持。
視覚アイデア: 初期に検出されたHRVの平均値を基準に、偏差量で歪む紐状のリングを表示（結び目やアメーバ状の変形）。時間経過に合わせて暖色系の多層グラデーションを重ね、彩度・透明度を滑らかに遷移させる。
未充足: トーラスのシェーダ実装や変形パラメータの定義、カウント表示のレイアウト（中央固定or周辺配置）。

心音交換フェーズ（約1分）
遷移: フェード＋高周波音でシームレスにモード切替。
音: 相手の心音を自身のチャンネルで再生、ホワイトノイズをミックス。
ビジュアル: 相手の心音に基づくシーンに切替（具体的演出不明）。
未充足: ホワイトノイズのレベル、遷移時間、定位の扱い（左右入替など）の仕様。
混在フェーズ
音: 自身の心音とホワイトノイズを基調に、ホワイトノイズは心音より約20%高いレベルで設定。インスペクター上で心音/ノイズ比率を微調整できるようにする。
相手心音演出:
Binauralモード＋3次元音源配置で定位し、距離情報を強調。
原音レベルは抑え、Francois–Garrison式を用いた周波数依存吸収を距離に応じて適用し、高域が徐々に減衰する響きを作る。
巨大空間の残響: Early reflectionsとLate tailを分離し、後者はFDNベースで約8秒の長いリバーブを合成。
Mid/Side処理で原音はMid寄せ、残響はSideを広げて包囲感を演出（IACC低下による包まれ感）。
サイドチェイン・コンプレッションで原音が鳴る瞬間に残響を柔らかくダッキングし、モワつきを抑制。
デエッサー＋Tilt EQで高域の勾配を整え、最後にルックアヘッド・リミッタで突発ピークを制御。
触覚: 基本は自分の心音に同期。相手成分を重ねるかどうかは要検討。
シーン管理: ミックス比や空間パラメータは共通スクリプトで保持し、シーン遷移時にクラス値を更新してデフォルトゲインやエフェクト設定を切り替える。リアルタイム心音振幅に応じたマスタゲイン自動調整も検討。
ビジュアル: 両者の心拍データから生成されるビジュアルを鑑賞。
音素材: 使用する基底音素材は全シーン共通。心音はリアルタイム取得値を使用するため変動がある。
終了処理
表示: 体験終了メッセージと退出案内。
音: 心音フェードアウト後に穏やかなアンビエントor静寂を提示。
未充足: 終了後のシステム状態（自動でキャリブレーションへ戻るか、待機画面へ遷移するか）とログ保存手順。