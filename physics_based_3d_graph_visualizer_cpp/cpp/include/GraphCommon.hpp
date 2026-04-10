#pragma once

#include <array>
#include <cmath>

namespace graph {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kDegToRad = kPi / 180.0f;

struct Vec2i {
    int x = 0;
    int y = 0;
};

struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct Mat4 {
    std::array<float, 16> v{};
};

inline Vec3 operator+(const Vec3& a, const Vec3& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3 operator-(const Vec3& a, const Vec3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec3 operator*(const Vec3& v, float s) { return {v.x * s, v.y * s, v.z * s}; }
inline Vec3 operator/(const Vec3& v, float s) { return {v.x / s, v.y / s, v.z / s}; }
inline Vec3& operator+=(Vec3& a, const Vec3& b) { a.x += b.x; a.y += b.y; a.z += b.z; return a; }
inline Vec3& operator-=(Vec3& a, const Vec3& b) { a.x -= b.x; a.y -= b.y; a.z -= b.z; return a; }
inline Vec3& operator*=(Vec3& v, float s) { v.x *= s; v.y *= s; v.z *= s; return v; }

inline float Dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

inline Vec3 Cross(const Vec3& a, const Vec3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

inline float LengthSquared(const Vec3& v) { return Dot(v, v); }
inline float Length(const Vec3& v) { return std::sqrt(LengthSquared(v)); }

inline Vec3 Normalize(const Vec3& v) {
    const float len = Length(v);
    if (len <= 1e-8f) {
        return {0.0f, 0.0f, 0.0f};
    }
    return v / len;
}

inline Mat4 IdentityMat4() {
    Mat4 out{};
    out.v[0] = 1.0f;
    out.v[5] = 1.0f;
    out.v[10] = 1.0f;
    out.v[15] = 1.0f;
    return out;
}

inline Mat4 Perspective(float fovy_rad, float aspect, float z_near, float z_far) {
    Mat4 out{};
    const float tan_half = std::tan(0.5f * fovy_rad);
    out.v[0] = 1.0f / (aspect * tan_half);
    out.v[5] = 1.0f / tan_half;
    out.v[10] = -(z_far + z_near) / (z_far - z_near);
    out.v[11] = -1.0f;
    out.v[14] = -(2.0f * z_far * z_near) / (z_far - z_near);
    return out;
}

inline Mat4 LookAt(const Vec3& eye, const Vec3& center, const Vec3& up) {
    const Vec3 f = Normalize(center - eye);
    const Vec3 s = Normalize(Cross(f, up));
    const Vec3 u = Cross(s, f);

    Mat4 out = IdentityMat4();
    out.v[0] = s.x;
    out.v[4] = s.y;
    out.v[8] = s.z;
    out.v[1] = u.x;
    out.v[5] = u.y;
    out.v[9] = u.z;
    out.v[2] = -f.x;
    out.v[6] = -f.y;
    out.v[10] = -f.z;
    out.v[12] = -Dot(s, eye);
    out.v[13] = -Dot(u, eye);
    out.v[14] = Dot(f, eye);
    return out;
}

}  // namespace graph
