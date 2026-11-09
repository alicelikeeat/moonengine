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
};
static_assert(sizeof(Vertex) == 24, "Vertex layout is shared with shaders");

// Triangle list. Front faces wind counter-clockwise seen from outside.
struct MeshData {
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
};

// Axis-aligned cube centred on the origin, 24 vertices with flat normals.
[[nodiscard]] MeshData make_cube(float size = 1.0f);

using MeshId = std::uint32_t;

// Draws `mesh` with the entity's GlobalTransform.
struct MeshInstance {
    static constexpr std::string_view kComponentName = "MeshInstance";
    MeshId mesh = 0;
    math::Vec3 color{0.8f, 0.8f, 0.8f};  // linear RGB
};

}  // namespace aether::render
