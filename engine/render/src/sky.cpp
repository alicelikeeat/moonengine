#include "aether/render/sky.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace aether::render {

namespace {

constexpr std::uint32_t kSpecularSamples = 32;
constexpr std::uint32_t kIrradianceSamples = 2048;

// Van der Corput radical inverse (Hammersley sequence).
float radical_inverse(std::uint32_t bits) noexcept {
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return static_cast<float>(bits) * 2.3283064365386963e-10f;
}

// Sky radiance blurred by the GGX lobe of `roughness` around `n` (with the
// usual N = V = R assumption).
math::Vec3 prefilter(const Environment& env, math::Vec3 n, float roughness) noexcept {
    if (roughness <= 0.0f) return sky_radiance(env, n);
    const math::Vec3 helper = std::abs(n.y) < 0.99f ? math::Vec3{0, 1, 0} : math::Vec3{1, 0, 0};
    const math::Vec3 tx = math::normalize(math::cross(helper, n));
    const math::Vec3 ty = math::cross(n, tx);
    const float a = roughness * roughness;
    math::Vec3 sum{0, 0, 0};
    float weight = 0.0f;
    for (std::uint32_t i = 0; i < kSpecularSamples; ++i) {
        const float u1 = (static_cast<float>(i) + 0.5f) / static_cast<float>(kSpecularSamples);
        const float u2 = radical_inverse(i);
        const float phi = 2.0f * math::kPi * u1;
        const float cos_theta = std::sqrt((1.0f - u2) / (1.0f + (a * a - 1.0f) * u2));
        const float sin_theta = std::sqrt(std::max(0.0f, 1.0f - cos_theta * cos_theta));
        const math::Vec3 h = tx * (sin_theta * std::cos(phi)) + ty * (sin_theta * std::sin(phi)) + n * cos_theta;
        const math::Vec3 l = h * (2.0f * math::dot(n, h)) - n;
        const float n_dot_l = math::dot(n, l);
        if (n_dot_l <= 0.0f) continue;
        sum = sum + sky_radiance(env, math::normalize(l)) * n_dot_l;
        weight += n_dot_l;
    }
    return weight > 0.0f ? sum * (1.0f / weight) : sky_radiance(env, n);
}

}  // namespace

math::Vec3 cube_direction(std::uint32_t face, float u, float v) noexcept {
    switch (face) {
        case 0: return {1, -v, -u};
        case 1: return {-1, -v, u};
        case 2: return {u, 1, v};
        case 3: return {u, -1, -v};
        case 4: return {u, -v, 1};
        default: return {-u, -v, -1};
    }
}

std::uint16_t float_to_half(float value) noexcept {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    const std::uint32_t sign = (bits >> 16u) & 0x8000u;
    const std::int32_t exponent = static_cast<std::int32_t>((bits >> 23u) & 0xFFu) - 127 + 15;
    std::uint32_t mantissa = bits & 0x7FFFFFu;
    if (((bits >> 23u) & 0xFFu) == 0xFFu)  // infinity or NaN
        return static_cast<std::uint16_t>(sign | 0x7C00u | (mantissa ? 0x200u : 0u));
    if (exponent >= 31) return static_cast<std::uint16_t>(sign | 0x7C00u);  // overflow
    if (exponent <= 0) {
        if (exponent < -10) return static_cast<std::uint16_t>(sign);  // too small: zero
        mantissa |= 0x800000u;  // subnormal half
        const std::uint32_t shift = static_cast<std::uint32_t>(14 - exponent);
        std::uint32_t half = mantissa >> shift;
        if ((mantissa >> (shift - 1u)) & 1u) ++half;  // round
        return static_cast<std::uint16_t>(sign | half);
    }
    std::uint32_t half = (static_cast<std::uint32_t>(exponent) << 10u) | (mantissa >> 13u);
    if (mantissa & 0x1000u) ++half;  // round; a carry into the exponent is still correct
    return static_cast<std::uint16_t>(sign | half);
}

float half_to_float(std::uint16_t half) noexcept {
    const std::uint32_t sign = (half & 0x8000u) << 16u;
    const std::uint32_t exponent = (half >> 10u) & 0x1Fu;
    const std::uint32_t mantissa = half & 0x3FFu;
    std::uint32_t bits = 0;
    if (exponent == 0) {
        if (mantissa == 0) {
            bits = sign;
        } else {  // subnormal: normalise
            std::uint32_t m = mantissa;
            std::uint32_t e = 113;  // 127 - 15 + 1
            while (!(m & 0x400u)) {
                m <<= 1u;
                --e;
            }
            bits = sign | (e << 23u) | ((m & 0x3FFu) << 13u);
        }
    } else if (exponent == 31) {
        bits = sign | 0x7F800000u | (mantissa << 13u);
    } else {
        bits = sign | ((exponent + 112u) << 23u) | (mantissa << 13u);
    }
    float out = 0;
    std::memcpy(&out, &bits, sizeof(out));
    return out;
}

SkyBake bake_sky(const Environment& env, std::uint32_t size, std::uint32_t mips) {
    SkyBake bake;
    bake.size = size;
    bake.mips = mips;
    const float intensity = std::max(0.0f, env.ambient_intensity);

    for (std::uint32_t mip = 0; mip < mips; ++mip) {
        const std::uint32_t edge = std::max(1u, size >> mip);
        const float roughness = mips > 1 ? static_cast<float>(mip) / static_cast<float>(mips - 1) : 0.0f;
        for (std::uint32_t face = 0; face < 6; ++face) {
            for (std::uint32_t y = 0; y < edge; ++y) {
                for (std::uint32_t x = 0; x < edge; ++x) {
                    const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(edge) * 2.0f - 1.0f;
                    const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(edge) * 2.0f - 1.0f;
                    const math::Vec3 c = prefilter(env, math::normalize(cube_direction(face, u, v)), roughness) * intensity;
                    bake.cube.push_back(float_to_half(c.x));
                    bake.cube.push_back(float_to_half(c.y));
                    bake.cube.push_back(float_to_half(c.z));
                    bake.cube.push_back(float_to_half(1.0f));
                }
            }
        }
    }

    // Project the sky onto spherical harmonics with a Fibonacci sphere, then
    // fold in the cosine lobe (pi, 2pi/3, pi/4 per band), the 1/pi of the
    // Lambert BRDF and each basis function's constant.
    std::array<math::Vec3, 9> l{};
    const float golden = math::kPi * (3.0f - std::sqrt(5.0f));
    for (std::uint32_t i = 0; i < kIrradianceSamples; ++i) {
        const float z = 1.0f - 2.0f * (static_cast<float>(i) + 0.5f) / static_cast<float>(kIrradianceSamples);
        const float r = std::sqrt(std::max(0.0f, 1.0f - z * z));
        const float phi = golden * static_cast<float>(i);
        const math::Vec3 d{r * std::cos(phi), r * std::sin(phi), z};
        const math::Vec3 c = sky_radiance(env, d);
        const float basis[9] = {0.282095f,
                                0.488603f * d.y,
                                0.488603f * d.z,
                                0.488603f * d.x,
                                1.092548f * d.x * d.y,
                                1.092548f * d.y * d.z,
                                0.315392f * (3.0f * d.z * d.z - 1.0f),
                                1.092548f * d.x * d.z,
                                0.546274f * (d.x * d.x - d.y * d.y)};
        for (std::size_t k = 0; k < 9; ++k) l[k] = l[k] + c * basis[k];
    }
    const float norm = 4.0f * math::kPi / static_cast<float>(kIrradianceSamples);
    const float constants[9] = {0.282095f, 0.488603f, 0.488603f, 0.488603f, 1.092548f,
                                1.092548f, 0.315392f, 1.092548f, 0.546274f};
    const float band[9] = {1.0f, 2.0f / 3.0f, 2.0f / 3.0f, 2.0f / 3.0f, 0.25f, 0.25f, 0.25f, 0.25f, 0.25f};
    for (std::size_t k = 0; k < 9; ++k) bake.irradiance[k] = l[k] * (norm * constants[k] * band[k] * intensity);
    return bake;
}

}  // namespace aether::render
