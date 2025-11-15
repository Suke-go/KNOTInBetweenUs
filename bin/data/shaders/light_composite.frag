#version 120

uniform sampler2D baseTex;
uniform sampler2D bloomTex;
uniform float bloomIntensity;
uniform float exposure;

vec3 toneMap(vec3 color) {
    return color / (vec3(1.0) + color);
}

void main() {
    vec2 uv = gl_TexCoord[0].xy;
    vec3 base = texture2D(baseTex, uv).rgb;
    vec3 bloom = texture2D(bloomTex, uv).rgb;
    vec3 color = base + bloom * bloomIntensity;
    color *= exposure;
    color = toneMap(color);
    color = pow(color, vec3(1.0 / 2.2));
    gl_FragColor = vec4(color, 1.0);
}
