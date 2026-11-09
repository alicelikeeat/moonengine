// Small 3D math library: Vec3, Quat (unit quaternion rotations) and Mat4.
//
// Conventions (also used by the renderer and file formats):
//  - Right-handed coordinates, Y up.
//  - Mat4 is column-major: m[column * 4 + row], matching GLSL/SPIR-V and glTF.
//  - Points are column vectors: world = M * local.
//  - Quat is (x, y, z, w) with w the scalar part, as in glTF.
//  - Composition: TRS = Translation * Rotation * Scale.
#pragma once

#include <cmath>
#include <cstddef>

namespace aether::math {

struct Vec3 {
    float x = 0, y = 0, z = 0;

    constexpr Vec3 operator+(const Vec3& o) const noexcept { return {x + o.x, y + o.y, z + o.z}; }
    constexpr Vec3 operator-(const Vec3& o) const noexcept { return {x - o.x, y - o.y, z - o.z}; }
    constexpr Vec3 operator*(float s) const noexcept { return {x * s, y * s, z * s}; }
    constexpr Vec3 operator*(const Vec3& o) const noexcept { return {x * o.x, y * o.y, z * o.z}; }
    constexpr Vec3 operator-() const noexcept { return {-x, -y, -z}; }
    constexpr bool operator==(const Vec3&) const noexcept = default;
};

[[nodiscard]] constexpr float dot(const Vec3& a, const Vec3& b) noexcept { return a.x * b.x + a.y * b.y + a.z * b.z; }
[[nodiscard]] constexpr Vec3 cross(const Vec3& a, const Vec3& b) noexcept {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
[[nodiscard]] inline float length(const Vec3& v) noexcept { return std::sqrt(dot(v, v)); }
[[nodiscard]] inline Vec3 normalize(const Vec3& v) noexcept {
    const float len = length(v);
    return len > 0 ? v * (1.0f / len) : Vec3{};
}

struct Quat {
    float x = 0, y = 0, z = 0, w = 1;

    [[nodiscard]] static constexpr Quat identity() noexcept { return {}; }
    // Rotation of `radians` around `axis` (normalised internally).
    [[nodiscard]] static Quat from_axis_angle(const Vec3& axis, float radians) noexcept {
        const Vec3 a = normalize(axis);
        const float s = std::sin(radians * 0.5f);
        return {a.x * s, a.y * s, a.z * s, std::cos(radians * 0.5f)};
    }

    // Hamilton product: (a * b) applies b first, then a.
    constexpr Quat operator*(const Quat& b) const noexcept {
        return {w * b.x + x * b.w + y * b.z - z * b.y,
                w * b.y - x * b.z + y * b.w + z * b.x,
                w * b.z + x * b.y - y * b.x + z * b.w,
                w * b.w - x * b.x - y * b.y - z * b.z};
    }
    constexpr bool operator==(const Quat&) const noexcept = default;
};

[[nodiscard]] inline Quat normalize(const Quat& q) noexcept {
    const float len = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    return len > 0 ? Quat{q.x / len, q.y / len, q.z / len, q.w / len} : Quat{};
}

[[nodiscard]] constexpr Vec3 rotate(const Quat& q, const Vec3& v) noexcept {
    const Vec3 u{q.x, q.y, q.z};
    const Vec3 t = cross(u, v) * 2.0f;
    return v + t * q.w + cross(u, t);
}

struct Mat4 {
    float m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

    [[nodiscard]] static constexpr Mat4 identity() noexcept { return {}; }
    [[nodiscard]] constexpr float& at(std::size_t col, std::size_t row) noexcept { return m[col * 4 + row]; }
    [[nodiscard]] constexpr float at(std::size_t col, std::size_t row) const noexcept { return m[col * 4 + row]; }

    [[nodiscard]] static constexpr Mat4 from_trs(const Vec3& t, const Quat& r, const Vec3& s) noexcept {
        const float xx = r.x * r.x, yy = r.y * r.y, zz = r.z * r.z;
        const float xy = r.x * r.y, xz = r.x * r.z, yz = r.y * r.z;
        const float wx = r.w * r.x, wy = r.w * r.y, wz = r.w * r.z;
        Mat4 out;
        out.m[0] = (1 - 2 * (yy + zz)) * s.x;
        out.m[1] = (2 * (xy + wz)) * s.x;
        out.m[2] = (2 * (xz - wy)) * s.x;
        out.m[3] = 0;
        out.m[4] = (2 * (xy - wz)) * s.y;
        out.m[5] = (1 - 2 * (xx + zz)) * s.y;
        out.m[6] = (2 * (yz + wx)) * s.y;
        out.m[7] = 0;
        out.m[8] = (2 * (xz + wy)) * s.z;
        out.m[9] = (2 * (yz - wx)) * s.z;
        out.m[10] = (1 - 2 * (xx + yy)) * s.z;
        out.m[11] = 0;
        out.m[12] = t.x;
        out.m[13] = t.y;
        out.m[14] = t.z;
        out.m[15] = 1;
        return out;
    }

    constexpr Mat4 operator*(const Mat4& b) const noexcept {
        Mat4 out;
        for (std::size_t c = 0; c < 4; ++c)
            for (std::size_t r = 0; r < 4; ++r) {
                float sum = 0;
                for (std::size_t k = 0; k < 4; ++k) sum += at(k, r) * b.at(c, k);
                out.at(c, r) = sum;
            }
        return out;
    }

    [[nodiscard]] constexpr Vec3 transform_point(const Vec3& p) const noexcept {
        return {at(0, 0) * p.x + at(1, 0) * p.y + at(2, 0) * p.z + at(3, 0),
                at(0, 1) * p.x + at(1, 1) * p.y + at(2, 1) * p.z + at(3, 1),
                at(0, 2) * p.x + at(1, 2) * p.y + at(2, 2) * p.z + at(3, 2)};
    }
    [[nodiscard]] constexpr Vec3 translation() const noexcept { return {m[12], m[13], m[14]}; }
};

[[nodiscard]] inline bool near(float a, float b, float eps = 1e-5f) noexcept { return std::fabs(a - b) <= eps; }
[[nodiscard]] inline bool near(const Vec3& a, const Vec3& b, float eps = 1e-5f) noexcept {
    return near(a.x, b.x, eps) && near(a.y, b.y, eps) && near(a.z, b.z, eps);
}
[[nodiscard]] inline bool near(const Mat4& a, const Mat4& b, float eps = 1e-5f) noexcept {
    for (std::size_t i = 0; i < 16; ++i)
        if (!near(a.m[i], b.m[i], eps)) return false;
    return true;
}

inline constexpr float kPi = 3.14159265358979323846f;

}  // namespace aether::math
