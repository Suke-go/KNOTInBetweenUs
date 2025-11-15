#version 120

uniform vec2 uResolution;
uniform float uTime;
uniform vec2 uEnvelopes;
uniform float uAlpha;

// 複数の波を重ねてより複雑なリップル効果を生成
float multiWave(float dist, float time, float speed, float freq, float offset) {
    // 基本波 + 高周波の細かい波 + ゆっくりとした大きな波の組み合わせ
    float wave1 = sin((dist - time * speed) * freq + offset);
    float wave2 = sin((dist - time * speed * 1.3) * freq * 2.2 + offset * 1.7) * 0.3;
    float wave3 = sin((dist - time * speed * 0.6) * freq * 0.5 + offset * 2.3) * 0.5;
    return (wave1 + wave2 + wave3) / 1.8;
}

// グロー効果を生成（中心に近いほど明るい）
float generateGlow(float dist, float radius) {
    return exp(-dist * dist / (radius * radius));
}

// 光の伝播 (wavefront) - 心拍から放射される光の波面
float lightPropagation(float dist, float time, float speed) {
    // 心拍から放射される光の波面
    float wavefront = fract(time * speed - dist * 0.5);
    
    // 波面の先端で明るく、後ろで暗く
    float intensity = smoothstep(0.9, 1.0, wavefront) - smoothstep(0.0, 0.1, wavefront);
    
    // 距離による減衰
    float decay = exp(-dist * 2.0);
    
    return intensity * decay;
}

void main() {
    vec2 uv = gl_FragCoord.xy / uResolution;
    vec2 centerL = vec2(0.32, 0.5);
    vec2 centerR = vec2(0.68, 0.5);

    float distL = distance(uv, centerL);
    float distR = distance(uv, centerR);

    float envL = clamp(uEnvelopes.x, 0.0, 1.0);
    float envR = clamp(uEnvelopes.y, 0.0, 1.0);

    // Phase1立ち上がり演出: 時間経過で徐々に波が強く、速くなる
    float riseTime = min(uTime * 0.15, 1.0);  // 最初の6-7秒で立ち上がる
    float riseFactor = smoothstep(0.0, 1.0, riseTime);

    // エンベロープに応じて波の速度と周波数を変化
    // 立ち上がり演出を組み込む
    float speedL = 0.22 * (0.3 + 0.7 * riseFactor) * (1.0 + envL * 0.8);
    float speedR = 0.22 * (0.3 + 0.7 * riseFactor) * (1.0 + envR * 0.8);
    float freqL = 32.0 * (0.5 + 0.5 * riseFactor) * (1.0 + envL * 0.5);
    float freqR = 32.0 * (0.5 + 0.5 * riseFactor) * (1.0 + envR * 0.5);

    // 複雑な波形を生成
    float waveL = multiWave(distL, uTime, speedL, freqL, 0.0);
    float waveR = multiWave(distR, uTime, speedR, freqR, 1.57);

    // より滑らかな減衰曲線
    float decayL = exp(-distL * 3.2);
    float decayR = exp(-distR * 3.2);

    // エッジでの自然なフェード
    float fadeEdgeL = smoothstep(0.85, 0.0, distL);
    float fadeEdgeR = smoothstep(0.85, 0.0, distR);

    // 中心部のグロー効果
    float glowL = generateGlow(distL, 0.15 + envL * 0.12);
    float glowR = generateGlow(distR, 0.15 + envR * 0.12);

    // 波の強度計算（より広いダイナミックレンジ）
    float waveIntensityL = waveL * 0.5 + 0.5;
    float waveIntensityR = waveR * 0.5 + 0.5;

    // 最終的な強度（グローを加算）
    float intensityL = clamp(
        (waveIntensityL * decayL * fadeEdgeL + glowL * 0.8) * mix(0.15, 1.0, envL),
        0.0, 1.0
    );
    float intensityR = clamp(
        (waveIntensityR * decayR * fadeEdgeR + glowR * 0.8) * mix(0.15, 1.0, envR),
        0.0, 1.0
    );

    // より鮮やかで深みのある色彩（青系と紫ピンク系）
    vec3 colorL = mix(
        vec3(0.08, 0.12, 0.28),  // 暗い青
        vec3(0.2, 0.5, 0.95),     // 鮮やかな青
        pow(intensityL, 0.8)
    );
    // グロー部分に白みを加える
    colorL = mix(colorL, vec3(0.6, 0.8, 1.0), glowL * envL * 0.5);

    vec3 colorR = mix(
        vec3(0.18, 0.08, 0.25),  // 暗い紫
        vec3(0.95, 0.35, 0.6),   // 鮮やかなピンク
        pow(intensityR, 0.8)
    );
    // グロー部分に白みを加える
    colorR = mix(colorR, vec3(1.0, 0.7, 0.8), glowR * envR * 0.5);

    // 加算合成で重なり部分が明るくなる
    vec3 color = colorL * intensityL + colorR * intensityR;

    // 光の伝播を追加
    float lightWaveL = lightPropagation(distL, uTime, 0.5);
    float lightWaveR = lightPropagation(distR, uTime, 0.5);
    
    // 暖かい白色の光 (#FBF5E7 = RGB(251, 245, 231))
    vec3 warmGlow = vec3(251.0/255.0, 245.0/255.0, 231.0/255.0);
    vec3 lightContribution = warmGlow * (lightWaveL + lightWaveR) * 0.3;
    
    // 既存のcolorに光の寄与を加算
    color += lightContribution * envL * envR;

    // エンベロープに連動してアルファ値も動的に変化
    float alphaL = intensityL * mix(0.6, 1.0, envL);
    float alphaR = intensityR * mix(0.6, 1.0, envR);
    float alpha = clamp((alphaL + alphaR) * 0.85 * uAlpha, 0.0, 1.0);

    gl_FragColor = vec4(color, alpha);
}
