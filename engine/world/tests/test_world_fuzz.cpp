// Randomised differential test: applies a long random sequence of
// operations to a World and to a trivially correct reference model, and
// checks they always agree.
#include <optional>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

#include "aether/world/world.hpp"
#include "aether_test.hpp"
#include "components.hpp"

using namespace aether;
using namespace aether::world;

namespace {

struct Model {
    std::optional<int> health;
    std::optional<std::string> name;
    std::optional<int> tracked;
    Uuid id;
};

void verify(World& w, const std::unordered_map<Entity, Model>& model) {
    CHECK_EQ(w.entity_count(), model.size());
    std::size_t with_health = 0, with_name = 0, with_tracked = 0;
    for (const auto& [e, m] : model) {
        REQUIRE(w.alive(e));
        CHECK(w.id_of(e) == m.id);
        CHECK(w.find(m.id) == e);
        const Health* h = w.get<Health>(e);
        CHECK(h ? m.health && *m.health == h->value : !m.health);
        const Name* n = w.get<Name>(e);
        CHECK(n ? m.name && *m.name == n->value : !m.name);
        const Tracked* t = w.get<Tracked>(e);
        CHECK(t ? m.tracked && *m.tracked == t->value : !m.tracked);
        with_health += m.health ? 1 : 0;
        with_name += m.name ? 1 : 0;
        with_tracked += m.tracked ? 1 : 0;
    }
    CHECK_EQ(w.count<Health>(), with_health);
    CHECK_EQ(w.count<Name>(), with_name);
    CHECK_EQ(w.count<Tracked>(), with_tracked);
    CHECK_EQ(static_cast<std::size_t>(Tracked::live.load()), with_tracked);
}

}  // namespace

AETHER_TEST(world_matches_reference_model_under_random_ops) {
    Tracked::live = 0;
    HeapAllocator heap;
    {
        World w({.seed = 2024, .allocator = &heap});
        std::unordered_map<Entity, Model> model;
        std::vector<Entity> handles;    // includes dead handles on purpose
        std::mt19937 rng(12345);
        const auto pick = [&]() -> Entity {
            if (handles.empty()) return kNullEntity;
            return handles[std::uniform_int_distribution<std::size_t>(0, handles.size() - 1)(rng)];
        };

        for (int step = 0; step < 20'000; ++step) {
            const int op = std::uniform_int_distribution<int>(0, 9)(rng);
            const int value = std::uniform_int_distribution<int>(0, 1'000'000)(rng);
            const Entity e = pick();
            const bool live = model.contains(e);
            switch (op) {
                case 0:
                case 1: {
                    const Entity n = w.create();
                    Model fresh;
                    fresh.id = w.id_of(n);
                    model[n] = fresh;
                    handles.push_back(n);
                    break;
                }
                case 2:
                    CHECK_EQ(w.destroy(e), live);
                    model.erase(e);
                    break;
                case 3:
                    if (live) { w.add<Health>(e, Health{value}); model[e].health = value; }
                    break;
                case 4:
                    if (live) { w.add<Name>(e, Name{std::to_string(value)}); model[e].name = std::to_string(value); }
                    break;
                case 5:
                    if (live) { w.add<Tracked>(e, value); model[e].tracked = value; }
                    break;
                case 6:
                    CHECK_EQ(w.remove<Health>(e), live && model[e].health.has_value());
                    if (live) model[e].health.reset();
                    break;
                case 7:
                    CHECK_EQ(w.remove<Name>(e), live && model[e].name.has_value());
                    if (live) model[e].name.reset();
                    break;
                case 8:
                    CHECK_EQ(w.remove<Tracked>(e), live && model[e].tracked.has_value());
                    if (live) model[e].tracked.reset();
                    break;
                case 9:
                    CHECK_EQ(w.alive(e), live);
                    break;
            }
            if (step % 2000 == 0) verify(w, model);
        }
        verify(w, model);
    }
    CHECK_EQ(Tracked::live.load(), 0);
    CHECK_EQ(heap.stats().live_allocations, std::uint64_t{0});
}
