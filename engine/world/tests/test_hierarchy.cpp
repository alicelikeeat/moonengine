#include <random>
#include <string>
#include <vector>

#include "aether/foundation/job_system.hpp"
#include "aether/world/hierarchy.hpp"
#include "aether_test.hpp"

using namespace aether;
using namespace aether::math;
using namespace aether::world;
namespace h = aether::world::hierarchy;

namespace {
std::vector<Entity> kids(const World& w, Entity e) {
    const auto s = h::children_of(w, e);
    return {s.begin(), s.end()};
}

// Reference: multiply local matrices walking up the parent chain.
Mat4 brute_force_world(const World& w, Entity e) {
    const auto local = [&](Entity x) {
        const Transform* t = w.get<Transform>(x);
        return t ? t->matrix() : Mat4::identity();
    };
    Mat4 m = local(e);
    for (Entity p = h::parent_of(w, e); !p.is_null() && w.alive(p); p = h::parent_of(w, p)) m = local(p) * m;
    return m;
}
}  // namespace

AETHER_TEST(set_parent_links_both_directions) {
    World w({.seed = 1});
    const Entity root = w.create(), a = w.create(), b = w.create();
    REQUIRE(h::set_parent(w, a, root).has_value());
    REQUIRE(h::set_parent(w, b, root).has_value());
    CHECK(h::parent_of(w, a) == root);
    CHECK(kids(w, root) == (std::vector<Entity>{a, b}));  // insertion order
    CHECK(h::is_ancestor(w, root, b));
    CHECK(!h::is_ancestor(w, a, b));
    CHECK(h::validate(w).empty());
}

AETHER_TEST(reparent_and_clear_keep_links_consistent) {
    World w({.seed = 1});
    const Entity p1 = w.create(), p2 = w.create(), c = w.create();
    REQUIRE(h::set_parent(w, c, p1).has_value());
    REQUIRE(h::set_parent(w, c, p2).has_value());
    CHECK(h::parent_of(w, c) == p2);
    CHECK(!w.has<Children>(p1));  // emptied list is removed
    CHECK(kids(w, p2) == std::vector<Entity>{c});
    CHECK(h::set_parent(w, c, p2).has_value());  // same parent: no-op
    CHECK_EQ(kids(w, p2).size(), std::size_t{1});
    CHECK(h::clear_parent(w, c));
    CHECK(!h::clear_parent(w, c));
    CHECK(h::parent_of(w, c).is_null());
    CHECK(!w.has<Children>(p2));
    CHECK(h::validate(w).empty());
}

AETHER_TEST(set_parent_rejects_cycles_and_dead_entities) {
    World w({.seed = 1});
    const Entity a = w.create(), b = w.create(), c = w.create();
    REQUIRE(h::set_parent(w, b, a).has_value());
    REQUIRE(h::set_parent(w, c, b).has_value());
    auto self = h::set_parent(w, a, a);
    REQUIRE(!self);
    CHECK(self.error().code == ErrorCode::InvalidArgument);
    auto cycle = h::set_parent(w, a, c);
    REQUIRE(!cycle);
    CHECK(cycle.error().code == ErrorCode::InvalidArgument);
    CHECK(h::parent_of(w, a).is_null());  // unchanged after the rejection
    const Entity dead = w.create();
    w.destroy(dead);
    auto r = h::set_parent(w, dead, a);
    REQUIRE(!r);
    CHECK(r.error().code == ErrorCode::NotFound);
    CHECK(h::validate(w).empty());
}

AETHER_TEST(destroy_recursive_removes_subtree_only) {
    World w({.seed = 1});
    const Entity root = w.create(), keep = w.create(), gone = w.create();
    REQUIRE(h::set_parent(w, keep, root).has_value());
    REQUIRE(h::set_parent(w, gone, root).has_value());
    for (int i = 0; i < 5; ++i) {
        const Entity leaf = w.create();
        REQUIRE(h::set_parent(w, leaf, gone).has_value());
    }
    CHECK_EQ(h::destroy_recursive(w, gone), std::size_t{6});
    CHECK(w.alive(root) && w.alive(keep));
    CHECK(kids(w, root) == std::vector<Entity>{keep});
    CHECK_EQ(w.entity_count(), std::size_t{2});
    CHECK(h::validate(w).empty());
}

AETHER_TEST(propagation_composes_parent_and_local) {
    World w({.seed = 1});
    const Entity parent = w.create(), child = w.create(), grandchild = w.create();
    w.add<Transform>(parent, Transform{{10, 0, 0}, Quat::from_axis_angle({0, 1, 0}, kPi / 2), {1, 1, 1}});
    w.add<Transform>(child, Transform{{1, 0, 0}, Quat::identity(), {2, 2, 2}});
    w.add<Transform>(grandchild, Transform{{1, 0, 0}, Quat::identity(), {1, 1, 1}});
    REQUIRE(h::set_parent(w, child, parent).has_value());
    REQUIRE(h::set_parent(w, grandchild, child).has_value());

    propagate_transforms(w);
    REQUIRE(w.has<GlobalTransform>(grandchild));
    // Y rotation of 90 degrees maps +X to -Z.
    CHECK(near(w.get<GlobalTransform>(child)->matrix.translation(), Vec3{10, 0, -1}));
    // Grandchild offset (1,0,0) is scaled by the child (x2), then rotated.
    CHECK(near(w.get<GlobalTransform>(grandchild)->matrix.translation(), Vec3{10, 0, -3}));

    w.get<Transform>(parent)->position = {0, 5, 0};
    propagate_transforms(w);
    CHECK(near(w.get<GlobalTransform>(grandchild)->matrix.translation(), Vec3{0, 5, -3}));
}

AETHER_TEST(grouping_nodes_and_dangling_parents) {
    World w({.seed = 1});
    const Entity group = w.create();  // no Transform: acts as identity
    const Entity child = w.create();
    w.add<Transform>(child, Transform{{3, 0, 0}});
    REQUIRE(h::set_parent(w, child, group).has_value());
    propagate_transforms(w);
    CHECK(near(w.get<GlobalTransform>(child)->matrix.translation(), Vec3{3, 0, 0}));

    const Entity parent = w.create(), orphan = w.create();
    w.add<Transform>(parent, Transform{{100, 0, 0}});
    w.add<Transform>(orphan, Transform{{1, 0, 0}});
    REQUIRE(h::set_parent(w, orphan, parent).has_value());
    w.destroy(parent);  // raw destroy leaves a dangling link
    propagate_transforms(w);
    CHECK(near(w.get<GlobalTransform>(orphan)->matrix.translation(), Vec3{1, 0, 0}));
    const auto problems = h::validate(w);
    REQUIRE(problems.size() == 1);
    CHECK(problems[0].find("dangling parent") != std::string::npos);
}

AETHER_TEST(validate_detects_manual_corruption) {
    World w({.seed = 1});
    const Entity a = w.create(), b = w.create();
    w.add<Parent>(b, Parent{a});  // bypasses hierarchy::set_parent
    CHECK(!h::validate(w).empty());
}

AETHER_TEST(deep_chain_does_not_overflow_the_stack) {
    World w({.seed = 1});
    constexpr int kDepth = 100'000;
    Entity prev = w.create();
    w.add<Transform>(prev);
    const Entity root = prev;
    for (int i = 1; i < kDepth; ++i) {
        const Entity e = w.create();
        w.add<Transform>(e, Transform{{0.001f, 0, 0}});
        REQUIRE(h::set_parent(w, e, prev).has_value());
        prev = e;
    }
    propagate_transforms(w);
    CHECK(near(w.get<GlobalTransform>(prev)->matrix.translation().x, 0.001f * (kDepth - 1), 1e-1f));
    CHECK_EQ(h::destroy_recursive(w, root), std::size_t{kDepth});
}

AETHER_TEST(random_forest_matches_brute_force_serial_and_parallel) {
    World w({.seed = 7});
    std::mt19937 rng(99);
    std::uniform_real_distribution<float> pos(-10, 10), ang(-3.1f, 3.1f), scl(0.5f, 1.5f);
    std::vector<Entity> es;
    for (int i = 0; i < 2000; ++i) {
        const Entity e = w.create();
        if (i % 10 != 0)
            w.add<Transform>(e, Transform{{pos(rng), pos(rng), pos(rng)},
                                          normalize(Quat::from_axis_angle({pos(rng), pos(rng), pos(rng) + 0.1f}, ang(rng))),
                                          {scl(rng), scl(rng), scl(rng)}});
        es.push_back(e);
    }
    // Random reparenting, including rejected cycles, to build a forest.
    std::uniform_int_distribution<std::size_t> pick(0, es.size() - 1);
    int rejected = 0;
    for (int i = 0; i < 4000; ++i) {
        const Entity c = es[pick(rng)], p = es[pick(rng)];
        if (!h::set_parent(w, c, p)) ++rejected;
        if (i % 7 == 0) h::clear_parent(w, es[pick(rng)]);
    }
    CHECK(rejected > 0);
    CHECK(h::validate(w).empty());

    const auto check_all = [&] {
        int mismatches = 0;
        for (const Entity e : es) {
            const GlobalTransform* g = w.get<GlobalTransform>(e);
            if (!w.has<Transform>(e)) continue;
            REQUIRE(g != nullptr);
            if (!near(g->matrix, brute_force_world(w, e), 2e-2f)) ++mismatches;
        }
        CHECK_EQ(mismatches, 0);
    };
    propagate_transforms(w);
    check_all();

    for (const Entity e : es)
        if (GlobalTransform* g = w.get<GlobalTransform>(e)) g->matrix = Mat4{};  // reset
    JobSystem jobs({.worker_count = 3});
    propagate_transforms(w, &jobs);
    check_all();
}
