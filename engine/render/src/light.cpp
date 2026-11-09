#include "aether/render/light.hpp"

#include <algorithm>
#include <cmath>

namespace aether::render {

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
