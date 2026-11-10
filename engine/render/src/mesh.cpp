#include "aether/render/mesh.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace aether::render {

MeshData make_cube(float size) {
    using math::Vec3;
    const float h = size * 0.5f;
    struct Face { Vec3 normal, u, v; };
    // u x v == normal, so corners listed (-u-v, +u-v, +u+v, -u+v) wind CCW
    // when seen from outside.
    const Face faces[6] = {
        {{1, 0, 0}, {0, 0, -1}, {0, 1, 0}},
        {{-1, 0, 0}, {0, 0, 1}, {0, 1, 0}},
        {{0, 1, 0}, {1, 0, 0}, {0, 0, -1}},
        {{0, -1, 0}, {1, 0, 0}, {0, 0, 1}},
        {{0, 0, 1}, {1, 0, 0}, {0, 1, 0}},
        {{0, 0, -1}, {-1, 0, 0}, {0, 1, 0}},
    };
    MeshData mesh;
    mesh.vertices.reserve(24);
    mesh.indices.reserve(36);
    for (const Face& f : faces) {
        const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
        const Vec3 c = f.normal * h;
        static constexpr std::pair<float, float> kCorners[4] = {{-1.f, -1.f}, {1.f, -1.f}, {1.f, 1.f}, {-1.f, 1.f}};
        for (const auto& [su, sv] : kCorners)
            // Tangent = u (increasing tex u); cross(normal, u) = v points up
            // the image (decreasing tex v), so the handedness is +1.
            mesh.vertices.push_back({c + f.u * (su * h) + f.v * (sv * h), f.normal, {(su + 1) * 0.5f, (1 - sv) * 0.5f},
                                     {f.u.x, f.u.y, f.u.z, 1.0f}});
        for (const std::uint32_t i : {0u, 1u, 2u, 0u, 2u, 3u}) mesh.indices.push_back(base + i);
    }
    return mesh;
}

MeshData make_sphere(float radius, std::uint32_t rings, std::uint32_t segments) {
    rings = std::max(rings, 2u);
    segments = std::max(segments, 3u);
    MeshData mesh;
    mesh.vertices.reserve(std::size_t{rings + 1} * (segments + 1));
    for (std::uint32_t i = 0; i <= rings; ++i) {
        const float v = static_cast<float>(i) / static_cast<float>(rings);
        const float theta = math::kPi * v;
        for (std::uint32_t j = 0; j <= segments; ++j) {
            const float u = static_cast<float>(j) / static_cast<float>(segments);
            const float phi = 2.0f * math::kPi * u;
            const math::Vec3 n{std::sin(theta) * std::cos(phi), std::cos(theta), -std::sin(theta) * std::sin(phi)};
            // Tangent: direction of increasing u (around the Y axis).
            mesh.vertices.push_back({n * radius, n, {u, v}, {-std::sin(phi), 0.0f, -std::cos(phi), 1.0f}});
        }
    }
    for (std::uint32_t i = 0; i < rings; ++i) {
        for (std::uint32_t j = 0; j < segments; ++j) {
            const std::uint32_t a = i * (segments + 1) + j, b = a + segments + 1;
            for (const std::uint32_t k : {a, b, b + 1, a, b + 1, a + 1}) mesh.indices.push_back(k);
        }
    }
    return mesh;
}

Aabb compute_bounds(const MeshData& mesh) noexcept {
    if (mesh.vertices.empty()) return {};
    Aabb box{mesh.vertices[0].position, mesh.vertices[0].position};
    for (const Vertex& v : mesh.vertices) {
        box.min = {std::min(box.min.x, v.position.x), std::min(box.min.y, v.position.y), std::min(box.min.z, v.position.z)};
        box.max = {std::max(box.max.x, v.position.x), std::max(box.max.y, v.position.y), std::max(box.max.z, v.position.z)};
    }
    return box;
}

}  // namespace aether::render
