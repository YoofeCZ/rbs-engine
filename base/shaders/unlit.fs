#version 330
in vec2 fragTexCoord;
in vec4 fragColor;
in vec3 fragPos;

uniform sampler2D texture0;
uniform vec4 colDiffuse;
uniform vec3 viewPos;
uniform vec3 fogColor;
uniform float fogDensity;
uniform float additive;   // 1 = fog fades to black instead of fog color

out vec4 finalColor;

void main() {
    vec4 tex = texture(texture0, fragTexCoord) * fragColor * colDiffuse;
    if (tex.a < 0.02) discard;
    float dist = length(fragPos - viewPos);
    float fog = clamp(1.0 - exp(-pow(fogDensity * dist, 2.0)), 0.0, 1.0);
    vec3 col = mix(tex.rgb, mix(fogColor, vec3(0.0), additive), fog);
    finalColor = vec4(col, tex.a);
}
