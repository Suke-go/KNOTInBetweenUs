#version 120

uniform vec2 uResolution;
uniform float uTime;
uniform vec2 uEnvelopes;
uniform float uAlpha;

void main() {
    vec2 uv = gl_FragCoord.xy / uResolution;
    vec2 centerL = vec2(0.32, 0.5);
    vec2 centerR = vec2(0.68, 0.5);

    float distL = distance(uv, centerL);
    float distR = distance(uv, centerR);

    float waveL = sin((distL - uTime * 0.18) * 35.0);
    float waveR = sin((distR - uTime * 0.18) * 35.0);

    float decayL = exp(-distL * 4.0);
    float decayR = exp(-distR * 4.0);
    float fadeEdgeL = smoothstep(0.8, 0.0, distL);
    float fadeEdgeR = smoothstep(0.8, 0.0, distR);

    float envL = clamp(uEnvelopes.x, 0.0, 1.0);
    float envR = clamp(uEnvelopes.y, 0.0, 1.0);

    float waveIntensityL = waveL * 0.5 + 0.5;
    float waveIntensityR = waveR * 0.5 + 0.5;

    float intensityL = clamp(waveIntensityL * decayL * fadeEdgeL * mix(0.12, 0.9, envL), 0.0, 1.0);
    float intensityR = clamp(waveIntensityR * decayR * fadeEdgeR * mix(0.12, 0.9, envR), 0.0, 1.0);

    vec3 colorL = mix(vec3(0.12, 0.18, 0.35), vec3(0.38, 0.6, 0.95), intensityL);
    vec3 colorR = mix(vec3(0.2, 0.12, 0.38), vec3(0.95, 0.42, 0.65), intensityR);
    vec3 color = colorL * intensityL + colorR * intensityR;

    float alpha = clamp((intensityL + intensityR) * 0.75 * uAlpha, 0.0, 1.0);
    gl_FragColor = vec4(color, alpha);
}
