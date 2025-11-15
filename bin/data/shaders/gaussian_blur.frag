#version 120

uniform sampler2D tex;
uniform vec2 direction;
uniform float blurSize;
uniform vec2 resolution;

const float weights[7] = float[](0.0205, 0.0855, 0.232, 0.324, 0.232, 0.0855, 0.0205);

void main() {
    vec2 uv = gl_TexCoord[0].xy;
    vec2 texelSize = 1.0 / resolution;
    vec4 color = vec4(0.0);
    for (int i = -6; i <= 6; ++i) {
        vec2 offset = direction * float(i) * blurSize * texelSize;
        float weight = weights[abs(i)];
        color += texture2D(tex, uv + offset) * weight;
    }
    gl_FragColor = color;
}
