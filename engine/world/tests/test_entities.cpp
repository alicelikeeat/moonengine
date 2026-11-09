#include <cstdint>
#include <string>
#include <vector>

#include "aether/world/world.hpp"
#include "aether_test.hpp"
#include "components.hpp"

using namespace aether;
using namespace aether::world;

AETHER_TEST(create_and_destroy_entities) {
    World w({.seed = 1});
    const Entity a = w.create();
    const Entity b = w.create();
    CHECK(a != b);
    CHECK(w.alive(a) && w.alive(b));
    CHECK_EQ(w.entity_count(), std::size_t{2});
    CHECK(w.destroy(a));
    CHECK(!w.alive(a));
    CHECK(!w.destroy(a));
    CHECK_EQ(w.entity_count(), std::size_t{1});
    CHECK(!w.alive(kNullEntity));
}

AETHER_TEST(recycled_index_gets_new_generation) {
    World w({.seed = 1});
    const Entity a = w.create();
    w.add<Health>(a, Health{5});
    w.destroy(a);
    const Entity b = w.create();
    CHECK_EQ(b.index, a.index);
    CHECK(b.generation != a.generation);
    CHECK(!w.alive(a));
    CHECK(w.get<Health>(a) == nullptr);  // stale handle sees nothing
    CHECK(w.get<Health>(b) == nullptr);  // new entity starts empty
}

AETHER_TEST(entities_have_persistent_ids) {
    World w({.seed = 99});
    const Entity a = w.create();
    const Uuid id = w.id_of(a);
    CHECK(!id.is_nil());
    CHECK(w.find(id) == a);
    w.destroy(a);
    CHECK(w.find(id).is_null());
    CHECK(w.id_of(a).is_nil());
}

AETHER_TEST(create_with_id_rejects_duplicates_and_nil) {
    World w({.seed = 3});
    const Uuid id = Uuid::parse("123e4567-e89b-42d3-a456-426614174000").value();
    auto first = w.create_with_id(id);
    REQUIRE(first.has_value());
    CHECK(w.id_of(first.value()) == id);
    auto again = w.create_with_id(id);
    REQUIRE(!again);
    CHECK(again.error().code == ErrorCode::AlreadyExists);
    auto nil = w.create_with_id(Uuid{});
    REQUIRE(!nil);
    CHECK(nil.error().code == ErrorCode::InvalidArgument);
}

AETHER_TEST(seeded_worlds_generate_identical_ids) {
    World a({.seed = 1234}), b({.seed = 1234});
    for (int i = 0; i < 100; ++i) CHECK(a.id_of(a.create()) == b.id_of(b.create()));
}

AETHER_TEST(add_get_remove_components) {
    World w({.seed = 1});
    const Entity e = w.create();
    w.add<Position>(e, Position{1, 2, 3});
    w.add<Velocity>(e, Velocity{4, 5, 6});
    w.add<Name>(e, Name{"lamp"});
    REQUIRE(w.get<Position>(e) != nullptr);
    CHECK(w.get<Position>(e)->y == 2.0f);
    CHECK(w.get<Velocity>(e)->z == 6.0f);
    CHECK_EQ(w.get<Name>(e)->value, std::string("lamp"));

    CHECK(w.remove<Velocity>(e));
    CHECK(!w.remove<Velocity>(e));
    CHECK(!w.has<Velocity>(e));
    CHECK(w.get<Position>(e)->x == 1.0f);  // survived the archetype move
    CHECK_EQ(w.get<Name>(e)->value, std::string("lamp"));

    w.add<Position>(e, Position{9, 9, 9});  // replace in place
    CHECK(w.get<Position>(e)->x == 9.0f);
    CHECK_EQ(w.components_of(e).size(), std::size_t{2});
}

AETHER_TEST(swap_remove_keeps_other_entities_intact) {
    World w({.seed = 1});
    std::vector<Entity> es;
    for (int i = 0; i < 100; ++i) {
        es.push_back(w.create());
        w.add<Health>(es.back(), Health{i});
    }
    for (int i = 0; i < 100; i += 3) w.destroy(es[static_cast<std::size_t>(i)]);
    for (int i = 0; i < 100; ++i) {
        const Entity e = es[static_cast<std::size_t>(i)];
        if (i % 3 == 0) {
            CHECK(!w.alive(e));
        } else {
            REQUIRE(w.get<Health>(e) != nullptr);
            CHECK_EQ(w.get<Health>(e)->value, i);
        }
    }
}

AETHER_TEST(component_lifetimes_are_balanced) {
    Tracked::live = 0;
    HeapAllocator heap;
    {
        World w({.seed = 1, .allocator = &heap});
        std::vector<Entity> es;
        for (int i = 0; i < 200; ++i) {
            es.push_back(w.create());
            w.add<Tracked>(es.back(), i);
            if (i % 2) w.add<Health>(es.back());
        }
        CHECK_EQ(Tracked::live.load(), 200);
        w.add<Tracked>(es[0], 1000);  // replace: old destroyed, new built
        CHECK_EQ(Tracked::live.load(), 200);
        CHECK_EQ(w.get<Tracked>(es[0])->value, 1000);
        for (int i = 0; i < 200; i += 2) w.remove<Tracked>(es[static_cast<std::size_t>(i)]);
        CHECK_EQ(Tracked::live.load(), 100);
        for (int i = 1; i < 50; i += 2) w.destroy(es[static_cast<std::size_t>(i)]);
        CHECK_EQ(Tracked::live.load(), 75);
        CHECK_EQ(w.get<Tracked>(es[99])->value, 99);
    }
    CHECK_EQ(Tracked::live.load(), 0);
    CHECK_EQ(heap.stats().live_allocations, std::uint64_t{0});
}

AETHER_TEST(move_only_and_overaligned_components) {
    World w({.seed = 1});
    const Entity e = w.create();
    w.add<Owned>(e, Owned{std::make_unique<int>(7)});
    for (int i = 0; i < 40; ++i) {  // force column growth
        const Entity other = w.create();
        w.add<CacheLine>(other);
        w.add<Owned>(other, Owned{std::make_unique<int>(i)});
    }
    w.add<CacheLine>(e);
    CHECK_EQ(*w.get<Owned>(e)->ptr, 7);
    CHECK(reinterpret_cast<std::uintptr_t>(w.get<CacheLine>(e)) % 64 == 0);
}

AETHER_TEST(components_are_addressable_by_name) {
    World w({.seed = 1});
    const Entity e = w.create();
    w.add<Velocity>(e);
    w.add<Unnamed>(e);
    const auto id = w.find_component("Velocity");
    REQUIRE(id.has_value());
    CHECK(w.has(e, *id));
    CHECK_EQ(w.component_info(*id).name, std::string("Velocity"));
    CHECK(w.component_info(*id).stable_name);
    CHECK(!w.component_info(*w.find_component_id<Unnamed>()).stable_name);
    CHECK(!w.find_component("Nope").has_value());
}
