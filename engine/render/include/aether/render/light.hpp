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

// Rotation that makes an entity's local -Z point along `direction` (for
// aiming directional lights). `direction` need not be normalised.
[[nodiscard]] math::Quat rotation_toward(math::Vec3 direction) noexcept;

// Point lights beyond this count (nearest to the camera first) are ignored.
inline constexpr std::uint32_t kMaxPointLights = 16;

}  // namespace aether::render
