// Camera and projection math for the renderer.
//
// Clip space follows Vulkan: depth in [0, 1], and the projection flips Y so
// that world +Y (up) appears at the top of the screen.
#pragma once

#include "aether/foundation/math.hpp"

namespace aether::render {

struct Camera {
    math::Vec3 position{0, 0, 5};
    math::Vec3 target{0, 0, 0};
    math::Vec3 up{0, 1, 0};
    float fov_y = math::kPi / 3;  // 60 degrees, vertical
    float near_plane = 0.1f;
    float far_plane = 1000.0f;
};

// Right-handed view matrix: camera looks down its local -Z.
[[nodiscard]] math::Mat4 look_at(const math::Vec3& eye, const math::Vec3& target, const math::Vec3& up) noexcept;
// Perspective projection to Vulkan clip space.
[[nodiscard]] math::Mat4 perspective(float fov_y, float aspect, float near_plane, float far_plane) noexcept;
[[nodiscard]] math::Mat4 view_projection(const Camera& camera, float aspect) noexcept;
// Applies `m` and the perspective divide. Returns NDC: x right, y down, z depth.
[[nodiscard]] math::Vec3 project_to_ndc(const math::Mat4& m, const math::Vec3& p) noexcept;

}  // namespace aether::render
