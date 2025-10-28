#version 120

uniform float uEnvelope;

varying vec4 vColor;

void main() {
    float mixFactor = clamp(uEnvelope, 0.0, 1.0);
    vec3 tinted = mix(gl_Color.rgb * 0.8, gl_Color.rgb * 1.3, mixFactor);
    vColor = vec4(tinted, gl_Color.a);
    gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;
}
