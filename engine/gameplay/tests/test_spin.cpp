#include "aether/gameplay/spin.hpp"
#include "aether/world/hierarchy.hpp"
#include "aether_test.hpp"

using namespace aether;
using namespace aether::gameplay;
using math::Vec3;

AETHER_TEST(spin_system_rotates_transforms) {
    world::World w({.seed = 1});
    const world::Entity e = w.create();
    w.add<world::Transform>(e);
    w.add<Spin>(e, Spin{{0, 1, 0}, math::kPi / 2});  // 90 deg/s
    const world::Entity still = w.create();
    w.add<world::Transform>(still);
    w.add<Spin>(still, Spin{{0, 0, 0}, 5.0f});  // zero axis: ignored, no NaN
    world::Schedule s;
    add_systems(s, w);
    for (int i = 0; i < 100; ++i) s.run(w, nullptr, 0.01);  // 1 second
    const Vec3 forward = math::rotate(w.get<world::Transform>(e)->rotation, {1, 0, 0});
    CHECK(math::near(forward, Vec3{0, 0, -1}, 1e-3f));  // +X turned 90 degrees about +Y
    CHECK(w.get<world::Transform>(still)->rotation == math::Quat::identity());
}

AETHER_TEST(spin_serializes_and_validates) {
    world::SerializerRegistry reg;
    world::register_core_components(reg);
    register_components(reg);
    world::World a({.seed = 2});
    const world::Entity e = a.create();
    a.add<Spin>(e, Spin{{0.5f, 1, 0}, 2.5f});
    world::World b({.seed = 3});
    auto report = world::load_level(world::save_level(a, reg), b, reg);
    REQUIRE(report.has_value());
    const Spin* s = b.get<Spin>(b.find(a.id_of(e)));
    REQUIRE(s != nullptr);
    CHECK(s->speed == 2.5f);
    CHECK(s->axis == (Vec3{0.5f, 1, 0}));

    auto bad = parse_json(R"({"format": "aether.level", "version": 1, "entities": [
        {"id": "00000000-0000-4000-8000-000000000001", "components": {"Spin": {"speed": "fast"}}}]})");
    world::World c({.seed = 4});
    auto r = world::load_level(bad.value(), c, reg);
    REQUIRE(r.has_value());
    CHECK_EQ(r.value().warnings.size(), std::size_t{1});
    CHECK(!c.has<Spin>(c.find(Uuid::parse("00000000-0000-4000-8000-000000000001").value())));
}
