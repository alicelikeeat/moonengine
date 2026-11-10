// CPU-side mesh data and the component that makes an entity visible.
#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

#include "aether/foundation/math.hpp"

namespace aether::render {

struct Vertex {
    math::Vec3 position;
    math::Vec3 normal;
    float uv[2] = {0, 0};  // texture coordinates; (0, 0) is the image's top-left
    // xyz: tangent (direction of increasing u); w: handedness, so that
    // bitangent = cross(normal, tangent) * w points up the image (glTF).
    float tangent[4] = {1, 0, 0, 1};
};
static_assert(sizeof(Vertex) == 48, "Vertex layout is shared with shaders");

// Triangle list. Front faces wind counter-clockwise seen from outside.
struct MeshData {
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
};

// Axis-aligned cube centred on the origin, 24 vertices with flat normals.
[[nodiscard]] MeshData make_cube(float size = 1.0f);

// UV sphere centred on the origin with smooth normals; u wraps around the
// Y axis, v runs from the top pole (0) to the bottom (1).
[[nodiscard]] MeshData make_sphere(float radius = 0.5f, std::uint32_t rings = 24, std::uint32_t segments = 48);

struct Aabb {
    math::Vec3 min{};
    math::Vec3 max{};
};

// Local-space bounds of a mesh (zero box for an empty mesh).
[[nodiscard]] Aabb compute_bounds(const MeshData& mesh) noexcept;

using MeshId = std::uint32_t;
using TextureId = std::uint32_t;

// Texture 0 always exists: 1x1 white, so untextured meshes use `color` alone.
inline constexpr TextureId kWhiteTexture = 0;

struct TextureData {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::uint8_t> rgba;  // RGBA8, top row first
    bool srgb = true;                // colour data (base colour) is sRGB-encoded
};

// Draws `mesh` with the entity's GlobalTransform and a metallic-roughness
// material (glTF model):
//   base colour = color * texture
//   metallic    = metallic * metallic_roughness_texture.b
//   roughness   = roughness * metallic_roughness_texture.g
//   normal      = normal_texture (tangent space); kWhiteTexture means none
//   emitted     = emissive (linear RGB, added after lighting)
struct MeshInstance {
    static constexpr std::string_view kComponentName = "MeshInstance";
    MeshId mesh = 0;
    math::Vec3 color{0.8f, 0.8f, 0.8f};  // linear RGB
    TextureId texture = kWhiteTexture;
    float metallic = 0.0f;
    float roughness = 0.5f;
    math::Vec3 emissive{0, 0, 0};
    TextureId normal_texture = kWhiteTexture;
    TextureId metallic_roughness_texture = kWhiteTexture;
};

}  // namespace aether::render
