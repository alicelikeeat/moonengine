#include "aether/editor/editor_camera.hpp"

#include <algorithm>
#include <cmath>

namespace aether::editor {

namespace {
constexpr float kDegToRad = math::kPi / 180.0f;
}

math::Vec3 EditorCamera::forward() const noexcept {
    const float y = yaw * kDegToRad, p = pitch * kDegToRad;
    return {std::sin(y) * std::cos(p), std::sin(p), -std::cos(y) * std::cos(p)};
}

math::Vec3 EditorCamera::right() const noexcept { return math::normalize(math::cross(forward(), {0, 1, 0})); }
math::Vec3 EditorCamera::up() const noexcept { return math::cross(right(), forward()); }

render::Camera EditorCamera::to_render_camera() const noexcept {
    render::Camera c;
    c.position = position();
    c.target = c.position + forward();
    c.up = {0, 1, 0};
    c.fov_y = fov_y;
    c.near_plane = 0.05f;
    c.far_plane = 2000.0f;
    return c;
}

void EditorCamera::orbit(float dx, float dy, float k) noexcept {
    yaw += dx * k;
    pitch = std::clamp(pitch - dy * k, -89.0f, 89.0f);
}

void EditorCamera::look(float dx, float dy, float k) noexcept {
    const math::Vec3 eye = position();
    yaw += dx * k;
    pitch = std::clamp(pitch - dy * k, -89.0f, 89.0f);
    focus = eye + forward() * distance;  // keep the eye still
}

void EditorCamera::pan(float dx, float dy, float viewport_height) noexcept {
    // World units per pixel at the focus distance.
    const float scale = 2.0f * distance * std::tan(fov_y * 0.5f) / std::max(1.0f, viewport_height);
    focus = focus - right() * (dx * scale) + up() * (dy * scale);
}

void EditorCamera::dolly(float wheel_steps) noexcept {
    distance = std::clamp(distance * std::pow(0.88f, wheel_steps), 0.2f, 5000.0f);
}

void EditorCamera::fly(const math::Vec3& move, float dt) noexcept {
    const math::Vec3 delta = (right() * move.x + math::Vec3{0, 1, 0} * move.y + forward() * move.z) * (fly_speed * dt);
    focus = focus + delta;
}

void EditorCamera::frame(const math::Vec3& center, float radius) noexcept {
    focus = center;
    distance = std::max(0.5f, radius / std::sin(fov_y * 0.5f) * 1.15f);
}

}  // namespace aether::editor
