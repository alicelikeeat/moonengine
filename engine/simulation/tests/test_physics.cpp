#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "aether/simulation/physics.hpp"
#include "aether/world/hierarchy.hpp"
#include "aether_test.hpp"

using namespace aether;
using namespace aether::math;
using namespace aether::simulation;
using namespace aether::world;

namespace {

constexpr float kStep = 1.0f / 60.0f;

Entity add_box(World& w, Vec3 position, Vec3 half_extents, BodyType type) {
    const Entity e = w.create();
    w.add<Transform>(e, Transform{position});
    Collider c;
    c.half_extents = half_extents;
    w.add<Collider>(e, c);
    if (type != BodyType::Static) w.add<RigidBody>(e, RigidBody{.type = type});
    return e;
}

Entity add_sphere(World& w, Vec3 position, float radius, float restitution = 0.1f) {
    const Entity e = w.create();
    w.add<Transform>(e, Transform{position});
    Collider c;
    c.shape = ShapeType::Sphere;
    c.radius = radius;
    w.add<Collider>(e, c);
    w.add<RigidBody>(e, RigidBody{.restitution = restitution});
    return e;
}

Entity add_ground(World& w) { return add_box(w, {0, -0.5f, 0}, {50, 0.5f, 50}, BodyType::Static); }

std::unique_ptr<PhysicsWorld> make_physics() {
    auto p = PhysicsWorld::create();
    REQUIRE(p.has_value());
    return std::move(p).value();
}

void run(PhysicsWorld& p, World& w, int steps) {
    for (int i = 0; i < steps; ++i) p.step(w, kStep);
}

}  // namespace

AETHER_TEST(box_falls_and_rests_on_the_ground) {
    auto physics = make_physics();
    World w({.seed = 1});
    add_ground(w);
    const Entity box = add_box(w, {0, 5, 0}, {0.5f, 0.5f, 0.5f}, BodyType::Dynamic);

    // Free fall first: after 0.5 s it has dropped about g t^2 / 2 = 1.23 m.
    run(*physics, w, 30);
    const float y_half_second = w.get<Transform>(box)->position.y;
    std::printf("    after 0.5 s: y = %.3f, vy = %.3f\n", static_cast<double>(y_half_second),
                static_cast<double>(w.get<RigidBody>(box)->linear_velocity.y));
    CHECK(std::abs(y_half_second - (5.0f - 1.226f)) < 0.1f);
    CHECK(std::abs(w.get<RigidBody>(box)->linear_velocity.y + 4.9f) < 0.2f);

    run(*physics, w, 300);
    const Transform& t = *w.get<Transform>(box);
    std::printf("    at rest: y = %.4f\n", static_cast<double>(t.position.y));
    CHECK(std::abs(t.position.y - 0.5f) < 0.02f);  // resting on the ground (top at y = 0)
    CHECK(std::abs(t.position.x) < 0.01f && std::abs(t.position.z) < 0.01f);
    CHECK(length(w.get<RigidBody>(box)->linear_velocity) < 0.01f);
    // GlobalTransform is up to date after step().
    CHECK(std::abs(w.get<GlobalTransform>(box)->matrix.translation().y - t.position.y) < 1e-5f);
}

AETHER_TEST(static_and_missing_rigid_bodies_do_not_move) {
    auto physics = make_physics();
    World w({.seed = 1});
    const Entity ground = add_ground(w);
    const Entity shelf = add_box(w, {0, 3, 0}, {1, 0.1f, 1}, BodyType::Static);  // Collider only
    const Entity pinned = w.create();
    w.add<Transform>(pinned, Transform{{4, 2, 0}});
    w.add<Collider>(pinned);
    w.add<RigidBody>(pinned, RigidBody{.type = BodyType::Static});
    const auto stats = physics->step(w, kStep);
    CHECK_EQ(stats.bodies, 3u);
    CHECK_EQ(stats.created, 3u);
    run(*physics, w, 60);
    CHECK(w.get<Transform>(ground)->position.y == -0.5f);
    CHECK(w.get<Transform>(shelf)->position.y == 3.0f);
    CHECK(w.get<Transform>(pinned)->position.y == 2.0f);
}

AETHER_TEST(restitution_makes_a_sphere_bounce) {
    auto physics = make_physics();
    World w({.seed = 1});
    add_ground(w);  // restitution 0.1; a contact uses the larger of the two bodies' values
    const Entity bouncy = add_sphere(w, {-2, 3, 0}, 0.5f, 0.8f);
    const Entity dull = add_sphere(w, {2, 3, 0}, 0.5f, 0.0f);
    float bouncy_peak = 0, dull_peak = 0;
    bool landed = false;
    for (int i = 0; i < 240; ++i) {
        physics->step(w, kStep);
        if (w.get<Transform>(dull)->position.y < 0.6f) landed = true;
        if (landed) {
            bouncy_peak = std::max(bouncy_peak, w.get<Transform>(bouncy)->position.y);
            dull_peak = std::max(dull_peak, w.get<Transform>(dull)->position.y);
        }
    }
    std::printf("    peak after landing: bouncy %.2f, dull %.2f\n", static_cast<double>(bouncy_peak),
                static_cast<double>(dull_peak));
    CHECK(bouncy_peak > 1.5f);
    CHECK(dull_peak < 0.7f);
}

AETHER_TEST(boxes_stack) {
    auto physics = make_physics();
    World w({.seed = 1});
    add_ground(w);
    std::vector<Entity> boxes;
    for (int i = 0; i < 5; ++i)
        boxes.push_back(add_box(w, {0, 0.5f + 1.05f * static_cast<float>(i), 0}, {0.5f, 0.5f, 0.5f}, BodyType::Dynamic));
    run(*physics, w, 600);
    for (std::size_t i = 0; i < boxes.size(); ++i) {
        const Vec3 p = w.get<Transform>(boxes[i])->position;
        CHECK(std::abs(p.y - (0.5f + static_cast<float>(i))) < 0.05f);
        CHECK(std::abs(p.x) < 0.05f && std::abs(p.z) < 0.05f);
    }
}

AETHER_TEST(simulation_is_deterministic) {
    const auto simulate = [] {
        auto physics = make_physics();
        World w({.seed = 1});
        add_ground(w);
        std::vector<Entity> bodies;
        for (int i = 0; i < 12; ++i) {
            const float f = static_cast<float>(i);
            bodies.push_back(i % 2 ? add_sphere(w, {0.3f * f, 2 + f, 0.1f * f}, 0.4f, 0.4f)
                                   : add_box(w, {0.2f * f, 2 + f, -0.1f * f}, {0.4f, 0.3f, 0.5f}, BodyType::Dynamic));
        }
        run(*physics, w, 300);
        std::vector<Vec3> out;
        for (const Entity e : bodies) out.push_back(w.get<Transform>(e)->position);
        return out;
    };
    const auto a = simulate();
    const auto b = simulate();
    REQUIRE(a.size() == b.size());
    for (std::size_t i = 0; i < a.size(); ++i) CHECK(a[i] == b[i]);  // bit-identical
}

AETHER_TEST(removing_entities_and_components_removes_bodies) {
    auto physics = make_physics();
    World w({.seed = 1});
    add_ground(w);
    const Entity a = add_box(w, {0, 2, 0}, {0.5f, 0.5f, 0.5f}, BodyType::Dynamic);
    const Entity b = add_box(w, {3, 2, 0}, {0.5f, 0.5f, 0.5f}, BodyType::Dynamic);
    physics->step(w, kStep);
    CHECK_EQ(physics->body_count(), 3u);
    w.destroy(a);
    w.remove<Collider>(b);
    const auto stats = physics->step(w, kStep);
    CHECK_EQ(stats.removed, 2u);
    CHECK_EQ(physics->body_count(), 1u);
    // b no longer simulates: it stays where it is.
    const float y = w.get<Transform>(b)->position.y;
    run(*physics, w, 30);
    CHECK(w.get<Transform>(b)->position.y == y);
}

AETHER_TEST(edits_to_transforms_and_settings_reach_the_simulation) {
    auto physics = make_physics();
    World w({.seed = 1});
    const Entity ground = add_ground(w);
    const Entity box = add_box(w, {0, 0.5f, 0}, {0.5f, 0.5f, 0.5f}, BodyType::Dynamic);
    run(*physics, w, 120);  // settles and falls asleep

    // Teleport (as an editor gizmo or gameplay code would): it falls again from there.
    w.get<Transform>(box)->position = {5, 4, 0};
    run(*physics, w, 20);
    const Vec3 p = w.get<Transform>(box)->position;
    CHECK(std::abs(p.x - 5.0f) < 0.01f);
    CHECK(p.y < 4.0f && p.y > 2.5f);

    // Set a velocity through the component.
    w.get<RigidBody>(box)->linear_velocity = {0, 10, 0};
    physics->step(w, kStep);
    CHECK(w.get<RigidBody>(box)->linear_velocity.y > 9.0f);

    // An impulse changes the velocity by impulse / mass and is not undone by the next sync.
    w.get<RigidBody>(box)->mass = 2.0f;  // rebuilds the body, keeping its motion
    physics->step(w, kStep);
    const float before = w.get<RigidBody>(box)->linear_velocity.x;
    CHECK(physics->add_impulse(box, {6, 0, 0}));
    physics->step(w, kStep);
    CHECK(std::abs(w.get<RigidBody>(box)->linear_velocity.x - (before + 3.0f)) < 0.1f);
    CHECK(!physics->add_impulse(ground, {1, 0, 0}));  // static

    // Changing the collider or scale rebuilds the body.
    w.get<Collider>(box)->half_extents = {1, 1, 1};
    CHECK_EQ(physics->step(w, kStep).created, 1u);
    w.get<Transform>(box)->scale = {2, 2, 2};
    CHECK_EQ(physics->step(w, kStep).created, 1u);
    CHECK_EQ(physics->step(w, kStep).created, 0u);

    // Moving the static ground away lets things fall through where it was.
    w.get<Transform>(ground)->position = {0, -100, 0};
    w.get<Transform>(box)->position = {0, 3, 0};
    w.get<RigidBody>(box)->linear_velocity = {0, 0, 0};
    run(*physics, w, 120);
    CHECK(w.get<Transform>(box)->position.y < -5.0f);
}

AETHER_TEST(kinematic_bodies_push_dynamic_ones) {
    auto physics = make_physics();
    World w({.seed = 1});
    add_ground(w);
    const Entity pusher = add_box(w, {-3, 0.5f, 0}, {0.5f, 0.5f, 0.5f}, BodyType::Kinematic);
    const Entity box = add_box(w, {0, 0.5f, 0}, {0.5f, 0.5f, 0.5f}, BodyType::Dynamic);
    run(*physics, w, 60);
    for (int i = 0; i < 120; ++i) {  // 3 m/s toward +X for 2 s
        w.get<Transform>(pusher)->position.x += 3.0f * kStep;
        physics->step(w, kStep);
    }
    CHECK(std::abs(w.get<Transform>(pusher)->position.x - 3.0f) < 0.01f);  // exactly where it was put
    CHECK(std::abs(w.get<Transform>(pusher)->position.y - 0.5f) < 1e-5f);  // gravity does not apply
    CHECK(w.get<Transform>(box)->position.x > 3.9f);                       // pushed ahead of it
}

AETHER_TEST(colliders_follow_scale_offset_and_parents) {
    auto physics = make_physics();
    World w({.seed = 1});
    add_ground(w);
    // A unit collider on an entity scaled 2x rests with its centre 1 m up.
    const Entity big = add_box(w, {0, 5, 0}, {0.5f, 0.5f, 0.5f}, BodyType::Dynamic);
    w.get<Transform>(big)->scale = {2, 2, 2};
    // A sphere collider offset 1 m below its entity: the entity rests 1.5 m up.
    const Entity offset = add_sphere(w, {6, 5, 0}, 0.5f, 0.0f);
    w.get<Collider>(offset)->offset = {0, -1, 0};
    w.get<RigidBody>(offset)->angular_damping = 100.0f;  // keep it upright
    // A dynamic child of a moved, rotated, scaled parent: results are written in the parent's space.
    const Entity parent = w.create();
    w.add<Transform>(parent, Transform{{-6, 2, 0}, Quat::from_axis_angle({0, 1, 0}, 1.0f), {2, 2, 2}});
    const Entity child = add_box(w, {0, 2, 0}, {0.25f, 0.25f, 0.25f}, BodyType::Dynamic);  // world half extent 0.5
    REQUIRE(hierarchy::set_parent(w, child, parent).has_value());
    run(*physics, w, 400);
    CHECK(std::abs(w.get<Transform>(big)->position.y - 1.0f) < 0.03f);
    CHECK(std::abs(w.get<Transform>(offset)->position.y - 1.5f) < 0.03f);
    const Vec3 world_pos = w.get<GlobalTransform>(child)->matrix.translation();
    std::printf("    child world position = (%.3f, %.3f, %.3f)\n", static_cast<double>(world_pos.x),
                static_cast<double>(world_pos.y), static_cast<double>(world_pos.z));
    CHECK(std::abs(world_pos.y - 0.5f) < 0.03f);
    CHECK(std::abs(world_pos.x + 6.0f) < 0.05f);
    CHECK(w.get<Transform>(child)->scale == (Vec3{1, 1, 1}));  // local scale untouched
}

AETHER_TEST(raycast_finds_the_nearest_collider) {
    auto physics = make_physics();
    World w({.seed = 1});
    const Entity ground = add_ground(w);
    const Entity wall = add_box(w, {0, 1, -5}, {2, 1, 0.5f}, BodyType::Static);
    physics->step(w, 0.0f);  // builds the bodies without advancing time
    auto hit = physics->raycast({0, 1, 0}, {0, 0, -3}, 100.0f);  // direction need not be unit length
    REQUIRE(hit.has_value());
    CHECK(hit->entity == wall);
    CHECK(std::abs(hit->distance - 4.5f) < 0.01f);
    CHECK(near(hit->normal, Vec3{0, 0, 1}));
    CHECK(std::abs(hit->point.z + 4.5f) < 0.01f);
    auto down = physics->raycast({10, 5, 10}, {0, -1, 0}, 100.0f);
    REQUIRE(down.has_value());
    CHECK(down->entity == ground);
    CHECK(std::abs(down->distance - 5.0f) < 0.01f);
    CHECK(!physics->raycast({0, 1, 0}, {0, 0, -1}, 2.0f));  // too short
    CHECK(!physics->raycast({0, 1, 0}, {0, 1, 0}, 100.0f)); // nothing above
    CHECK(!physics->raycast({0, 1, 0}, {0, 0, 0}, 100.0f)); // no direction
}

AETHER_TEST(gravity_can_be_changed_and_scaled) {
    auto physics = make_physics();
    World w({.seed = 1});
    physics->set_gravity({0, 0, 0});
    CHECK(physics->gravity() == (Vec3{0, 0, 0}));
    const Entity floating = add_box(w, {0, 3, 0}, {0.5f, 0.5f, 0.5f}, BodyType::Dynamic);
    run(*physics, w, 60);
    CHECK(w.get<Transform>(floating)->position.y == 3.0f);
    physics->set_gravity({0, -9.81f, 0});
    w.get<RigidBody>(floating)->gravity_scale = -1.0f;  // falls upward
    w.get<RigidBody>(floating)->linear_velocity = {0, 0.001f, 0};  // wake it
    run(*physics, w, 60);
    CHECK(w.get<Transform>(floating)->position.y > 6.0f);
}

AETHER_TEST(physics_components_round_trip_through_levels) {
    SerializerRegistry reg;
    register_core_components(reg);
    simulation::register_components(reg);
    World a({.seed = 1});
    const Entity e = a.create();
    RigidBody rb{.type = BodyType::Kinematic, .mass = 12.5f, .friction = 0.9f, .restitution = 0.3f};
    rb.linear_velocity = {1, 2, 3};
    a.add<RigidBody>(e, rb);
    Collider c;
    c.shape = ShapeType::Capsule;
    c.radius = 0.3f;
    c.half_height = 0.7f;
    c.offset = {0, 1, 0};
    a.add<Collider>(e, c);
    const Json doc = save_level(a, reg);
    World b({.seed = 2});
    auto loaded = load_level(doc, b, reg);
    REQUIRE(loaded.has_value());
    CHECK(loaded.value().warnings.empty());
    const Entity f = b.find(a.id_of(e));
    const RigidBody* rb2 = b.get<RigidBody>(f);
    const Collider* c2 = b.get<Collider>(f);
    REQUIRE(rb2 != nullptr && c2 != nullptr);
    CHECK(rb2->type == BodyType::Kinematic && rb2->mass == 12.5f && rb2->friction == 0.9f && rb2->restitution == 0.3f);
    CHECK(rb2->linear_velocity == (Vec3{1, 2, 3}));
    CHECK(c2->shape == ShapeType::Capsule && c2->radius == 0.3f && c2->half_height == 0.7f);
    CHECK(c2->offset == (Vec3{0, 1, 0}));

    for (const char* bad : {R"({"RigidBody": {"type": "ghost"}})", R"({"RigidBody": {"mass": 0}})",
                            R"({"RigidBody": {"restitution": 2}})", R"({"Collider": {"shape": "mesh"}})",
                            R"({"Collider": {"half_extents": [1, 0, 1]}})", R"({"Collider": {"shape": "sphere", "radius": -1}})"}) {
        auto parsed = parse_json(std::string(R"({"format": "aether.level", "version": 1, "entities": [
            {"id": "00000000-0000-4000-8000-000000000001", "components": )") + bad + "}]}");
        REQUIRE(parsed.has_value());
        World w({.seed = 1});
        auto r = load_level(parsed.value(), w, reg);
        REQUIRE(r.has_value());
        CHECK_EQ(r.value().warnings.size(), std::size_t{1});
    }
}

AETHER_TEST(many_worlds_can_coexist_and_limits_are_checked) {
    CHECK(!PhysicsWorld::create(PhysicsDesc{.max_bodies = 0}));
    auto a = make_physics();
    auto b = make_physics();
    World wa({.seed = 1}), wb({.seed = 2});
    add_ground(wa);
    const Entity in_a = add_box(wa, {0, 3, 0}, {0.5f, 0.5f, 0.5f}, BodyType::Dynamic);
    const Entity in_b = add_box(wb, {0, 3, 0}, {0.5f, 0.5f, 0.5f}, BodyType::Dynamic);
    for (int i = 0; i < 200; ++i) {
        a->step(wa, kStep);
        b->step(wb, kStep);
    }
    a.reset();  // the other world keeps working
    b->step(wb, kStep);
    CHECK(std::abs(wa.get<Transform>(in_a)->position.y - 0.5f) < 0.02f);
    CHECK(wb.get<Transform>(in_b)->position.y < -10.0f);  // no ground in b
}
