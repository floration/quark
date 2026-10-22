// Small, header-only math library. Y is up, units are meters.
#pragma once
#include <cmath>
#include <cstdint>
#include <algorithm>

constexpr float PI = 3.14159265358979f;
inline float deg2rad(float d) { return d * (PI / 180.0f); }
inline float clampf(float v, float lo, float hi) { return v >= lo ? (v > hi ? hi : v) : lo; } // (NaN -> lo)
inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }

struct Vec3 {
    float x = 0, y = 0, z = 0;
    Vec3() = default;
    constexpr Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
    Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
    Vec3 operator/(float s) const { return {x / s, y / s, z / s}; }
    Vec3 operator-() const { return {-x, -y, -z}; }
    Vec3& operator+=(const Vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }
    Vec3& operator-=(const Vec3& o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
    Vec3& operator*=(float s) { x *= s; y *= s; z *= s; return *this; }
    float& operator[](int i) { return (&x)[i]; }
    float operator[](int i) const { return (&x)[i]; }
};
inline float dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 cross(const Vec3& a, const Vec3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline float length(const Vec3& v) { return std::sqrt(dot(v, v)); }
inline Vec3 normalize(const Vec3& v) { float l = length(v); return l > 1e-8f ? v / l : Vec3(0, 0, 0); }
inline Vec3 vlerp(const Vec3& a, const Vec3& b, float t) { return a + (b - a) * t; }
inline Vec3 vmin(const Vec3& a, const Vec3& b) { return {std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z)}; }
inline Vec3 vmax(const Vec3& a, const Vec3& b) { return {std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z)}; }

struct AABB {
    Vec3 mn, mx;
    bool overlaps(const AABB& o) const {
        return mn.x < o.mx.x && mx.x > o.mn.x && mn.y < o.mx.y && mx.y > o.mn.y && mn.z < o.mx.z && mx.z > o.mn.z;
    }
    bool contains(const Vec3& p) const {
        return p.x >= mn.x && p.x <= mx.x && p.y >= mn.y && p.y <= mx.y && p.z >= mn.z && p.z <= mx.z;
    }
    Vec3 center() const { return (mn + mx) * 0.5f; }
};

// Ray vs AABB slab test. Returns entry distance in [0, maxT] or -1.
inline float rayAABB(const Vec3& o, const Vec3& invDir, const AABB& b, float maxT, int* hitAxis = nullptr) {
    float tmin = 0.0f, tmax = maxT;
    int axis = -1;
    for (int i = 0; i < 3; i++) {
        float t1 = (b.mn[i] - o[i]) * invDir[i];
        float t2 = (b.mx[i] - o[i]) * invDir[i];
        if (t1 > t2) std::swap(t1, t2);
        if (t1 > tmin) { tmin = t1; axis = i; }
        if (t2 < tmax) tmax = t2;
        if (tmin > tmax) return -1.0f;
    }
    if (hitAxis) *hitAxis = axis;
    return tmin;
}
inline Vec3 safeInv(const Vec3& d) {
    auto inv = [](float v) { return std::fabs(v) < 1e-12f ? 1e30f : 1.0f / v; };
    return {inv(d.x), inv(d.y), inv(d.z)};
}

// Column-major 4x4 matrix (OpenGL convention).
struct Mat4 {
    float m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    Mat4 operator*(const Mat4& o) const {
        Mat4 r;
        for (int c = 0; c < 4; c++)
            for (int rr = 0; rr < 4; rr++) {
                float s = 0;
                for (int k = 0; k < 4; k++) s += m[k * 4 + rr] * o.m[c * 4 + k];
                r.m[c * 4 + rr] = s;
            }
        return r;
    }
    static Mat4 perspective(float fovyRad, float aspect, float zn, float zf) {
        Mat4 r;
        float f = 1.0f / std::tan(fovyRad * 0.5f);
        r.m[0] = f / aspect; r.m[5] = f;
        r.m[10] = (zf + zn) / (zn - zf); r.m[11] = -1;
        r.m[14] = 2 * zf * zn / (zn - zf); r.m[15] = 0;
        return r;
    }
    static Mat4 ortho(float l, float r_, float b, float t, float n, float f) {
        Mat4 r;
        r.m[0] = 2 / (r_ - l); r.m[5] = 2 / (t - b); r.m[10] = -2 / (f - n);
        r.m[12] = -(r_ + l) / (r_ - l); r.m[13] = -(t + b) / (t - b); r.m[14] = -(f + n) / (f - n);
        return r;
    }
    static Mat4 lookAt(const Vec3& eye, const Vec3& at, const Vec3& up) {
        Vec3 f = normalize(at - eye), s = normalize(cross(f, up)), u = cross(s, f);
        Mat4 r;
        r.m[0] = s.x; r.m[4] = s.y; r.m[8] = s.z;
        r.m[1] = u.x; r.m[5] = u.y; r.m[9] = u.z;
        r.m[2] = -f.x; r.m[6] = -f.y; r.m[10] = -f.z;
        r.m[12] = -dot(s, eye); r.m[13] = -dot(u, eye); r.m[14] = dot(f, eye);
        return r;
    }
    static Mat4 translate(const Vec3& t) { Mat4 r; r.m[12] = t.x; r.m[13] = t.y; r.m[14] = t.z; return r; }
    static Mat4 rotY(float a) { Mat4 r; float c = std::cos(a), s = std::sin(a); r.m[0] = c; r.m[2] = -s; r.m[8] = s; r.m[10] = c; return r; }
    static Mat4 rotX(float a) { Mat4 r; float c = std::cos(a), s = std::sin(a); r.m[5] = c; r.m[6] = s; r.m[9] = -s; r.m[10] = c; return r; }
};

// View direction from yaw/pitch (radians). yaw=0 looks down -Z.
inline Vec3 angleForward(float yaw, float pitch) {
    return {-std::sin(yaw) * std::cos(pitch), std::sin(pitch), -std::cos(yaw) * std::cos(pitch)};
}
