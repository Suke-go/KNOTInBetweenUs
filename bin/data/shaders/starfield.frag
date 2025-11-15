#version 120

uniform vec2 uResolution;
uniform float uTime;
uniform vec2 uEnvelopes;
uniform float uAlpha;
uniform float uIdleMode;

// ハッシュ関数（疑似乱数生成）
float hash(vec2 p) {
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}

// 2次元ハッシュベクトル
vec2 hash2(vec2 p) {
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(vec2(p.x * p.y, (p.x + p.y) * p.x));
}

// グローエフェクト（ガウス風）
float glow(float dist, float radius, float intensity) {
    return intensity * exp(-dist * dist / (radius * radius));
}

// Perlinノイズ風の補間ノイズ
float smoothNoise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);  // smoothstep interpolation

    float a = hash(i);
    float b = hash(i + vec2(1.0, 0.0));
    float c = hash(i + vec2(0.0, 1.0));
    float d = hash(i + vec2(1.0, 1.0));

    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

// 銀河の渦構造を生成
float spiralPattern(vec2 pos, float time, float arms) {
    float angle = atan(pos.y, pos.x);
    float radius = length(pos);

    // 対数螺旋の形状
    float spiral = sin(arms * angle - log(radius + 0.01) * 3.0 + time * 0.15) * 0.5 + 0.5;

    // 渦の強度を半径に応じて調整
    float radialFalloff = smoothstep(0.8, 0.0, radius);

    return spiral * radialFalloff;
}

void main() {
    vec2 uv = gl_FragCoord.xy / uResolution;
    vec2 center = vec2(0.5, 0.5);

    // 画面中心からの相対座標
    vec2 toCenter = center - uv;
    float distToCenter = length(toCenter);
    vec2 dirToCenter = normalize(toCenter);

    // Idleモード判定
    bool isIdleMode = uIdleMode > 0.5;
    
    // 左右のエンベロープに基づく全体の活性度
    float avgEnvelope = (uEnvelopes.x + uEnvelopes.y) * 0.5;
    float maxEnvelope = max(uEnvelopes.x, uEnvelopes.y);
    
    // Idleモード時は完全にエンベロープ値を無視し、時間ベースのみで美しい星空を表示
    // 音入力が弱くても確実に星空が見えるようにする
    if (isIdleMode) {
        // 時間ベースの美しい星空用の値（エンベロープ値は完全に無視）
        // 複数の周波数で動きを豊かに
        float timeBrightness1 = 0.7 + 0.3 * sin(uTime * 0.3);
        float timeBrightness2 = 0.1 * sin(uTime * 0.5);
        float timeBrightness3 = 0.05 * sin(uTime * 0.8);
        float timeBasedBrightness = timeBrightness1 + timeBrightness2 + timeBrightness3;
        // エンベロープ値は使用せず、時間ベースのみ
        avgEnvelope = clamp(timeBasedBrightness, 0.65, 1.05);
        maxEnvelope = clamp(timeBasedBrightness, 0.65, 1.05);
    }

    // パーティクル層1: 中心に向かって流れる星
    // Idleモード時は星の密度を上げる（解像度を上げる）
    float cell1Resolution = isIdleMode ? 900.0 : 600.0;
    // Idleモード時は流れをより滑らかに、時間ベースで動く
    float flowSpeed = isIdleMode ? 0.05 : 0.035;
    vec2 flowUV = uv + dirToCenter * uTime * flowSpeed * (1.0 + (isIdleMode ? 0.5 : avgEnvelope * 0.8));
    // Idleモード時は追加の回転的な動きを加える
    if (isIdleMode) {
        float rotation = uTime * 0.02;
        float cosRot = cos(rotation);
        float sinRot = sin(rotation);
        vec2 rotatedOffset = vec2(
            (flowUV.x - 0.5) * cosRot - (flowUV.y - 0.5) * sinRot,
            (flowUV.x - 0.5) * sinRot + (flowUV.y - 0.5) * cosRot
        );
        flowUV = rotatedOffset + 0.5;
    }
    vec2 cell1 = floor(flowUV * cell1Resolution);
    float h1 = hash(cell1);
    // Idleモード時は星をより明るく、より多く表示（指数を下げてより多くの星を表示）
    float sparkle1Pow = isIdleMode ? 45.0 : 55.0;
    float sparkle1 = pow(h1, sparkle1Pow);

    // 中心に近づくほど明るく大きく
    float centralBoost = smoothstep(0.7, 0.15, distToCenter);

    // 拍動による星のパルス（左右の心拍で異なる周波数）
    float pulseL = 0.5 + 0.5 * sin(uTime * 3.5 + h1 * 6.28);
    float pulseR = 0.5 + 0.5 * sin(uTime * 3.8 + h1 * 6.28 + 1.57);
    float pulse = mix(pulseL, pulseR, uv.x);

    // エンベロープ連動の強度計算
    // Idleモード時は時間ベースで美しく、エンベロープによる追加の反応は控えめに
    float minModulation = isIdleMode ? 0.9 : 0.2;
    float envelopeModulation = isIdleMode ? minModulation + (1.0 - minModulation) * avgEnvelope * 0.2 : mix(minModulation, 1.0, avgEnvelope);
    float star1Intensity = sparkle1 * pulse * envelopeModulation * (1.0 + centralBoost * (isIdleMode ? 2.5 : 2.0));

    // パーティクル層2: ゆっくり回転する背景の星々
    // Idleモード時は回転速度を少し上げて動きを感じやすく
    float rotationSpeed = isIdleMode ? 0.12 : 0.08;
    float rotation = uTime * rotationSpeed;
    float c = cos(rotation);
    float s = sin(rotation);
    vec2 rotatedUV = vec2(
        (uv.x - 0.5) * c - (uv.y - 0.5) * s + 0.5,
        (uv.x - 0.5) * s + (uv.y - 0.5) * c + 0.5
    );
    // Idleモード時は星の密度を上げる
    float cell2Resolution = isIdleMode ? 650.0 : 400.0;
    vec2 cell2 = floor(rotatedUV * cell2Resolution);
    float h2 = hash(cell2);
    // Idleモード時は星をより明るく、より多く表示
    float sparkle2Pow = isIdleMode ? 60.0 : 70.0;
    float sparkle2 = pow(h2, sparkle2Pow);

    // 瞬き - Idleモード時はより強く、より明るく、時間ベースで美しく
    float twinkleMin = isIdleMode ? 0.7 : 0.4;
    float twinkleMax = isIdleMode ? 1.2 : 0.6;
    // Idleモード時は複数の周波数で瞬きを重ねて、より豊かな動きに
    float twinkleSpeed1 = isIdleMode ? 2.5 : 1.5;
    float twinkleSpeed2 = isIdleMode ? 3.8 : 0.0;  // Idleモード時のみ追加の周波数
    float twinkle1 = sin(uTime * twinkleSpeed1 + h2 * 20.0);
    float twinkle2 = isIdleMode ? sin(uTime * twinkleSpeed2 + h2 * 15.0) * 0.3 : 0.0;
    float twinkle = twinkleMin + (twinkleMax - twinkleMin) * (twinkle1 * 0.7 + twinkle2 + 0.3);
    // Idleモード時は常に美しく見える強度（エンベロープに依存しない）
    float star2MinIntensity = isIdleMode ? 0.8 : 0.3;
    float star2MaxIntensity = isIdleMode ? 1.2 : 0.8;
    float star2Intensity = sparkle2 * twinkle * (isIdleMode ? star2MinIntensity : mix(star2MinIntensity, star2MaxIntensity, avgEnvelope));

    // 銀河の渦構造
    vec2 galaxyPos = (uv - center) * 2.0;
    // Idleモード時は渦の動きをより滑らかに
    float spiralTime = isIdleMode ? uTime * 0.2 : uTime * 0.15;
    float spiral = spiralPattern(galaxyPos, spiralTime, 3.0);
    // Idleモード時は常に美しいグローを表示（時間ベースで変動）
    float spiralMinGlow = isIdleMode ? 0.35 : 0.15;
    float spiralMaxGlow = isIdleMode ? 0.75 : 0.55;
    float spiralGlowBase = isIdleMode ? 0.5 + 0.25 * sin(uTime * 0.4) : maxEnvelope;
    float spiralGlow = spiral * mix(spiralMinGlow, spiralMaxGlow, spiralGlowBase);

    // 中心部の明るいコア（拍動で大きさが変わる）
    // Idleモード時は時間ベースで美しく脈動
    float coreRadiusBase = isIdleMode ? 0.1 : 0.08;
    float coreRadiusVariation = isIdleMode ? 0.04 : 0.06;
    float corePulse = isIdleMode ? 0.8 + 0.2 * sin(uTime * 1.5) : maxEnvelope;
    float coreRadius = coreRadiusBase + coreRadiusVariation * corePulse;
    float coreGlowIntensity = isIdleMode ? 1.4 : 1.2;
    float coreGlow = glow(distToCenter, coreRadius, coreGlowIntensity) * (0.8 + 0.2 * sin(uTime * (isIdleMode ? 1.5 : 2.0)));

    // 中心からの光の放射（ブルーム効果）
    // Idleモード時は時間ベースで美しく光る
    float radialGlowMin = isIdleMode ? 0.8 : 0.5;
    float radialGlowBase = isIdleMode ? 0.85 + 0.15 * sin(uTime * 0.5) : avgEnvelope;
    float radialGlow = glow(distToCenter, 0.4, 0.35) * (radialGlowMin + (1.0 - radialGlowMin) * radialGlowBase);

    // パーティクルの色を中心からの距離とエンベロープで変化
    // 中心に近い: 白っぽい、外側: 青〜紫
    vec3 innerColor = vec3(0.9, 0.95, 1.0);   // 白っぽい
    vec3 midColor = vec3(0.4, 0.7, 1.0);      // 青
    vec3 outerColor = vec3(0.6, 0.4, 0.9);    // 紫

    float colorMix = smoothstep(0.0, 0.6, distToCenter);
    vec3 particleColor = mix(
        mix(innerColor, midColor, colorMix),
        outerColor,
        smoothstep(0.6, 1.0, distToCenter)
    );

    // 星のカラー計算
    vec3 stars = particleColor * (star1Intensity + star2Intensity * 0.6);

    // 銀河の渦のカラー（青紫系）
    vec3 spiralColor = mix(
        vec3(0.2, 0.3, 0.6),
        vec3(0.5, 0.3, 0.7),
        spiral
    ) * spiralGlow;

    // 中心コアのカラー（明るい白青）
    vec3 coreColor = vec3(0.8, 0.9, 1.0) * coreGlow;

    // 放射状のグローカラー（淡い青）
    vec3 radialColor = vec3(0.3, 0.5, 0.8) * radialGlow;

    // Phase3用: 全体的なノイズエフェクト（有機的な揺らぎ）
    float noiseScale = isIdleMode ? 2.5 : 3.0;
    float noiseSpeed = isIdleMode ? 0.08 : 0.1;
    float noiseLayer1 = smoothNoise(uv * noiseScale + uTime * noiseSpeed);
    float noiseLayer2 = smoothNoise(uv * noiseScale * 2.0 + uTime * (noiseSpeed * 1.5) + 100.0);
    float combinedNoise = (noiseLayer1 * 0.7 + noiseLayer2 * 0.3);

    // Idleモード時は時間ベースで美しいノイズ、エンベロープによる追加の反応は控えめに
    float noiseIntensityBase = isIdleMode ? 0.08 : 0.0;
    float noiseIntensityModulation = isIdleMode ? noiseIntensityBase + maxEnvelope * 0.04 : maxEnvelope * 0.12;
    float noiseIntensity = noiseIntensityModulation * combinedNoise;

    // パーティクル風の動き: ノイズに基づいた流れ
    // Idleモード時は時間ベースで美しく動く
    float flowNoiseSpeed = isIdleMode ? 0.06 : 0.08;
    float flowIntensity = isIdleMode ? 0.02 + avgEnvelope * 0.01 : 0.03 * avgEnvelope;
    vec2 flowOffset = vec2(
        smoothNoise(uv * 2.0 + uTime * flowNoiseSpeed),
        smoothNoise(uv * 2.0 + uTime * flowNoiseSpeed + 50.0)
    ) * flowIntensity;

    // 深い宇宙の背景（中心に向かってわずかに明るく）
    // Idleモード時は背景をやや明るく（深い青系に調整）、時間ベースで微かに変動
    vec3 baseBackground = isIdleMode ? vec3(0.025, 0.035, 0.06) : vec3(0.01, 0.015, 0.03);
    float backgroundVariation = isIdleMode ? 1.0 + 0.15 * sin(uTime * 0.2) : 1.0;
    float backgroundBoost = isIdleMode ? 0.9 : 0.5;
    vec3 background = baseBackground * backgroundVariation * (1.0 + centralBoost * backgroundBoost);

    // すべてを加算合成
    vec3 color = background + stars + spiralColor + coreColor + radialColor;

    // ノイズをカラーに加算（青紫系のノイズ）
    vec3 noiseColor = mix(
        vec3(0.15, 0.2, 0.35),
        vec3(0.3, 0.15, 0.4),
        noiseLayer1
    ) * noiseIntensity;
    color += noiseColor;

    // 拍動で全体の明るさを変動させる（サブトル）
    // Idleモード時は時間ベースで美しく、エンベロープによる追加の反応は控えめに
    float pulseModulationBase = isIdleMode ? 0.1 : 0.25;
    float pulseModulation = isIdleMode ? pulseModulationBase + 0.05 * maxEnvelope : pulseModulationBase * maxEnvelope;
    float pulseFrequency = isIdleMode ? 1.8 : 2.5;
    color *= 1.0 + pulseModulation * sin(uTime * pulseFrequency);

    // 最終的なアルファ値
    float totalBrightness = (star1Intensity + star2Intensity + spiralGlow + coreGlow + radialGlow);
    float alpha = clamp(mix(0.85, 1.0, totalBrightness) * uAlpha, 0.0, 1.0);

    gl_FragColor = vec4(color, alpha);
}
