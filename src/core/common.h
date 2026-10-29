// RBS Engine - shared includes and small math helpers.
#pragma once

#include <raylib.h>
#include <raymath.h>

#include <cstdint>
#include <cmath>
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <algorithm>

namespace rbs {

constexpr float kPi = 3.14159265358979f;

inline Vector3 V3(float x, float y, float z) { return Vector3{x, y, z}; }
inline Vector3 operator+(Vector3 a, Vector3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vector3 operator-(Vector3 a, Vector3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vector3 operator-(Vector3 a) { return {-a.x, -a.y, -a.z}; }
inline Vector3 operator*(Vector3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
inline Vector3 operator*(float s, Vector3 a) { return {a.x * s, a.y * s, a.z * s}; }
inline Vector3 operator/(Vector3 a, float s) { return {a.x / s, a.y / s, a.z / s}; }
inline Vector3& operator+=(Vector3& a, Vector3 b) { a = a + b; return a; }
inline Vector3& operator-=(Vector3& a, Vector3 b) { a = a - b; return a; }
inline Vector3& operator*=(Vector3& a, float s) { a = a * s; return a; }
inline float Dot(Vector3 a, Vector3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vector3 Cross(Vector3 a, Vector3 b) { return Vector3CrossProduct(a, b); }
inline float Len(Vector3 a) { return sqrtf(Dot(a, a)); }
inline Vector3 Norm(Vector3 a) { float l = Len(a); return l > 1e-8f ? a / l : Vector3{0, 0, 0}; }
inline Vector3 Mul(Vector3 a, Vector3 b) { return {a.x * b.x, a.y * b.y, a.z * b.z}; }
inline Vector3 VMin(Vector3 a, Vector3 b) { return {fminf(a.x, b.x), fminf(a.y, b.y), fminf(a.z, b.z)}; }
inline Vector3 VMax(Vector3 a, Vector3 b) { return {fmaxf(a.x, b.x), fmaxf(a.y, b.y), fmaxf(a.z, b.z)}; }
inline float Comp(Vector3 v, int axis) { return axis == 0 ? v.x : (axis == 1 ? v.y : v.z); }
inline void SetComp(Vector3& v, int axis, float f) { if (axis == 0) v.x = f; else if (axis == 1) v.y = f; else v.z = f; }
inline float Clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline float Lerpf(float a, float b, float t) { return a + (b - a) * t; }
inline float Snap(float v, float grid) { return grid > 0 ? roundf(v / grid) * grid : v; }
inline float RandF() { return (float)GetRandomValue(0, 1000000) / 1000000.0f; }
inline float RandRange(float a, float b) { return a + (b - a) * RandF(); }
inline Vector3 RandDir() {
    for (;;) {
        Vector3 v{RandRange(-1, 1), RandRange(-1, 1), RandRange(-1, 1)};
        float l = Dot(v, v);
        if (l > 0.0001f && l <= 1.0f) return v / sqrtf(l);
    }
}

// Angles are in degrees: pitch (up positive), yaw (0 = +X, 90 = -Z, counter-clockwise seen from above).
inline Vector3 AnglesForward(float pitch, float yaw) {
    float p = pitch * DEG2RAD, y = yaw * DEG2RAD;
    return {cosf(p) * cosf(y), sinf(p), -cosf(p) * sinf(y)};
}
inline Vector3 YawRight(float yaw) {
    float y = yaw * DEG2RAD;
    return {sinf(y), 0, cosf(y)};
}
inline float YawFromDir(Vector3 d) { return atan2f(-d.z, d.x) * RAD2DEG; }

inline Color ColorFromVec(Vector3 c, float a = 1.0f) {
    return Color{(unsigned char)Clampf(c.x * 255.0f, 0, 255), (unsigned char)Clampf(c.y * 255.0f, 0, 255),
                 (unsigned char)Clampf(c.z * 255.0f, 0, 255), (unsigned char)Clampf(a * 255.0f, 0, 255)};
}
inline Vector3 VecFromColor(Color c) { return {c.r / 255.0f, c.g / 255.0f, c.b / 255.0f}; }

std::string StrFormat(const char* fmt, ...);
std::string ToLower(std::string s);
std::vector<std::string> SplitWords(const std::string& s);
bool StartsWith(const std::string& s, const std::string& prefix);
bool EndsWith(const std::string& s, const std::string& suffix);

}  // namespace rbs
