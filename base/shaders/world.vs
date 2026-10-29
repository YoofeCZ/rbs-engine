#version 330
// RBS Engine - world (brush) vertex shader
in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec2 vertexTexCoord2;
in vec3 vertexNormal;

uniform mat4 mvp;
uniform mat4 matModel;

out vec2 fragTexCoord;
out vec2 fragLightCoord;
out vec3 fragPos;
out vec3 fragNormal;

void main() {
    vec4 wp = matModel * vec4(vertexPosition, 1.0);
    fragPos = wp.xyz;
    fragNormal = normalize(mat3(matModel) * vertexNormal);
    fragTexCoord = vertexTexCoord;
    fragLightCoord = vertexTexCoord2;
    gl_Position = mvp * vec4(vertexPosition, 1.0);
}
