// Light sources.
//
// A Light on an entity illuminates the scene from the entity's
// GlobalTransform:
//  - Directional: parallel light travelling along the entity's local -Z
//    axis (like a sun). The first directional light with cast_shadows
//    casts shadows.
//  - Point: light from the entity's position in all directions, fading to
//    zero at `range`.
// A world with no Light at all is lit by a default sun so that new and
// older levels are never black.
#pragma once

#include <cstdint>
#include <string_view>

#include "aether/foundation/math.hpp"

namespace aether::render {

enum class LightType : std::uint8_t { Directional, Point };

struct Light {
    static constexpr std::string_view kComponentName = "Light";
    LightType type = LightType::Point;
    math::Vec3 color{1, 1, 1};  // linear RGB
    // Directional: illuminance multiplier (about 3 for daylight in this
    // renderer's units). Point: intensity at 1 m before range fading.
    float intensity = 10.0f;
    float range = 10.0f;        // point lights only, metres
    bool cast_shadows = true;   // directional lights only (for now)
};

// Sky and image-wide settings. Put it on one entity of the level (the first
// one found is used); without one, RenderOptions::environment applies.
//
// The sky is a gradient: `sky_color` at the zenith, `horizon_color` at the
// horizon, `ground_color` below. It lights the scene from all directions
// (diffuse and reflections) and, when `sky_visible`, is drawn behind the
// scene together with the sun's disc.
struct Environment {
    static constexpr std::string_view kComponentName = "Environment";
    math::Vec3 sky_color{0.20f, 0.36f, 0.72f};      // linear RGB
    math::Vec3 horizon_color{0.62f, 0.72f, 0.86f};
    math::Vec3 ground_color{0.16f, 0.14f, 0.12f};
    float ambient_intensity = 1.0f;  // brightness of the sky as a light and as a background
    float exposure = 1.0f;           // applied before ACES tone mapping
    bool shadows = true;             // sun shadows on/off
    bool sky_visible = false;        // draw the sky instead of the clear colour

    bool operator==(const Environment&) const noexcept = default;
};

// Radiance of the Environment's sky toward unit direction `dir` (without the
// sun, before ambient_intensity). The sky shader computes the same.
[[nodiscard]] math::Vec3 sky_radiance(const Environment& env, math::Vec3 dir) noexcept;

// Rotation that makes an entity's local -Z point along `direction` (for
// aiming directional lights). `direction` need not be normalised.
[[nodiscard]] math::Quat rotation_toward(math::Vec3 direction) noexcept;

// Point lights beyond this count (nearest to the camera first) are ignored.
inline constexpr std::uint32_t kMaxPointLights = 16;

}  // namespace aether::render
