// Viewport camera with Unreal-style controls.
//
// The camera orbits a focus point: position = focus - forward * distance.
// Orbit (Alt+LMB) rotates around the focus, look (RMB) rotates in place,
// pan (MMB) slides the focus, dolly (wheel) changes the distance, fly
// (RMB + WASD/QE) moves both, and frame (F) fits a sphere in view.
#pragma once

#include "aether/foundation/math.hpp"
#include "aether/render/camera.hpp"

namespace aether::editor {

struct EditorCamera {
    math::Vec3 focus{0, 0, 0};
    float yaw = 0;      // degrees; 0 looks toward -Z, positive turns toward +X
    float pitch = -25;  // degrees; negative looks down
    float distance = 18;
    float fov_y = 0.9f;
    float fly_speed = 8.0f;  // m/s

    [[nodiscard]] math::Vec3 forward() const noexcept;
    [[nodiscard]] math::Vec3 right() const noexcept;
    [[nodiscard]] math::Vec3 up() const noexcept;
    [[nodiscard]] math::Vec3 position() const noexcept { return focus - forward() * distance; }
    [[nodiscard]] render::Camera to_render_camera() const noexcept;

    // Mouse deltas are in pixels; `degrees_per_pixel` sets sensitivity.
    void orbit(float dx, float dy, float degrees_per_pixel = 0.3f) noexcept;
    void look(float dx, float dy, float degrees_per_pixel = 0.2f) noexcept;
    // Pans so the focus point follows the mouse at its depth.
    void pan(float dx, float dy, float viewport_height) noexcept;
    void dolly(float wheel_steps) noexcept;
    // `move` is (right, up, forward) in -1..1 per axis.
    void fly(const math::Vec3& move, float dt) noexcept;
    void frame(const math::Vec3& center, float radius) noexcept;
};

}  // namespace aether::editor
