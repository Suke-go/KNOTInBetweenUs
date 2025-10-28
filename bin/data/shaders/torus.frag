#version 120

uniform float uTime;
uniform float uAlpha;

varying vec4 vColor;

void main() {
    float pulse = 0.6 + 0.4 * sin(uTime * 1.4);
    vec3 color = vColor.rgb * pulse;
    float alpha = clamp(vColor.a * uAlpha, 0.0, 1.0);
    gl_FragColor = vec4(color, alpha);
}
