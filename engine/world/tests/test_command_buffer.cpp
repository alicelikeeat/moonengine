#include "aether/world/command_buffer.hpp"
#include "aether_test.hpp"
#include "components.hpp"

using namespace aether::world;

AETHER_TEST(commands_defer_structural_changes) {
    World w({.seed = 1});
    for (int i = 0; i < 10; ++i) w.add<Health>(w.create(), Health{i});

    CommandBuffer cmd;
    w.each<const Health>([&](Entity e, const Health& h) {
        if (h.value % 2) cmd.destroy(e);
        else cmd.add<Name>(e, Name{"even"});
    });
    CHECK_EQ(w.entity_count(), std::size_t{10});  // nothing applied yet
    CHECK_EQ(cmd.size(), std::size_t{10});

    const auto stats = cmd.apply(w);
    CHECK_EQ(stats.applied, std::size_t{10});
    CHECK(cmd.empty());
    CHECK_EQ(w.entity_count(), std::size_t{5});
    CHECK_EQ((w.count<Health, Name>()), std::size_t{5});
}

AETHER_TEST(commands_on_dead_entities_are_skipped) {
    World w({.seed = 1});
    const Entity e = w.create();
    CommandBuffer cmd;
    cmd.destroy(e);
    cmd.add<Health>(e, Health{1});
    cmd.remove<Health>(e);
    cmd.destroy(e);
    const auto stats = cmd.apply(w);
    CHECK_EQ(stats.applied, std::size_t{1});
    CHECK_EQ(stats.skipped, std::size_t{3});
}

AETHER_TEST(spawn_runs_initialiser_in_order) {
    World w({.seed = 1});
    CommandBuffer cmd;
    cmd.spawn([](World& world, Entity e) { world.add<Owned>(e, Owned{std::make_unique<int>(5)}); });
    cmd.add<Owned>(kNullEntity, Owned{});  // move-only payload is accepted, then skipped
    cmd.spawn();
    const auto stats = cmd.apply(w);
    CHECK_EQ(stats.applied, std::size_t{2});
    CHECK_EQ(stats.skipped, std::size_t{1});
    CHECK_EQ(w.entity_count(), std::size_t{2});
    int seen = 0;
    w.each<Owned>([&](Owned& o) { seen += *o.ptr; });
    CHECK_EQ(seen, 5);
}
