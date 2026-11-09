#include <cmath>
#include <vector>

#include "aether/editor/editor_camera.hpp"
#include "aether/editor/picking.hpp"
#include "aether/world/hierarchy.hpp"
#include "aether_test.hpp"

using namespace aether;
using namespace aether::editor;
using namespace aether::math;

AETHER_TEST(camera_orbits_around_focus_at_fixed_distance) {
    EditorCamera cam;
    cam.focus = {1, 2, 3};
    cam.distance = 10;
    for (int i = 0; i < 20; ++i) {
        cam.orbit(13, 7);
        CHECK(near(length(cam.position() - cam.focus), 10.0f, 1e-3f));
    }
    CHECK(cam.pitch >= -89.0f && cam.pitch <= 89.0f);
}

AETHER_TEST(camera_look_keeps_eye_still) {
    EditorCamera cam;
    const Vec3 eye = cam.position();
    cam.look(40, -25);
    CHECK(near(cam.position(), eye, 1e-3f));
    CHECK(near(cam.forward(), normalize(cam.focus - eye), 1e-4f));
}

AETHER_TEST(camera_basis_and_fly) {
    EditorCamera cam;
    cam.yaw = 0;
    cam.pitch = 0;
    CHECK(near(cam.forward(), Vec3{0, 0, -1}));
    CHECK(near(cam.right(), Vec3{1, 0, 0}));
    CHECK(near(cam.up(), Vec3{0, 1, 0}));
    const Vec3 before = cam.position();
    cam.fly_speed = 2;
    cam.fly({0, 0, 1}, 0.5f);  // forward 1 m
    CHECK(near(cam.position(), before + Vec3{0, 0, -1}, 1e-4f));
}

AETHER_TEST(camera_frame_fits_sphere) {
    EditorCamera cam;
    cam.frame({5, 0, 0}, 2.0f);
    CHECK(near(cam.focus, Vec3{5, 0, 0}));
    // The sphere fits inside the vertical field of view.
    CHECK(std::asin(2.0f / cam.distance) <= cam.fov_y * 0.5f);
}

AETHER_TEST(ray_hits_aabb_and_misses_behind) {
    const render::Aabb box{{-1, -1, -1}, {1, 1, 1}};
    auto hit = intersect_aabb({{0, 0, 5}, {0, 0, -1}}, box);
    REQUIRE(hit.has_value());
    CHECK(near(*hit, 4.0f));
    CHECK(!intersect_aabb({{0, 0, 5}, {0, 0, 1}}, box));     // pointing away
    CHECK(!intersect_aabb({{3, 0, 5}, {0, 0, -1}}, box));    // passes beside
    auto inside = intersect_aabb({{0, 0, 0}, {1, 0, 0}}, box);
    REQUIRE(inside.has_value());
    CHECK(near(*inside, 1.0f));                               // exit point
}

AETHER_TEST(pick_selects_nearest_visible_entity) {
    world::World w({.seed = 1});
    const auto spawn = [&](Vec3 pos, float scale) {
        const world::Entity e = w.create();
        w.add<world::Transform>(e, world::Transform{pos, Quat::identity(), {scale, scale, scale}});
        w.add<render::MeshInstance>(e, render::MeshInstance{0, {1, 1, 1}});
        return e;
    };
    const world::Entity near_cube = spawn({0, 0, 0}, 1);
    const world::Entity far_cube = spawn({0, 0, -10}, 3);
    const world::Entity side_cube = spawn({4, 0, 0}, 1);
    world::propagate_transforms(w);
    const std::vector<render::Aabb> bounds{{{-0.5f, -0.5f, -0.5f}, {0.5f, 0.5f, 0.5f}}};

    render::Camera cam;
    cam.position = {0, 0, 8};
    const Mat4 vp = render::view_projection(cam, 1.0f);
    const PickResult center = pick(w, screen_ray(vp, 0, 0), bounds);
    CHECK(center.entity == near_cube);  // the far cube is behind it
    const Vec3 side_ndc = render::project_to_ndc(vp, {4, 0, 0});
    CHECK(pick(w, screen_ray(vp, side_ndc.x, side_ndc.y), bounds).entity == side_cube);
    CHECK(pick(w, screen_ray(vp, 0.95f, -0.95f), bounds).entity.is_null());
    (void)far_cube;
}

AETHER_TEST(pick_respects_hierarchy_and_rotation) {
    world::World w({.seed = 1});
    const world::Entity parent = w.create();
    w.add<world::Transform>(parent, world::Transform{{0, 0, 0}, Quat::from_axis_angle({0, 1, 0}, kPi / 2)});
    const world::Entity child = w.create();
    w.add<world::Transform>(child, world::Transform{{3, 0, 0}});  // world (0, 0, -3) after parent rotation
    w.add<render::MeshInstance>(child, render::MeshInstance{0, {1, 1, 1}});
    REQUIRE(world::hierarchy::set_parent(w, child, parent).has_value());
    world::propagate_transforms(w);
    const std::vector<render::Aabb> bounds{{{-0.5f, -0.5f, -0.5f}, {0.5f, 0.5f, 0.5f}}};
    const Ray down{{0, 10, -3}, {0, -1, 0}};
    const PickResult r = pick(w, down, bounds);
    CHECK(r.entity == child);
    CHECK(near(r.t, 9.5f, 1e-3f));
}
