#version 330
// RBS Engine - world (brush) fragment shader: diffuse * baked lightmap + dynamic lights + fog
in vec2 fragTexCoord;
in vec2 fragLightCoord;
in vec3 fragPos;
in vec3 fragNormal;

uniform sampler2D texture0;   // diffuse
uniform sampler2D texture1;   // lightmap (stored at half intensity -> 2x overbright)
uniform vec4 colDiffuse;
uniform vec3 viewPos;
uniform vec3 fogColor;
uniform float fogDensity;
uniform vec2 uvScroll;
uniform float time;
uniform float lightScale;     // 2.0 normally, used by editor preview
uniform vec3 editorLight;     // >0: simple directional shading instead of lightmap (editor)

#define MAX_DLIGHTS 16
uniform vec4 dlPosRad[MAX_DLIGHTS];
uniform vec3 dlColor[MAX_DLIGHTS];
uniform int dlCount;

out vec4 finalColor;

void main() {
    vec4 tex = texture(texture0, fragTexCoord + uvScroll * time);
    if (tex.a < 0.5) discard;
    vec3 n = normalize(fragNormal);
    vec3 light;
    if (editorLight.x > 0.0) {
        light = vec3(0.55 + 0.45 * abs(dot(n, normalize(vec3(0.3, 0.8, 0.5)))));
    } else {
        light = texture(texture1, fragLightCoord).rgb * lightScale;
    }
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
    vec3 col = tex.rgb * colDiffuse.rgb * light;
    float dist = length(fragPos - viewPos);
    float fog = 1.0 - exp(-pow(fogDensity * dist, 2.0));
    col = mix(col, fogColor, clamp(fog, 0.0, 1.0));
    finalColor = vec4(col, 1.0);
}
