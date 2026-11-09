#include "aether/render/mesh.hpp"

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
            mesh.vertices.push_back({c + f.u * (su * h) + f.v * (sv * h), f.normal});
        for (const std::uint32_t i : {0u, 1u, 2u, 0u, 2u, 3u}) mesh.indices.push_back(base + i);
    }
    return mesh;
}

}  // namespace aether::render
