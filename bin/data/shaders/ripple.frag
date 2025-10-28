#version 120

uniform vec2 uResolution;
uniform float uTime;
uniform float uEnvelope;
uniform float uAlpha;

void main() {
    vec2 uv = gl_FragCoord.xy / uResolution;
    vec2 center = vec2(0.5);
    float dist = distance(uv, center);
    float wave = sin((dist - uTime * 0.25) * 40.0);
    float decay = exp(-dist * 5.5);
    float envelope = clamp(uEnvelope, 0.0, 1.0);
    float intensity = clamp((wave * 0.5 + 0.5) * decay * mix(0.25, 0.9, envelope), 0.0, 1.0);
    vec3 color = vec3(0.25, 0.32, 0.55) * intensity;
    float alpha = clamp(intensity * uAlpha, 0.0, 1.0);
    gl_FragColor = vec4(color, alpha);
}
