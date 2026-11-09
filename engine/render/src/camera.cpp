#include "aether/render/camera.hpp"

#include <cmath>

namespace aether::render {

using math::Mat4;
using math::Vec3;

Mat4 look_at(const Vec3& eye, const Vec3& target, const Vec3& up) noexcept {
    const Vec3 f = math::normalize(target - eye);
    const Vec3 s = math::normalize(math::cross(f, up));
    const Vec3 u = math::cross(s, f);
    Mat4 m;
    m.m[0] = s.x;  m.m[4] = s.y;  m.m[8] = s.z;
    m.m[1] = u.x;  m.m[5] = u.y;  m.m[9] = u.z;
    m.m[2] = -f.x; m.m[6] = -f.y; m.m[10] = -f.z;
    m.m[3] = 0;    m.m[7] = 0;    m.m[11] = 0;
    m.m[12] = -math::dot(s, eye);
    m.m[13] = -math::dot(u, eye);
    m.m[14] = math::dot(f, eye);
    m.m[15] = 1;
    return m;
}

Mat4 perspective(float fov_y, float aspect, float near_plane, float far_plane) noexcept {
    const float f = 1.0f / std::tan(fov_y * 0.5f);
    Mat4 m;
    for (float& v : m.m) v = 0;
    m.m[0] = f / aspect;
    m.m[5] = -f;  // Vulkan NDC has +Y down; flip so world up is screen up
    m.m[10] = far_plane / (near_plane - far_plane);
    m.m[11] = -1;
    m.m[14] = near_plane * far_plane / (near_plane - far_plane);
    return m;
}

Mat4 view_projection(const Camera& c, float aspect) noexcept {
    return perspective(c.fov_y, aspect, c.near_plane, c.far_plane) * look_at(c.position, c.target, c.up);
}

Vec3 project_to_ndc(const Mat4& m, const Vec3& p) noexcept {
    const float x = m.at(0, 0) * p.x + m.at(1, 0) * p.y + m.at(2, 0) * p.z + m.at(3, 0);
    const float y = m.at(0, 1) * p.x + m.at(1, 1) * p.y + m.at(2, 1) * p.z + m.at(3, 1);
    const float z = m.at(0, 2) * p.x + m.at(1, 2) * p.y + m.at(2, 2) * p.z + m.at(3, 2);
    const float w = m.at(0, 3) * p.x + m.at(1, 3) * p.y + m.at(2, 3) * p.z + m.at(3, 3);
    return {x / w, y / w, z / w};
}

}  // namespace aether::render
