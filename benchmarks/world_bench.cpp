// World (ECS) micro-benchmarks. One JSON object per line.
#include <algorithm>
#include <cstdio>
#include <string_view>
#include <vector>

#include "aether/foundation/job_system.hpp"
#include "aether/foundation/platform.hpp"
#include "aether/foundation/time.hpp"
#include "aether/world/hierarchy.hpp"
#include "aether/world/world.hpp"

using namespace aether;
using namespace aether::world;

namespace {

struct Position { static constexpr std::string_view kComponentName = "Position"; float x = 0, y = 0, z = 0; };
struct Velocity { static constexpr std::string_view kComponentName = "Velocity"; float x = 1, y = 1, z = 1; };
struct Health { static constexpr std::string_view kComponentName = "Health"; int value = 100; };

void report(const char* name, double ns, double items) {
    std::printf("{\"bench\":\"%s\",\"best_ns\":%.0f,\"ns_per_item\":%.2f,\"items\":%.0f}\n",
                name, ns, ns / items, items);
}

template <typename Fn>
double best_of(int runs, Fn&& fn) {
    double best = 1e300;
    for (int i = 0; i < runs; ++i) {
        Stopwatch sw;
        fn();
        best = std::min(best, static_cast<double>(sw.elapsed_ns()));
    }
    return best;
}

}  // namespace

int main() {
    std::printf("{\"os\":\"%.*s\",\"hardware_threads\":%u}\n",
                static_cast<int>(platform::os_name().size()), platform::os_name().data(),
                platform::hardware_threads());
    constexpr int kEntities = 1'000'000;

    report("create_with_2_components", best_of(3, [&] {
        World w({.seed = 1});
        for (int i = 0; i < kEntities; ++i) {
            const Entity e = w.create();
            w.add<Position>(e);
            w.add<Velocity>(e);
        }
    }), kEntities);

    World w({.seed = 1});
    std::vector<Entity> es;
    es.reserve(kEntities);
    for (int i = 0; i < kEntities; ++i) {
        es.push_back(w.create());
        w.add<Position>(es.back());
        w.add<Velocity>(es.back());
        if (i % 2) w.add<Health>(es.back());
    }

    report("each_integrate_1m", best_of(10, [&] {
        w.each<const Velocity, Position>([](const Velocity& v, Position& p) {
            p.x += v.x * 0.016f; p.y += v.y * 0.016f; p.z += v.z * 0.016f;
        });
    }), kEntities);

    JobSystem jobs;
    report("par_each_integrate_1m", best_of(10, [&] {
        w.par_each<const Velocity, Position>(jobs, 16'384, [](const Velocity& v, Position& p) {
            p.x += v.x * 0.016f; p.y += v.y * 0.016f; p.z += v.z * 0.016f;
        });
    }), kEntities);

    constexpr int kChurn = 100'000;
    report("add_remove_component_churn", best_of(3, [&] {
        for (int i = 0; i < kChurn; ++i) {
            const Entity e = es[static_cast<std::size_t>(i) * 7 % kEntities];
            if (w.has<Health>(e)) w.remove<Health>(e); else w.add<Health>(e);
        }
    }), kChurn);

    report("get_random_access", best_of(5, [&] {
        float sum = 0;
        for (int i = 0; i < kChurn; ++i) sum += w.get<Position>(es[static_cast<std::size_t>(i) * 13 % kEntities])->x;
        if (sum < 0) std::printf("%f", static_cast<double>(sum));
    }), kChurn);

    // Transform propagation: 10,000 trees of 100 entities (root + 99 nodes,
    // each attached to a random earlier node in its tree).
    {
        World scene({.seed = 3});
        std::uint32_t lcg = 12345;
        std::vector<Entity> tree;
        for (int t = 0; t < 10'000; ++t) {
            tree.clear();
            for (int i = 0; i < 100; ++i) {
                const Entity e = scene.create();
                scene.add<Transform>(e, Transform{{1, 0, 0}});
                if (i > 0) {
                    lcg = lcg * 1664525u + 1013904223u;
                    (void)hierarchy::set_parent(scene, e, tree[lcg % tree.size()]);
                }
                tree.push_back(e);
            }
        }
        propagate_transforms(scene);  // adds GlobalTransform once
        report("propagate_transforms_1m", best_of(5, [&] { propagate_transforms(scene); }), 1'000'000);
        report("propagate_transforms_1m_parallel",
               best_of(5, [&] { propagate_transforms(scene, &jobs); }), 1'000'000);
    }
    return 0;
}
