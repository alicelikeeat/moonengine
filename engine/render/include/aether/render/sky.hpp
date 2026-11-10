// Image-based lighting data for an Environment's sky, computed on the CPU.
//
// The sky is an analytic gradient, so its light can be baked in well under
// a millisecond-scale budget whenever the Environment changes:
//  - `cube`: a small cube map of the sky, prefiltered with the GGX lobe for
//    increasing roughness in each mip level (mip = roughness * (mips - 1)),
//    sampled along the reflection vector for specular lighting.
//  - `irradiance`: diffuse sky light as 9 spherical-harmonic coefficients,
//    already divided by pi and with the cosine lobe folded in, in the order
//    the mesh shader evaluates them: 1, y, z, x, xy, yz, 3z^2-1, xz, x^2-y^2.
// Both include Environment::ambient_intensity. The sun is not part of the
// bake: it is a light of its own.
#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "aether/render/light.hpp"

namespace aether::render {

struct SkyBake {
    std::uint32_t size = 0;  // edge of mip 0 in texels
    std::uint32_t mips = 0;
    // RGBA16F texels: mip 0 faces +X, -X, +Y, -Y, +Z, -Z, then mip 1, ...
    std::vector<std::uint16_t> cube;
    std::array<math::Vec3, 9> irradiance{};
};

// `size` must be a power of two with at least `mips` levels.
[[nodiscard]] SkyBake bake_sky(const Environment& env, std::uint32_t size, std::uint32_t mips);

// Direction through cube face `face` (+X, -X, +Y, -Y, +Z, -Z) at (u, v) in
// [-1, 1], v pointing down the face image (Vulkan convention). Not normalised.
[[nodiscard]] math::Vec3 cube_direction(std::uint32_t face, float u, float v) noexcept;

// IEEE 754 binary16 conversion (round to nearest; overflow becomes infinity).
[[nodiscard]] std::uint16_t float_to_half(float value) noexcept;
[[nodiscard]] float half_to_float(std::uint16_t half) noexcept;

}  // namespace aether::render
