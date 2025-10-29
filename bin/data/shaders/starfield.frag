#version 120

uniform vec2 uResolution;
uniform float uTime;
uniform vec2 uEnvelopes;
uniform float uAlpha;

float hash(vec2 p) {
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}

void main() {
    vec2 uv = gl_FragCoord.xy / uResolution;

    // より細かいグリッド(800x800)で小さな星を生成
    vec2 cell = floor(uv * 800.0);
    float h = hash(cell);

    // sparkle閾値を上げて星の数を減らす(pow指数を40→80に)
    float sparkle = pow(h, 80.0);

    // 瞬き: ゆっくりとした周期で自然な明滅
    float twinkle = 0.3 + 0.7 * sin(uTime * 1.2 + h * 31.0);

    // Envelope連動: 心拍が強いと星が明るく
    float envelope = mix(uEnvelopes.x, uEnvelopes.y, clamp(uv.x, 0.0, 1.0));
    float intensity = mix(0.12, 0.65, clamp(envelope, 0.0, 1.0));

    // 星の明るさ: 小さく鋭い輝き
    float star = sparkle * twinkle * intensity;

    // 背景: 深い宇宙の黒
    float background = 0.02;

    // 星の色: やや青白い色調(アンビエント)
    vec3 starColor = vec3(0.65, 0.75, 0.95) * star;
    vec3 color = vec3(background) + starColor;

    float alpha = clamp(uAlpha, 0.0, 1.0);
    gl_FragColor = vec4(color, alpha);
}
