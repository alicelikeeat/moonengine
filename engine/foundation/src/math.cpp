#include "aether/foundation/math.hpp"

#include <algorithm>

namespace aether::math {

bool inverse(const Mat4& mat, Mat4& out) noexcept {
    const float* m = mat.m;
    float inv[16];
    inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
    inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
    inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
    inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
    inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
    inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
    inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
    inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
    inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] + m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
    inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
    inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
    inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
    inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
    inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] + m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
    inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] - m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
    inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] + m[8] * m[1] * m[6] - m[8] * m[2] * m[5];

    const float det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
    if (std::fabs(det) < 1e-12f) return false;
    const float inv_det = 1.0f / det;
    for (int i = 0; i < 16; ++i) out.m[i] = inv[i] * inv_det;
    return true;
}

Vec3 transform_projective(const Mat4& m, const Vec3& p) noexcept {
    const float x = m.at(0, 0) * p.x + m.at(1, 0) * p.y + m.at(2, 0) * p.z + m.at(3, 0);
    const float y = m.at(0, 1) * p.x + m.at(1, 1) * p.y + m.at(2, 1) * p.z + m.at(3, 1);
    const float z = m.at(0, 2) * p.x + m.at(1, 2) * p.y + m.at(2, 2) * p.z + m.at(3, 2);
    const float w = m.at(0, 3) * p.x + m.at(1, 3) * p.y + m.at(2, 3) * p.z + m.at(3, 3);
    return {x / w, y / w, z / w};
}

Quat quat_from_rotation_matrix(const Mat4& m) noexcept {
    // r(row, col) of the rotation; column-major storage means at(col, row).
    const auto r = [&](int row, int col) { return m.at(static_cast<std::size_t>(col), static_cast<std::size_t>(row)); };
    const float trace = r(0, 0) + r(1, 1) + r(2, 2);
    Quat q;
    if (trace > 0) {
        const float s = std::sqrt(trace + 1.0f) * 2.0f;
        q = {(r(2, 1) - r(1, 2)) / s, (r(0, 2) - r(2, 0)) / s, (r(1, 0) - r(0, 1)) / s, 0.25f * s};
    } else if (r(0, 0) > r(1, 1) && r(0, 0) > r(2, 2)) {
        const float s = std::sqrt(1.0f + r(0, 0) - r(1, 1) - r(2, 2)) * 2.0f;
        q = {0.25f * s, (r(0, 1) + r(1, 0)) / s, (r(0, 2) + r(2, 0)) / s, (r(2, 1) - r(1, 2)) / s};
    } else if (r(1, 1) > r(2, 2)) {
        const float s = std::sqrt(1.0f + r(1, 1) - r(0, 0) - r(2, 2)) * 2.0f;
        q = {(r(0, 1) + r(1, 0)) / s, 0.25f * s, (r(1, 2) + r(2, 1)) / s, (r(0, 2) - r(2, 0)) / s};
    } else {
        const float s = std::sqrt(1.0f + r(2, 2) - r(0, 0) - r(1, 1)) * 2.0f;
        q = {(r(0, 2) + r(2, 0)) / s, (r(1, 2) + r(2, 1)) / s, 0.25f * s, (r(1, 0) - r(0, 1)) / s};
    }
    return normalize(q);
}

void decompose(const Mat4& m, Vec3& translation, Quat& rotation, Vec3& scale) noexcept {
    translation = m.translation();
    Vec3 c0{m.m[0], m.m[1], m.m[2]}, c1{m.m[4], m.m[5], m.m[6]}, c2{m.m[8], m.m[9], m.m[10]};
    scale = {length(c0), length(c1), length(c2)};
    if (dot(c0, cross(c1, c2)) < 0) scale.x = -scale.x;  // mirrored: fold the sign into X
    Mat4 rot;
    const auto put = [&](std::size_t col, Vec3 v, float s) {
        const float k = s != 0 ? 1.0f / s : 0.0f;
        rot.m[col * 4 + 0] = v.x * k;
        rot.m[col * 4 + 1] = v.y * k;
        rot.m[col * 4 + 2] = v.z * k;
    };
    put(0, c0, scale.x);
    put(1, c1, scale.y);
    put(2, c2, scale.z);
    rotation = quat_from_rotation_matrix(rot);
}

namespace {
constexpr float kDegToRad = kPi / 180.0f;
constexpr float kRadToDeg = 180.0f / kPi;
}  // namespace

Quat quat_from_euler_degrees(const Vec3& e) noexcept {
    const Quat yaw = Quat::from_axis_angle({0, 1, 0}, e.y * kDegToRad);
    const Quat pitch = Quat::from_axis_angle({1, 0, 0}, e.x * kDegToRad);
    const Quat roll = Quat::from_axis_angle({0, 0, 1}, e.z * kDegToRad);
    return normalize(yaw * pitch * roll);
}

Vec3 euler_degrees_from_quat(const Quat& q) noexcept {
    // R = Ry(yaw) * Rx(pitch) * Rz(roll): R12 = -sin(pitch),
    // R02 / R22 give yaw, R10 / R11 give roll.
    const Mat4 m = Mat4::from_trs({}, normalize(q), {1, 1, 1});
    const auto r = [&](int row, int col) { return m.at(static_cast<std::size_t>(col), static_cast<std::size_t>(row)); };
    const float sp = std::clamp(-r(1, 2), -1.0f, 1.0f);
    const float pitch = std::asin(sp);
    float yaw, roll;
    if (std::fabs(sp) < 0.9999f) {
        yaw = std::atan2(r(0, 2), r(2, 2));
        roll = std::atan2(r(1, 0), r(1, 1));
    } else {  // gimbal lock: put everything into yaw
        yaw = std::atan2(-r(2, 0), r(0, 0));
        roll = 0;
    }
    return {pitch * kRadToDeg, yaw * kRadToDeg, roll * kRadToDeg};
}

}  // namespace aether::math
