#version 120

void main() {
    vec2 uv = gl_TexCoord[0].xy * 2.0 - vec2(1.0);
    float dist = length(uv);
    float intensity = smoothstep(0.8, 0.0, dist);
    gl_FragColor = vec4(vec3(intensity), intensity);
}
