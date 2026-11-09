// Mouse picking: a ray from the viewport into the scene, tested against the
// oriented bounds of every visible mesh.
#pragma once

#include <optional>
#include <vector>

#include "aether/foundation/math.hpp"
#include "aether/render/mesh.hpp"
#include "aether/world/world.hpp"

namespace aether::editor {

struct Ray {
    math::Vec3 origin;
    math::Vec3 direction;  // not necessarily unit length
};

// `ndc` uses the renderer's convention: x right, y down, both in -1..1.
[[nodiscard]] Ray screen_ray(const math::Mat4& view_proj, float ndc_x, float ndc_y) noexcept;

// Ray parameter t (point = origin + t * direction) of the first hit, if any.
[[nodiscard]] std::optional<float> intersect_aabb(const Ray& ray, const render::Aabb& box) noexcept;

struct PickResult {
    world::Entity entity = world::kNullEntity;
    float t = 0;
};

// Nearest entity with GlobalTransform + MeshInstance hit by `ray`.
// `mesh_bounds[id]` is the local AABB of mesh id; unknown ids are skipped.
[[nodiscard]] PickResult pick(world::World& world, const Ray& ray, const std::vector<render::Aabb>& mesh_bounds);

}  // namespace aether::editor
