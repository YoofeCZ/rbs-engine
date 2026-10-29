#version 330
// RBS Engine - post process: retro color quantization, dithering, damage flash, vignette
in vec2 fragTexCoord;
in vec4 fragColor;

uniform sampler2D texture0;
uniform vec2 resolution;
uniform float posterize;   // color levels per channel, 0 = off
uniform float dither;      // 0..1
uniform vec4 flash;        // rgb + amount
uniform float vignette;
uniform float gamma;
uniform float brightness;

out vec4 finalColor;

float bayer4(vec2 p) {
    int x = int(mod(p.x, 4.0)), y = int(mod(p.y, 4.0));
    int idx = x + y * 4;
    float m[16] = float[16](0., 8., 2., 10., 12., 4., 14., 6., 3., 11., 1., 9., 15., 7., 13., 5.);
    return m[idx] / 16.0 - 0.5;
}

void main() {
    vec3 col = texture(texture0, fragTexCoord).rgb;
    col *= brightness;
    col = pow(max(col, vec3(0.0)), vec3(1.0 / gamma));
    if (posterize > 0.5) {
        float d = bayer4(gl_FragCoord.xy) * dither;
        col = floor(col * posterize + 0.5 + d) / posterize;
    }
    vec2 uv = fragTexCoord - 0.5;
    col *= 1.0 - dot(uv, uv) * vignette;
    col = mix(col, flash.rgb, flash.a);
    finalColor = vec4(col, 1.0);
}
