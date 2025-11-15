#version 120

uniform vec2 uResolution;
uniform float uTime;
uniform float uPhase1;          // 参加者1の心拍位相 (0.0-1.0)
uniform float uPhase2;          // 参加者2の心拍位相 (0.0-1.0)
uniform float uEnvelope1;       // 参加者1のエンベロープ
uniform float uEnvelope2;       // 参加者2のエンベロープ
uniform float uSyncLevel;       // 同期度 (0.0-1.0)
uniform vec2 uLight1;           // ライト1の位置 (正規化座標)
uniform vec2 uLight2;           // ライト2の位置 (正規化座標)
uniform float uLightIntensity1; // ライト1の強度
uniform float uLightIntensity2; // ライト2の強度
uniform float uAlpha;           // 透明度

// ノイズ関数
float hash(float n) {
    return fract(sin(n) * 43758.5453);
}

float hash(vec2 p) {
    float n = dot(p, vec2(127.1, 311.7));
    return fract(sin(n) * 43758.5453);
}

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

// フラクタルブラウニアンモーション（流動的な動き）
float fbm(vec2 p, int octaves) {
    float value = 0.0;
    float amplitude = 0.5;
    float frequency = 1.0;
    
    for (int i = 0; i < octaves; i++) {
        value += amplitude * noise(p * frequency);
        frequency *= 2.0;
        amplitude *= 0.5;
    }
    
    return value;
}

// 回転行列
mat2 rot(float angle) {
    float c = cos(angle);
    float s = sin(angle);
    return mat2(c, -s, s, c);
}

// 円のSDF
float sdCircle(vec2 p, float r) {
    return length(p) - r;
}

// メタボール風のSDF（複数の円の和）
float metaballSDF(vec2 p, vec2 center1, vec2 center2, float radius1, float radius2, float smoothness) {
    float d1 = sdCircle(p - center1, radius1);
    float d2 = sdCircle(p - center2, radius2);
    
    // 指数関数を使った滑らかな合成
    float k = smoothness;
    float h = clamp(0.5 + 0.5 * (d2 - d1) / k, 0.0, 1.0);
    float d = mix(d2, d1, h) - k * h * (1.0 - h);
    
    return d;
}

// アメーバ形状のSDF（メタボール + ノイズ変形）
float amoebaSDF(vec2 uv, float time) {
    // 中心位置
    vec2 center = vec2(0.5, 0.5);
    
    // 心拍による脈動
    float pulse1 = 0.5 + 0.5 * sin(uPhase1 * 6.28318);
    float pulse2 = 0.5 + 0.5 * sin(uPhase2 * 6.28318);
    
    // エンベロープに基づくサイズ
    float baseRadius1 = 0.15 + 0.1 * uEnvelope1;
    float baseRadius2 = 0.15 + 0.1 * uEnvelope2;
    
    // 同期度に応じて形状が統合される
    float syncFactor = uSyncLevel;
    
    // 2つのメタボールの中心（同期度に応じて近づく）
    vec2 center1 = center + vec2(-0.12 * (1.0 - syncFactor), 0.0);
    vec2 center2 = center + vec2(0.12 * (1.0 - syncFactor), 0.0);
    
    // 時間による流動的な変形
    float flowTime = time * 0.3;
    vec2 flow1 = vec2(
        fbm(center1 * 3.0 + vec2(flowTime * 0.5, flowTime * 0.7), 4) - 0.5,
        fbm(center1 * 3.0 + vec2(flowTime * 0.3, flowTime * 1.1), 4) - 0.5
    ) * 0.08;
    
    vec2 flow2 = vec2(
        fbm(center2 * 3.0 + vec2(flowTime * 0.6, flowTime * 0.8), 4) - 0.5,
        fbm(center2 * 3.0 + vec2(flowTime * 0.4, flowTime * 1.2), 4) - 0.5
    ) * 0.08;
    
    center1 += flow1;
    center2 += flow2;
    
    // 半径に脈動を適用
    float radius1 = baseRadius1 * (0.8 + 0.2 * pulse1);
    float radius2 = baseRadius2 * (0.8 + 0.2 * pulse2);
    
    // メタボールのSDF
    float metaball = metaballSDF(uv, center1, center2, radius1, radius2, 0.18);
    
    // ノイズによる有機的な変形（より流動的に）
    float noiseScale = 10.0;
    float noiseAmount = 0.035;
    vec2 noiseUV = uv * noiseScale + vec2(flowTime * 0.25, flowTime * 0.18);
    float noiseDistortion = fbm(noiseUV, 4) * noiseAmount;
    metaball += noiseDistortion;
    
    // 追加の有機的な変形（複数の小さなメタボールでアメーバのような突起を作る）
    float organicDetail = 0.0;
    for (int i = 0; i < 4; i++) {
        float angle = float(i) * 1.571 + flowTime * 0.4; // 90度間隔
        float dist = 0.12 + 0.06 * sin(flowTime * 0.8 + float(i) * 0.5);
        vec2 offset = vec2(cos(angle), sin(angle)) * dist;
        float smallRadius = 0.035 + 0.025 * sin(flowTime * 1.5 + float(i));
        
        // メインのメタボールからの距離を考慮
        float distToMain = length(uv - center);
        float influence = 1.0 - smoothstep(0.1, 0.25, distToMain);
        
        float smallSDF = length(uv - (center + offset)) - smallRadius;
        organicDetail += influence * 1.0 / (1.0 + abs(smallSDF) * 25.0);
    }
    organicDetail = organicDetail * 0.015 - 0.008;
    metaball += organicDetail;
    
    // 同期度が高いほど、形状がより滑らかになる
    float syncSmoothness = mix(0.0, 0.02, syncFactor);
    metaball = mix(metaball, smoothstep(-0.05, 0.05, metaball) * 0.1 - 0.05, syncSmoothness);
    
    return metaball;
}

// 距離ベースのライティング（フォンシェーディング風）
vec3 calculateLighting(vec2 uv, float sdf, vec2 normal, vec3 baseColor) {
    // ライト1の計算
    vec2 toLight1 = normalize(uLight1 - uv);
    float distToLight1 = length(uLight1 - uv);
    float light1Attenuation = 1.0 / (1.0 + distToLight1 * distToLight1 * 2.5);
    float light1Dot = max(0.0, dot(normal, toLight1));
    float light1Intensity = uLightIntensity1 * light1Attenuation * (0.3 + 0.7 * light1Dot);
    
    // ライト2の計算
    vec2 toLight2 = normalize(uLight2 - uv);
    float distToLight2 = length(uLight2 - uv);
    float light2Attenuation = 1.0 / (1.0 + distToLight2 * distToLight2 * 2.5);
    float light2Dot = max(0.0, dot(normal, toLight2));
    float light2Intensity = uLightIntensity2 * light2Attenuation * (0.3 + 0.7 * light2Dot);
    
    // 環境光（より明るく）
    float ambient = 0.25;
    
    // スペキュラ（輝き）- より強く
    vec2 viewDir = normalize(vec2(0.5, 0.5) - uv);
    vec2 halfDir1 = normalize(toLight1 + viewDir);
    vec2 halfDir2 = normalize(toLight2 + viewDir);
    float spec1 = pow(max(0.0, dot(normal, halfDir1)), 40.0) * light1Intensity * 1.2;
    float spec2 = pow(max(0.0, dot(normal, halfDir2)), 40.0) * light2Intensity * 1.2;
    
    // ライトの色（心拍に応じて変化、より鮮やかに）
    vec3 lightColor1 = vec3(1.0, 0.96, 0.92) + vec3(0.12, 0.06, 0.0) * sin(uPhase1 * 6.28318);
    vec3 lightColor2 = vec3(1.0, 0.92, 0.96) + vec3(0.06, 0.12, 0.0) * sin(uPhase2 * 6.28318);
    
    // 最終的な色
    vec3 litColor = baseColor * (ambient + light1Intensity * lightColor1 + light2Intensity * lightColor2);
    litColor += vec3(1.0, 1.0, 0.97) * (spec1 + spec2) * 0.6;
    
    return litColor;
}


void main() {
    // 正規化座標（0.0-1.0）
    vec2 uv = gl_FragCoord.xy / uResolution;
    
    // アスペクト比を考慮した正規化座標（正方形に変換）
    float aspect = uResolution.x / uResolution.y;
    vec2 coord = uv;
    if (aspect > 1.0) {
        // 横長: xを縮小
        coord.x = (coord.x - 0.5) / aspect + 0.5;
    } else {
        // 縦長: yを縮小
        coord.y = (coord.y - 0.5) * aspect + 0.5;
    }
    
    // SDFの計算（正規化座標を使用）
    float sdf = amoebaSDF(coord, uTime);
    
    // アンチエイリアシング（より滑らかなエッジ）
    float edgeWidth = 0.003;
    float alpha = 1.0 - smoothstep(-edgeWidth, edgeWidth, sdf);
    
    // 内部の距離による色の変化（深さ感）
    float depth = smoothstep(0.0, 0.12, -sdf);
    
    // ベースカラー（同期度に応じて変化）
    float hue = 0.55 + 0.18 * (1.0 - uSyncLevel);
    float saturation = 0.75 + 0.2 * uSyncLevel;
    float brightness = 0.65 + 0.3 * depth;
    
    // HSBからRGBへの変換（より効率的な実装）
    vec3 baseColor = vec3(0.0);
    float c = brightness * saturation;
    float x = c * (1.0 - abs(mod(hue * 6.0, 2.0) - 1.0));
    float m = brightness - c;
    
    if (hue < 1.0/6.0) baseColor = vec3(c, x, 0.0);
    else if (hue < 2.0/6.0) baseColor = vec3(x, c, 0.0);
    else if (hue < 3.0/6.0) baseColor = vec3(0.0, c, x);
    else if (hue < 4.0/6.0) baseColor = vec3(0.0, x, c);
    else if (hue < 5.0/6.0) baseColor = vec3(x, 0.0, c);
    else baseColor = vec3(c, 0.0, x);
    baseColor += vec3(m);
    
    // エンベロープに応じた色の変化（より明るく）
    float envMix = (uEnvelope1 + uEnvelope2) * 0.5;
    baseColor = mix(baseColor, vec3(1.0, 0.96, 0.92), envMix * 0.4);
    
    // 法線の計算（ライティング用）- 効率化のため、SDFを一度だけ計算
    vec2 eps = vec2(0.002, 0.0);
    float sdfX1 = amoebaSDF(coord + eps.xy, uTime);
    float sdfX2 = amoebaSDF(coord - eps.xy, uTime);
    float sdfY1 = amoebaSDF(coord + eps.yx, uTime);
    float sdfY2 = amoebaSDF(coord - eps.yx, uTime);
    vec2 normal = normalize(vec2(sdfX1 - sdfX2, sdfY1 - sdfY2));
    
    // ライティングの計算（正規化座標を使用）
    vec3 litColor = calculateLighting(coord, sdf, normal, baseColor);
    
    // グロー効果（エッジからの距離に基づく、より強く）
    float glowDist = abs(sdf);
    float glow = exp(-glowDist * 20.0) * 0.8;
    litColor += baseColor * glow * (1.0 + envMix * 0.5);
    
    // 追加の内側グロー（中心部の輝き）
    float innerGlow = exp(-max(0.0, -sdf) * 25.0) * 0.4;
    litColor += vec3(1.0, 0.98, 0.95) * innerGlow;
    
    // 最終的なアルファ値
    float finalAlpha = alpha * uAlpha;
    
    gl_FragColor = vec4(litColor, finalAlpha);
}

