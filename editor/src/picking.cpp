#include "aether/editor/picking.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#include "aether/world/hierarchy.hpp"

namespace aether::editor {

Ray screen_ray(const math::Mat4& view_proj, float ndc_x, float ndc_y) noexcept {
    math::Mat4 inv;
    if (!math::inverse(view_proj, inv)) return {};
    const math::Vec3 near_point = math::transform_projective(inv, {ndc_x, ndc_y, 0.0f});
    const math::Vec3 far_point = math::transform_projective(inv, {ndc_x, ndc_y, 1.0f});
    return {near_point, far_point - near_point};
}

std::optional<float> intersect_aabb(const Ray& ray, const render::Aabb& box) noexcept {
    float t_min = -std::numeric_limits<float>::infinity();
    float t_max = std::numeric_limits<float>::infinity();
    const float o[3] = {ray.origin.x, ray.origin.y, ray.origin.z};
    const float d[3] = {ray.direction.x, ray.direction.y, ray.direction.z};
    const float lo[3] = {box.min.x, box.min.y, box.min.z};
    const float hi[3] = {box.max.x, box.max.y, box.max.z};
    for (int i = 0; i < 3; ++i) {
        if (std::fabs(d[i]) < 1e-12f) {
            if (o[i] < lo[i] || o[i] > hi[i]) return std::nullopt;
            continue;
        }
        float t0 = (lo[i] - o[i]) / d[i];
        float t1 = (hi[i] - o[i]) / d[i];
        if (t0 > t1) std::swap(t0, t1);
        t_min = std::max(t_min, t0);
        t_max = std::min(t_max, t1);
        if (t_min > t_max) return std::nullopt;
    }
    if (t_max < 0) return std::nullopt;  // box is behind the ray
    return t_min >= 0 ? t_min : t_max;   // inside the box: exit point
}

PickResult pick(world::World& world, const Ray& ray, const std::vector<render::Aabb>& mesh_bounds) {
    PickResult best;
    best.t = std::numeric_limits<float>::infinity();
    world.each<const world::GlobalTransform, const render::MeshInstance>(
        [&](world::Entity e, const world::GlobalTransform& g, const render::MeshInstance& m) {
            if (m.mesh >= mesh_bounds.size()) return;
            math::Mat4 inv;
            if (!math::inverse(g.matrix, inv)) return;
            // An affine map preserves the ray parameter, so t is comparable
            // across entities without normalising the local direction.
            const math::Vec3 o = inv.transform_point(ray.origin);
            const math::Vec3 d = inv.transform_point(ray.origin + ray.direction) - o;
            const auto t = intersect_aabb({o, d}, mesh_bounds[m.mesh]);
            if (t && *t < best.t) {
                best.t = *t;
                best.entity = e;
            }
        });
    if (best.entity.is_null()) best.t = 0;
    return best;
}

}  // namespace aether::editor
