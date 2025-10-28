#version 120

uniform vec2 uResolution;
uniform float uTime;
uniform float uEnvelope;
uniform float uAlpha;

void main() {
    vec2 uv = gl_FragCoord.xy / uResolution;
    vec2 center = vec2(0.5);
    float dist = distance(uv, center);

    // 波紋の位相: よりゆっくりとした伝播
    float wave = sin((dist - uTime * 0.18) * 35.0);

    // 減衰曲線: 中心から外側へのグラディエント強化
    // exp + smoothstep の組み合わせで滑らかな濃淡
    float decay = exp(-dist * 4.0);
    float fadeEdge = smoothstep(0.8, 0.0, dist);  // 外側を滑らかに消失

    float envelope = clamp(uEnvelope, 0.0, 1.0);

    // 波紋の明るさ: sin波を0-1に正規化し、減衰とenvelope適用
    float waveIntensity = wave * 0.5 + 0.5;
    float intensity = waveIntensity * decay * fadeEdge * mix(0.15, 0.85, envelope);
    intensity = clamp(intensity, 0.0, 1.0);

    // グラディエーションカラー: 中心(明るい青)→外側(暗い紫)
    vec3 innerColor = vec3(0.4, 0.55, 0.85);   // 明るい青
    vec3 outerColor = vec3(0.15, 0.22, 0.45);  // 暗い紫
    vec3 color = mix(outerColor, innerColor, intensity) * intensity;

    // アルファ: 濃淡を反映
    float alpha = clamp(intensity * uAlpha * 1.5, 0.0, 1.0);
    gl_FragColor = vec4(color, alpha);
}
