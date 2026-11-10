#include "aether/render/light.hpp"

#include <algorithm>
#include <cmath>

namespace aether::render {

math::Vec3 sky_radiance(const Environment& env, math::Vec3 dir) noexcept {
    // Keep in sync with sky_gradient() in shaders/common.glsl.
    const auto mix = [](math::Vec3 a, math::Vec3 b, float t) { return a * (1.0f - t) + b * t; };
    const float y = std::clamp(dir.y, -1.0f, 1.0f);
    if (y >= 0.0f) return mix(env.horizon_color, env.sky_color, std::sqrt(y));
    const float t = std::clamp(-y / 0.12f, 0.0f, 1.0f);
    return mix(env.horizon_color, env.ground_color, t * t * (3.0f - 2.0f * t));
}

math::Quat rotation_toward(math::Vec3 direction) noexcept {
    const float len = math::length(direction);
    if (!(len > 0)) return math::Quat{};
    const math::Vec3 to = direction * (1.0f / len);
    const math::Vec3 from{0, 0, -1};
    const float c = std::clamp(math::dot(from, to), -1.0f, 1.0f);
    if (c < -0.9999f) return math::Quat::from_axis_angle({0, 1, 0}, math::kPi);  // opposite: half turn
    const math::Vec3 axis = math::cross(from, to);
    if (math::length(axis) < 1e-6f) return math::Quat{};
    return math::Quat::from_axis_angle(math::normalize(axis), std::acos(c));
}

}  // namespace aether::render
