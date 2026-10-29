#version 330
// RBS Engine - procedural sky (gradient + scrolling clouds + sun glow), drawn on "sky" faces
in vec2 fragTexCoord;
in vec2 fragLightCoord;
in vec3 fragPos;
in vec3 fragNormal;

uniform vec3 viewPos;
uniform float time;
uniform vec3 skyZenith;
uniform vec3 skyHorizon;
uniform vec3 skyClouds;
uniform vec3 sunDir;
uniform vec3 sunColor;

out vec4 finalColor;

float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float noise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash(i), hash(i + vec2(1, 0)), f.x), mix(hash(i + vec2(0, 1)), hash(i + vec2(1, 1)), f.x), f.y);
}
float fbm(vec2 p) {
    float v = 0.0, a = 0.5;
    for (int i = 0; i < 5; i++) { v += a * noise(p); p *= 2.03; a *= 0.5; }
    return v;
}

void main() {
    vec3 dir = normalize(fragPos - viewPos);
    float h = clamp(dir.y, -0.2, 1.0);
    vec3 col = mix(skyHorizon, skyZenith, pow(clamp(h, 0.0, 1.0), 0.6));
    if (dir.y > 0.0) {
        vec2 uv = dir.xz / (dir.y + 0.15) * 1.6;
        float c = fbm(uv + vec2(time * 0.02, time * 0.012));
        c = smoothstep(0.45, 0.85, c);
        col = mix(col, skyClouds, c * clamp(dir.y * 4.0, 0.0, 1.0) * 0.85);
    } else {
        col = mix(skyHorizon, skyHorizon * 0.5, clamp(-dir.y * 5.0, 0.0, 1.0));
    }
    vec3 sd = normalize(-sunDir);
    float s = max(dot(dir, sd), 0.0);
    col += sunColor * (pow(s, 300.0) * 2.0 + pow(s, 12.0) * 0.25);
    finalColor = vec4(col, 1.0);
}
