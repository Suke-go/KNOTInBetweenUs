#version 120

uniform vec2 uPosition;      // 光の中心位置（画面座標、0.0-1.0）
uniform float uTime;          // 時間
uniform float uPhase;         // 心拍位相（0.0-1.0）
uniform float uAlpha;         // 透明度
uniform float uSizeScale;     // サイズスケール（成長因子）
uniform vec2 uResolution;     // 画面解像度

// ノイズ関数（簡易版 - ハッシュベース）
float hash(float n) {
    return fract(sin(n) * 43758.5453);
}

float hash(vec2 p) {
    float n = dot(p, vec2(127.1, 311.7));
    return fract(sin(n) * 43758.5453);
}

// 2Dノイズ関数
float noise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    
    float a = hash(i);
    float b = hash(i + vec2(1.0, 0.0));
    float c = hash(i + vec2(0.0, 1.0));
    float d = hash(i + vec2(1.0, 1.0));
    
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

// フラクタルノイズ（オーロラのような流動性）
float fbm(vec2 p) {
    float value = 0.0;
    float amplitude = 0.5;
    float frequency = 1.0;
    
    for (int i = 0; i < 4; i++) {
        value += amplitude * noise(p * frequency);
        frequency *= 2.0;
        amplitude *= 0.5;
    }
    
    return value;
}

void main() {
    // 画面座標を正規化（0.0-1.0）
    vec2 uv = gl_FragCoord.xy / uResolution;
    
    // 中心からの距離と方向
    vec2 toCenter = uv - uPosition;
    float dist = length(toCenter);
    
    // ゼロ除算を防ぐ（距離が非常に小さい場合はデフォルト方向を使用）
    vec2 dir = dist > 0.0001 ? normalize(toCenter) : vec2(1.0, 0.0);
    
    // 心拍による脈動
    float systole = smoothstep(0.0, 0.3, uPhase);
    float diastole = smoothstep(0.3, 1.0, uPhase);
    float pulse = 0.75 + 0.25 * (systole - diastole);
    
    // 基本サイズ（成長因子を適用）
    float baseRadius = 0.08 * uSizeScale;  // 画面幅の8%を基準
    
    // オーロラのような流動的な動き（時間的な変化）
    float timeFlow = uTime * 0.3;
    vec2 flowOffset = vec2(
        fbm(dir * 2.0 + vec2(timeFlow, timeFlow * 0.7)) - 0.5,
        fbm(dir * 2.0 + vec2(timeFlow * 0.5, timeFlow * 1.2)) - 0.5
    ) * 0.015;  // 流動性の強さ（画面幅の1.5%）
    
    // 距離による減衰（inverse square lawに近い）
    float normalizedDist = dist / baseRadius;
    
    // オーロラのような流動的な変形
    float flowDistortion = fbm((uv + flowOffset) * 10.0 + vec2(timeFlow * 0.1)) * 0.05;
    normalizedDist += flowDistortion;
    
    // コア部分（明るい中心）
    float coreRadius = 0.15 * baseRadius * pulse;
    float coreIntensity = 1.0 - smoothstep(0.0, coreRadius, dist);
    
    // 内側グロー（強い光）
    float innerRadius = 0.4 * baseRadius * pulse;
    float innerIntensity = exp(-dist * dist / (innerRadius * innerRadius * 0.5));
    
    // 中間グロー（柔らかい光）
    float midRadius = 0.7 * baseRadius * pulse;
    float midIntensity = exp(-dist * dist / (midRadius * midRadius * 2.0));
    
    // 外側グロー（拡散する光）
    float outerRadius = 1.2 * baseRadius * pulse;
    float outerIntensity = exp(-dist * dist / (outerRadius * outerRadius * 4.0));
    
    // 光の散乱シミュレーション（レイリー散乱風）
    float scatterFactor = 1.0 - smoothstep(0.3, 1.0, normalizedDist);
    float scatteredIntensity = midIntensity * scatterFactor * 0.3;
    
    // 最終的な光の強度
    float totalIntensity = coreIntensity * 1.0 +
                          innerIntensity * 0.8 +
                          midIntensity * 0.5 +
                          outerIntensity * 0.3 +
                          scatteredIntensity;
    
    // 距離による減衰（よりリアルな減衰カーブ）
    float distanceAttenuation = 1.0 / (1.0 + normalizedDist * normalizedDist * 2.0);
    totalIntensity *= distanceAttenuation;
    
    // 暖かい白色（電球の色）
    vec3 warmWhite = vec3(1.0, 0.96, 0.9);  // わずかに黄色がかった白
    vec3 lightColor = warmWhite * totalIntensity;
    
    // オーロラのような色の変化（微細な色の揺らぎ）
    float colorVariation = fbm(uv * 5.0 + vec2(timeFlow * 0.05)) * 0.1;
    lightColor += vec3(0.05, 0.03, 0.01) * colorVariation;
    
    // アルファ値
    float alpha = totalIntensity * uAlpha;
    
    gl_FragColor = vec4(lightColor, alpha);
}

