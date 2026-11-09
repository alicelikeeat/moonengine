#include <atomic>
#include <vector>

#include "aether/foundation/job_system.hpp"
#include "aether/world/world.hpp"
#include "aether_test.hpp"
#include "components.hpp"

using namespace aether;
using namespace aether::world;

namespace {
void populate(World& w) {
    for (int i = 0; i < 30; ++i) {
        const Entity e = w.create();
        w.add<Position>(e, Position{static_cast<float>(i), 0, 0});
        if (i % 2 == 0) w.add<Velocity>(e, Velocity{1, 0, 0});
        if (i % 3 == 0) w.add<Health>(e, Health{i});
    }
}
}  // namespace

AETHER_TEST(each_visits_matching_archetypes_only) {
    World w({.seed = 1});
    populate(w);
    CHECK_EQ(w.count<Position>(), std::size_t{30});
    CHECK_EQ((w.count<Position, Velocity>()), std::size_t{15});
    CHECK_EQ((w.count<Velocity, Health>()), std::size_t{5});

    int visits = 0;
    w.each<const Velocity, Position>([&](Entity e, const Velocity& v, Position& p) {
        CHECK(w.alive(e));
        p.x += v.x;
        ++visits;
    });
    CHECK_EQ(visits, 15);

    float sum = 0;
    w.each<const Position>([&](const Position& p) { sum += p.x; });
    CHECK(sum == 435.0f + 15.0f);  // 0..29 plus one step for each mover
}

AETHER_TEST(each_with_unregistered_component_does_nothing) {
    World w({.seed = 1});
    populate(w);
    int calls = 0;
    w.each<Position, Name>([&](Position&, Name&) { ++calls; });
    CHECK_EQ(calls, 0);
    CHECK_EQ(w.count<Name>(), std::size_t{0});
}

AETHER_TEST(par_each_matches_each) {
    JobSystem jobs({.worker_count = 3});
    World w({.seed = 1});
    for (int i = 0; i < 5000; ++i) {
        const Entity e = w.create();
        w.add<Position>(e, Position{1, 0, 0});
        w.add<Velocity>(e, Velocity{static_cast<float>(i % 7), 0, 0});
        if (i % 4 == 0) w.add<Health>(e);
    }
    w.par_each<const Velocity, Position>(jobs, 128, [](const Velocity& v, Position& p) { p.x += v.x; });
    std::atomic<int> visited{0};
    w.par_each<const Position>(jobs, 100, [&](Entity, const Position&) { visited.fetch_add(1); });
    CHECK_EQ(visited.load(), 5000);

    double expected = 0, actual = 0;
    for (int i = 0; i < 5000; ++i) expected += 1.0 + (i % 7);
    w.each<const Position>([&](const Position& p) { actual += p.x; });
    CHECK(expected == actual);
}

AETHER_TEST(concurrent_read_queries_are_allowed) {
    World w({.seed = 1});
    populate(w);
    JobSystem jobs({.worker_count = 4});
    std::atomic<int> total{0};
    WaitGroup g;
    for (int t = 0; t < 8; ++t)
        jobs.submit([&] {
            int n = 0;
            w.each<const Position>([&](const Position&) { ++n; });
            total.fetch_add(n);
        }, &g);
    jobs.wait(g);
    CHECK_EQ(total.load(), 8 * 30);
}
