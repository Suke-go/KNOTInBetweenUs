#version 120

uniform vec2 uResolution;
uniform float uTime;
uniform float uEnvelope;
uniform float uAlpha;

float hash(vec2 p) {
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}

void main() {
    vec2 uv = gl_FragCoord.xy / uResolution;
    vec2 cell = floor(uv * 200.0);
    float h = hash(cell);
    float sparkle = pow(h, 40.0);
    float twinkle = 0.5 + 0.5 * sin(uTime * 2.2 + h * 31.0);
    float intensity = mix(0.18, 0.9, clamp(uEnvelope, 0.0, 1.0));
    float star = sparkle * twinkle * intensity;
    float background = 0.05;
    vec3 color = vec3(background) + vec3(0.35, 0.42, 0.55) * star;
    float alpha = clamp(uAlpha, 0.0, 1.0);
    gl_FragColor = vec4(color, alpha);
}
