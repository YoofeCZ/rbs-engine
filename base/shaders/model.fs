#version 330
// RBS Engine - model fragment shader: ambient + one directional light + dynamic lights + fog
in vec2 fragTexCoord;
in vec4 fragColor;
in vec3 fragPos;
in vec3 fragNormal;

uniform sampler2D texture0;
uniform vec4 colDiffuse;
uniform vec3 viewPos;
uniform vec3 fogColor;
uniform float fogDensity;
uniform vec3 ambientColor;
uniform vec3 lightColor;
uniform vec3 lightDir;
uniform vec4 flashColor;      // rgb + amount (hit flash)
uniform float fullbright;

#define MAX_DLIGHTS 16
uniform vec4 dlPosRad[MAX_DLIGHTS];
uniform vec3 dlColor[MAX_DLIGHTS];
uniform int dlCount;

out vec4 finalColor;

void main() {
    vec4 tex = texture(texture0, fragTexCoord) * colDiffuse * fragColor;
    if (tex.a < 0.5) discard;
    vec3 n = normalize(fragNormal);
    vec3 light = ambientColor + lightColor * clamp(dot(n, lightDir) * 0.8 + 0.2, 0.0, 1.0);
    for (int i = 0; i < MAX_DLIGHTS; i++) {
        if (i >= dlCount) break;
        vec3 L = dlPosRad[i].xyz - fragPos;
        float d = length(L);
        float r = dlPosRad[i].w;
        if (d < r) {
            float a = 1.0 - d / r;
            light += dlColor[i] * (a * a) * clamp(dot(n, L / max(d, 0.001)) * 0.7 + 0.3, 0.0, 1.0);
        }
    }
    light = mix(light, vec3(1.0), fullbright);
    vec3 col = tex.rgb * light;
    col = mix(col, flashColor.rgb, flashColor.a);
    float dist = length(fragPos - viewPos);
    float fog = 1.0 - exp(-pow(fogDensity * dist, 2.0));
    col = mix(col, fogColor, clamp(fog, 0.0, 1.0));
    finalColor = vec4(col, tex.a);
}
