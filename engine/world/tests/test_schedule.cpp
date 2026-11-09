#include <atomic>
#include <cstdio>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

#include "aether/foundation/job_system.hpp"
#include "aether/world/schedule.hpp"
#include "aether_test.hpp"
#include "components.hpp"

using namespace aether;
using namespace aether::world;

namespace {
using Plan = std::vector<std::vector<std::string>>;
}

AETHER_TEST(plan_groups_non_conflicting_systems) {
    World w({.seed = 1});
    Schedule s;
    auto noop = [](SystemContext&) {};
    s.add_system("move", SystemAccess::of<const Velocity, Position>(w), noop);
    s.add_system("regen", SystemAccess::of<const Velocity, Health>(w), noop);
    s.add_system("render_prep", SystemAccess::of<const Position>(w), noop);
    s.add_system("names", SystemAccess::of<Name>(w), noop);
    s.add_system("save", SystemAccess::exclusive(), noop);
    s.add_system("after_save", SystemAccess::of<const Health>(w), noop);

    const Plan expected{{"move", "regen", "names"}, {"render_prep"}, {"save"}, {"after_save"}};
    CHECK(s.plan() == expected);
}

AETHER_TEST(schedule_runs_systems_and_applies_commands) {
    for (const bool parallel : {false, true}) {
        JobSystem jobs({.worker_count = 3});
        World w({.seed = 1});
        for (int i = 0; i < 100; ++i) {
            const Entity e = w.create();
            w.add<Position>(e);
            w.add<Velocity>(e, Velocity{1, 2, 0});
            w.add<Health>(e, Health{i});
        }
        Schedule s;
        s.add_system("move", SystemAccess::of<const Velocity, Position>(w), [](SystemContext& ctx) {
            ctx.each<const Velocity, Position>([&](const Velocity& v, Position& p) {
                p.x += v.x * static_cast<float>(ctx.dt);
                p.y += v.y * static_cast<float>(ctx.dt);
            });
        });
        s.add_system("cull", SystemAccess::of<const Health>(w), [](SystemContext& ctx) {
            ctx.each<const Health>([&](Entity e, const Health& h) {
                if (h.value < 10) ctx.commands.destroy(e);
            });
        });
        std::size_t count_before_run = 0;
        std::size_t count_seen_in_run = 0;
        s.add_system("count", SystemAccess::exclusive(), [&](SystemContext& ctx) {
            count_seen_in_run = ctx.world().entity_count();
        });

        for (int frame = 0; frame < 4; ++frame) {
            count_before_run = w.entity_count();
            s.run(w, parallel ? &jobs : nullptr, 0.5);
            // "cull" ran before "count", but its commands only apply after the run.
            CHECK_EQ(count_seen_in_run, count_before_run);
        }

        CHECK_EQ(w.entity_count(), std::size_t{90});
        w.each<const Position>([](const Position& p) {
            CHECK(p.x == 2.0f);
            CHECK(p.y == 4.0f);
        });
    }
}

AETHER_TEST(conflicting_systems_never_overlap) {
    JobSystem jobs({.worker_count = 4});
    World w({.seed = 1});
    w.add<Health>(w.create());

    std::atomic<int> writers_in_flight{0};
    std::atomic<int> max_writers{0};
    std::atomic<int> readers_in_flight{0};
    std::atomic<int> max_readers{0};
    const auto enter = [](std::atomic<int>& in_flight, std::atomic<int>& max_seen) {
        const int now = in_flight.fetch_add(1) + 1;
        int prev = max_seen.load();
        while (now > prev && !max_seen.compare_exchange_weak(prev, now)) {}
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        in_flight.fetch_sub(1);
    };

    Schedule s;
    for (int i = 0; i < 4; ++i)
        s.add_system("writer" + std::to_string(i), SystemAccess::of<Health>(w),
                     [&](SystemContext&) { enter(writers_in_flight, max_writers); });
    for (int i = 0; i < 4; ++i)
        s.add_system("reader" + std::to_string(i), SystemAccess::of<const Position>(w),
                     [&](SystemContext&) { enter(readers_in_flight, max_readers); });

    for (int run = 0; run < 3; ++run) s.run(w, &jobs, 0.016);
    CHECK_EQ(max_writers.load(), 1);
    // Readers share a wave; with 4 workers they are expected to overlap,
    // but scheduling is not guaranteed, so only require that they may.
    CHECK(max_readers.load() >= 1);
    std::printf("    max concurrent readers observed: %d\n", max_readers.load());
}
